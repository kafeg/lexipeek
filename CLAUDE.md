# LexiPeek

Pop-up dictionary and offline translator for the desktop (macOS first, then Windows, Linux): select text in any app, press a hotkey, a card opens at the
cursor. Desktop sibling of InkPeek, uses its language packs. Spec — `docs/spec.md` (source of truth).
Public repo (MIT): no secrets, keys, personal paths or IPs in commits.

## Stack
C++20 core with all the logic + a thin native shell per OS. `core/` — no UI, no OS headers; vcpkg: sqlite3,
nlohmann/json, libcurl, libarchive (zstd), hash-library, utf8proc, doctest (from M1: the spec's Stack); facade `core/include/lexipeek/` (plain types, Swift-importable).
`mac/` — Swift, SwiftUI + AppKit, XcodeGen, C++ interop (as panerail). `qt/` (M6) — Qt 6 Widgets for Windows and Linux.
Engines as submodules at InkPeek's commits: Bergamot, PCRE2, fastText, llama.cpp. macOS 14+, Apple Silicon only.

## Build and run
Submodules: `vcpkg/` (manifest mode, `vcpkg.json`, toolchain set in `CMakeLists.txt`), `engines/` (Bergamot,
fastText, llama.cpp, PCRE2 at InkPeek's commits), `tests/inkpeek` (InkPeek, test data and the engine pins).
- `make deps` — submodules (Bergamot recursively; not `--recursive` at the root: InkPeek's own engines are not needed).
- `make build` — CMake + Ninja into `build/`; the first build compiles the vcpkg ports and the engines (~8 min).
- `make test` — ctest: `core` (doctest, `tests/*_test.cpp`), `engine-pins`, `cli-usage`. Model tests run when
  `LEXIPEEK_MODELS` names a data dir with the models, else they say "skipped".
- `make asan` — the same with ASan + UBSan on our code (`build-asan/`). `make bench` — `lexipeek-cli bench`.
- Packs: `lexipeek-cli packs list | status | install <id>… | remove <id>… | update [--base URL]` from InkPeek's `packs`
  release (sha256, resumable); layout as InkPeek's (`dicts/<id>/`, `models/<from><to>/`, `opus/<id>/`, `llm/<id>.gguf`,
  `langid.ftz`, `installed.json`). CI still fetches models with `scripts/fetch-models.sh` (cache key, runs before the build).
  Default data dir: `~/Library/Application Support/LexiPeek` (or `LEXIPEEK_DATA`, `--data`).
- App (macOS): `make app` (core + `mac/` by XcodeGen, Release, ad-hoc signed; `SIGN=<identity>` keeps the
  Accessibility grant across builds), `make app-test` (Swift unit tests of `LexiPeekKit`), `make install`
  (into `/Applications` in place, refreshes Services, starts it). Check without Accessibility:
  `open "lexipeek://lookup?text=Das%20Haus"` or `lexipeek://input`.
- **UI check: `make snapshots`** (`scripts/snapshots.sh [out-dir]`, ~1.5 min) — the built app draws a fixed set of cards
  (word, full entry with sections, a section, a gloss word opened, phrase and passages on both tabs, long list scrolled
  to the end, same language, no pack, unknown, input, selection failed, saved ★), Settings → Notes' three tabs and every
  settings section × tab in its toolbar window (`settings-<section>[-<tab>]`), light and dark, into `build/snapshots/index.html`. **Look at every image before
  handing over UI work**. The run is invisible
  (accessory app, windows off screen, no menu-bar item, no hotkeys, never activated): it must never show a window on
  the owner's screen; a new card state → a new case
  in the script. How: `LEXIPEEK_SNAPSHOT=<dir>` (`Snapshot.swift`) — the app reads `lexipeek://…&snap=<name>[&tab=
  dictionary][&full=1][&open=<word>][&section=<key>]` (open: a word clicked in the card; section: entry 0's) from
  `<dir>/request` (not `open`: an installed copy would take the URL), writes `<name>.png`,
  `<name>-end.png` when the body scrolls, then `<name>.done`; `&save=1` presses ☆ once the card is final, `&method=fast` overrides the method,
  `lexipeek://notes?tab=words|phrases|history&snap=<name>` shoots Settings → Notes; no Esc/⌘P/outside-click closing; packs symlinked from
  `lexipeek://firstrun?step=<access|list|list-error|pair|download|error|done|done-later>&snap=<name>` draws that first-run
  step (a second instance on an empty data dir, packs.json / installed.json copied in as the steps go);
  the data dir, history empty. Another `make install` (`pkill -x LexiPeek`) stops the run: it says so.
- Own dictionaries: `lexipeek-cli dicts list | import <file>… | add-folder <dir> | remove-folder <dir> | on|off|remove <id> |
  catalog [filter] | get <url>` (`<data>/dicts-own/`, settings.json's `dicts`).
- CLI: `build/cli/lexipeek-cli translate --from de --to ru [--method fast|direct|llm] TEXT`, `langid TEXT`, `bench`, `merge FOLDER`, `history`.

## Code layout and gotchas
- `core/include/lexipeek/` — the facade (`mt.hpp`: `Translator`, `packs.hpp`, `core.hpp`, `views.hpp`, `app.hpp`);
  `core/src/<module>/` — one folder per module of the spec's Architecture, empty ones are stubs for their milestone.
- `core/src/views/`: `views` — what screens show: `card_view(Card, CardState)` → `CardView` (header and its style, badge,
  tabs and the open one, body rows per tab, every state's text, ☆, footer: "←", method menu, "Full entry ⏎", a section's pager),
  `first_state`, Notes (`notes_view`, `remove_note`: Words · Phrases · History rows), the first run
  (`first_run_view(Manifest, installed, FirstRunState)` → step, texts, choices, progress; `first_run_at_launch`,
  `first_answer`, `has_pair`), the packs list (`pack_rows`, `missing_ids`, `freed_ids`, `answer_languages`, `packs_summary`,
  `pair_detail`); `settings` — Settings' panes: `settings_view(section, tab, Settings, SettingsState)` → sections, sub-tabs,
  a pane drawn by its own view (packs, other, notes) or rows (InkPeek's Rows: kind, key, title, one-line about, control),
  `settings_toggle` / `settings_choose` by a row's key; `app` — the facade for
  Swift (`lexipeek::app`): data dir as a string, `Engine` (a copyable `shared_ptr<Core>`), `Download` (polled atomics),
  no `std::function` / `std::filesystem::path` / `std::optional` in what Swift calls.
- `Core::configure(Settings)` applies the answer language, method, the card's settings (`CardPrefs`: what a phrase opens
  on, full entry, grammar, `.rel` sections, width, text size, font, height limit — copied into each `Card::prefs`, honoured
  by `first_state` / `card_view`) to the next cards; `Core::reload()` after a
  packs change (dictionaries again, the held model freed). Fast through English keeps the English as the text's own
  `en` answer (InkPeek's); `Translation::english` is filled only with the `mt_english` setting. Offline answers: `ver` = `answer_version` (Java
  `hashCode` of the installed models' sha256, as InkPeek's `OfflineTranslation`), model field `bergamot`/`opus`/`llm`.
- `core/src/dict/` (InkPeek's `stardict/`): `stardict` (own sorted index in `<data>/index/<id>/`, `.dict.dz` by dictzip
  chunks), `library` (lookup across packs — the text's language's packs into the answer first, as InkPeek's ticked
  pairs —, heads, gloss, separable verbs, rank, a pack article's senses; the own dictionaries (`Own`: dicts-own and
  folders, off, order) after the packs, `found()` all on disk in order, `all()` those in use; `StarDict::pack()` by
  its root, not its folder name), `short` (InkPeek's `shortMeaning`, `head(hit)`: a pack's line or "word — meaning"),
  `importer` (libarchive: zip, tar + gzip/bzip2/xz/zstd; names leaving the folder skipped; `.ifo` copied last),
  `catalog` (WikDict / Wiktionary / FreeDict listings, ISO 639 codes and names, `download_dict` records `got`).
  `views/dicts`: Settings → Other dictionaries rows, edits of `DictSettings`, catalog rows; facade `lexipeek/dicts.hpp`. `core/src/text/`: PCRE2 wrapper, InkPeek's
  lookup key (NFC, no soft hyphen, lowercase), query cleaning. `core/src/lookup/`: `Core` — request → `Card` (one `Entry` per
  dictionary: line, articles parsed into senses and examples, `.rel` sections), `section()` (a section's page of words with
  meanings, 20 per page), language by the dictionaries (InkPeek's `LangId.source`). Cross-test: `tests/inkpeek_cases_test.cpp` runs InkPeek's `cases.tsv`.
- `core/src/packs/`: `packs` (list, install, remove, levels, needs, `cannot` reasons), `http` (libcurl: Range resume,
  progress, cancel; native CA store on macOS / Windows). Tests serve a fixture release by `file://` (no network).
- `core/src/mt/`: `text` (steps, Marian options, sentences, chunks — pure, tested), `model_host` (one model, idle
  thread), `bergamot`/`llm`/`langid` (the engines, as InkPeek's `*_jni.cpp`), `translator` (the methods).
- Sentences: `mt/sentences.cpp`, UAX #29 as ICU without suppressions; table `mt/sentence_break.inc` from
  `scripts/gen-sentence-break.py <SentenceBreakProperty.txt>` (Unicode 17); tests: `tests/data/SentenceBreakTest.txt`, InkPeek's cases.
- `core/src/store/`: `log` (lines, HLC, read, append, merge — pure, InkPeek's `tests/sync` cases), `store` (`Store`:
  own lines to `<data>/log/<device>.jsonl`, `<data>/lexipeek.db` is a view: rebuilt from the logs on open and
  `merge()`, a text's rows re-merged from its lines on each write; the 1,000-entry limit only in the view).
  Compaction (`log` `compact_file`): only the own file, on open, when it has ≥ 1,000 lines and ≥ 2× what it compacts
  to, so a rewrite always at least halves it and small logs are never rewritten; `<device>.jsonl.tmp`, then renamed.
- Marian's headers do not compile as C++20 (`std::result_of`): `bergamot.cpp` is its own C++17 target.
- Bergamot's `BlockingService` is never destroyed: at exit it would outlive marian's statics and abort.
- Marian needs BLAS and LAPACK: Accelerate on macOS, OpenBLAS + clapack (vcpkg, no Fortran) elsewhere. Windows: static CRT (`/MT`, Bergamot's
  choice) everywhere, triplet `x64-windows-static`; macOS: overlay triplets in `cmake/triplets/` (macOS 14).
- Engines build as C++17 (`cmake/engines.cmake`), our code as C++20. Linux: OpenBLAS `dynamic-arch` (a cached
  build runs on other runners' CPUs, else SIGILL), marian's `USE_STATIC_LIBS` off (glibc's `libm.a`).
- CI (`.github/workflows/ci.yml`): macOS arm64, Linux x64, Windows x64, Linux ASan + UBSan; models from the packs
  release on macOS and Linux (cached), none on Windows.
- `mac/`: `cmake/apple_bundle.cmake` walks `lexipeek_core`'s link closure and `libtool`s it into
  `build/mac/liblexipeek_all.a` + `link.xcconfig` (frameworks, system libs); Swift imports the facade through
  `mac/Bridge` (module `LexiPeekCore`): only typedefs for template instances and `std::optional` readers — no logic.
  The card (`CardView`, `CardModel`) draws `lexipeek::card_view`; its state is the core's `CardState`. A clicked word
  (row `link`) opens in the same card; `CardModel` keeps the cards before it (`CardState.before` names the last), "←" restores them.
  ☆: `Card`'s `save_text`/`save_kind`/`save_line` (InkPeek's `saveKey`, set in `lookup`), `Core::toggle_saved`; Settings →
  Notes (`NotesPane.swift`) draws `notes_view` and reloads on `.notesChanged` (posted by the card); the menu's Notes… opens it.
  Settings (`SettingsView.swift`): toolbar sections, a segmented control of sub-tabs, `RowsView` draws the core's rows;
  a change goes `app::toggle` / `choose` → settings.json, then `Prefs.reload()` (its `changed` → `configure`).
  Hotkeys and app lists stay `Prefs` bindings (platform values); `OtherDictionariesPane.swift` is own dictionaries' pane (with the dictionary order; Article's "Dictionary order" opens it). ⌘S is a local key (not a hotkey: a pinned
  card would steal the frontmost app's Save).
  The first-run window (`FirstRun.swift`) draws `app::first_run`; its state is the core's `FirstRunState` (`first_run` in settings.json).
  `LexiPeekKit` — pure Swift (shortcuts, placement, recents, URLs), tested without the core. `CoreClient` — two
  serial queues (look-ups, one translation worker), a newer request turns older ones into no-ops.
- Icons: `mac/Icon/AppIcon.svg` (the card at the cursor) and `MenuBarIcon.svg` (a template glyph) → `scripts/gen-icons.sh`
  (rsvg-convert) → `Assets.xcassets` (PNGs committed). An old icon after a rebuild is LaunchServices' cache: `lsregister -f`.
- A Debug `LexiPeek.app` would also claim `lexipeek://`: the test scheme builds only `LexiPeekKit`.
- Warnings as errors only for our targets (`lexipeek_flags`); engines are `SYSTEM` and keep their flags.

## Rules
- Branch model: `main` only, commit and push to `main`.
- Spec first: a new feature → `docs/spec.md` → implement by milestone, stop after each.
- **InkPeek is the reference.** No shared code. Behaviour and data formats (`packs.json`, sync log, records) are
  defined in InkPeek first; LexiPeek matches them. `tests/inkpeek` (submodule, pinned) gives the cross-test data;
  moving its pin = catching up: bump, run tests, fix LexiPeek. Never change a case to make LexiPeek pass.
- **No AI in core.** Look-up, header and translation work offline from the packs. AI is an optional tab, only on
  the person's action. No guessing heuristics in data.
- **An app, not a machine.** Everything comes from the app's own downloads (InkPeek's `packs` release, sha256).
- **Logic lives in the core**, tested there; a shell only draws `views` and calls the facade. `core/` never
  includes UI or OS headers; strings are UTF-8, normalised with utf8proc (NFC, lowercase), regex only PCRE2, sentences by our UAX #29 segmenter. KISS; comments — one line at most.
- The API key never goes to logs, UI or commits.
- **Nothing of the author's infrastructure** in the repo: no host or VM names, IPs, runner labels, paths. Build
  scripts and CI are generic (GitHub-hosted runners); building on private machines is documented outside this repo.
