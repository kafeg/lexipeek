# LexiPeek — spec

A pop-up dictionary and offline translator for the desktop, macOS first, then Windows and Linux. Select text in any app, press a hotkey: a card opens next to
the cursor with the dictionary entry and a translation made on this Mac. Close it and you are back where you were.
LexiPeek is the desktop sibling of [InkPeek](https://github.com/kafeg/inkpeek) (Android e-ink readers) and uses its
language packs.

## Goals
InkPeek's behaviour and data on the desktop, nearly 1:1: its card (dictionary, gloss, offline translation, ☆, notes),
its own dictionaries, its settings and its AI layer — in desktop form (menu bar, hotkey, an icon at the selection,
Service) instead of Android's intents, without what is e-ink only (black and white, no animations, the icon row).
- One card over any app, from a hotkey or an icon at the selection; it does not take focus.
- Dictionary first: the header with grammar from the packs, one short line per dictionary, the full entry and its
  sections, the base form of an inflected word; a phrase or a passage gets a word-by-word gloss, separable verbs
  included.
- Offline translation, three methods as in InkPeek: Fast · Mozilla, Direct · OPUS-MT, AI model · Hy-MT2 (Metal).
- Everything comes from the app's own downloads: InkPeek's `packs` release, checked by sha256.

## Not yet
Browser extension, whole-page translation, Linux, sync — see Milestones.

## Principles
- The core works offline and without AI. AI is an optional, marked tab, asked only on the person's action.
- No guessing heuristics in data: forms, genders, lemmas come only from the packs.
- One heavy model in memory; it is unloaded when idle.
- No telemetry, no accounts. Network: the pack list, downloads, the update check, AI when it is on.

## Platform
macOS 14+, Apple Silicon only. A menu-bar app (`LSUIElement`). Needs Accessibility (hotkey on the selection, reading
the selected text); the first run asks for it and explains why.

## Stack
One C++ core with all the logic; a thin native shell per OS that only draws what the core gives it.

| part | what |
|---|---|
| `core/` | C++20, CMake, no UI and no OS headers. Libraries (vcpkg manifest, the same on every OS): sqlite3 (C API, own RAII wrapper), nlohmann/json, libcurl (own thin wrapper: Range, progress, cancel), hash-library (sha256), **libarchive** with zstd (packs' `.tar.zst` and `.zip`; StarDict's `.dict.dz` read by our own code on zlib), **utf8proc** (NFC, lowercase as InkPeek's `lowercase(Locale.ROOT)`, not case folding — strings are UTF-8 `std::string` everywhere), doctest. Sentences — our own UAX #29 segmenter, matching InkPeek's (ICU without abbreviation suppressions): InkPeek's `tests/sentences` and Unicode's `SentenceBreakTest.txt`. Regex — PCRE2 (never `std::regex`); formatting — `std::format`. Built and tested on its own (`lexipeek-cli`) |
| `core/include/lexipeek/` | the facade the shells call: plain structs, enums, `std::string`, `std::vector`, `std::optional`, errors as values — importable by Swift's C++ interop and used as is by C++ shells. No C API |
| engines | git submodules, the same pinned commits as InkPeek: Bergamot (RTranslator fork), PCRE2, fastText, llama.cpp (Metal on macOS) |
| `mac/` | Swift, SwiftUI + AppKit, as panerail: XcodeGen `project.yml`, `make build/test/install`. Calls the core through Swift's C++ interop. Menu bar (`LSUIElement`), non-activating `NSPanel` with a system material, hotkey (Carbon `RegisterEventHotKey`), selected text (AX; Cmd+C with the clipboard restored only where allowed), Service, URL scheme, Accessibility, memory pressure |
| `qt/` | (M6) Windows and Linux: Qt 6 Widgets in C++ — the core is called directly; Windows 11 style, on Linux the desktop's theme. Platform parts per OS (Windows: `RegisterHotKey`, UI Automation; Linux: X11 / portals) |
| checks | warnings as errors, clang-tidy, ASan + UBSan test runs |
| CI | GitHub-hosted runners: **the core, the CLI and its tests on macOS, Windows and Linux from M0**; the mac app on macOS; `.app` signed with Developer ID, notarized, on GitHub Releases |

Why native shells: the look must be the system's on every OS; on macOS almost everything is native anyway (panel,
material, Service, Accessibility), and SwiftUI settings look as in panerail. The rule (as panerail's `PaneRailKit`):
everything that decides lives in the core and is tested there; a shell has no logic of its own, so adding an OS is
drawing screens, not porting behaviour. Not used: Electron, Tauri, JVM, QML.

## Relation to InkPeek
Two projects, **no shared code**. **InkPeek is the reference** for behaviour and data formats; LexiPeek is written
to match it and is checked against it.
- **Formats** (`packs.json`, the sync log, the records schema) are defined in InkPeek's docs. A new or changed format
  is written there first, with test cases; then both apps implement it.
- **Cross-tests:** `tests/inkpeek` is a git submodule of InkPeek at a pinned commit, used only for its test data:
  golden words (`packs/tests/golden`), device cases (`app/src/androidTest/assets/cases.tsv`), sync cases. LexiPeek's
  tests run the same cases through `lexipeek-cli`. Moving the pin forward = catching up with the reference: bump,
  run the tests, fix LexiPeek.
- **Engines:** a test checks that LexiPeek's engine submodules are at the same commits as InkPeek's.
- What is ported from InkPeek (Kotlin → C++): `stardict/` (StarDict, Library, Short, Related, Separable), `packs/`
  (manifest, download, install), `mt/` (Engine, Llm, LangId, OfflineTranslation, method choice), the C++ of its
  `*_jni.cpp` without JNI; later `ai/` and `Store`.
- The two apps link to each other in their READMEs.

## Architecture
Laid out for every milestone now; a milestone fills a module, it does not reshape them.

```
mac/ (Swift)       menu bar · card · settings · onboarding · server (M7, 127.0.0.1 + token) · platform parts
qt/  (C++, M4)     the same screens for Windows and Linux · platform parts per OS
core/ (C++, no UI)
  views    what each screen shows (card, packs list, settings state) — shells only draw it
  lookup   Request{text, source: hotkey | input | service | url | extension} → Card{kind, dictionary, translation, ai?}; the one entry for the card,
           the input field, the CLI and later the extension
  dict     Library, StarDict (.syn), header, gloss, separable verbs
  packs    manifest, resumable download, sha256, install, requirements → reasons
  mt       Translator (Bergamot: fast, direct; Llm: Hy-MT2), LangId, ModelHost, method choice
  store    sync log (append, HLC), merge, SQLite view; history, saved, ai answers
  ai       (M5) Provider: HTTP (Anthropic, OpenAI-compatible) and CLI (claude -p, codex exec); modules, prompts
  settings one JSON for the app and the CLI
cli/       lexipeek-cli: lookup, translate, langid, packs, bench, merge
```
- **Offline core, optional AI** (as in InkPeek): `lookup` never depends on `ai`. A card is complete without it; AI
  is a marked tab that asks only when opened, answers go to the store (`ai.put`) and come back offline.
- **Models in memory** (`ModelHost`): one heavy model at a time; idle unload — Hy-MT2 after 2 min, Bergamot after
  5 min (InkPeek's values, tuned by M0 numbers) and at once on the platform's memory-pressure signal. A request
  that needs an unloaded model shows the card at once (dictionary), the translation comes when the model is back.
  `lexipeek-cli bench` measures cold load, first and next translations, unload, reload and peak memory per method;
  numbers go to the spec, a test fails when reload gets slower than its budget.
- **M0 bench** (`lexipeek-cli bench`, de → ru, Release, median of 3 runs; Apple M2 Pro, 32 GB, macOS 15; model files
  in the file cache). "first" is one sentence, "next" a paragraph of three; MB loaded / working: the footprint after
  the load / after both translations; peak: the process's peak resident set.

  | method | load | first | next | unload | reload | first after reload | MB loaded | MB working | peak MB |
  |---|---|---|---|---|---|---|---|---|---|
  | fast (Mozilla de-en + en-ru) | 83 ms | 213 ms | 114 ms | 20 ms | 100 ms | 265 ms | 102 | 425 | 814 |
  | direct (OPUS-MT `deu_eng_fra_por_spa-sla`) | 134 ms | 1110 ms | 1520 ms | 40 ms | 134 ms | 905 ms | 252 | 1163 | 2074 |
  | llm (Hy-MT2 1.8B Q4, Metal) | 474 ms | 269 ms | 871 ms | 27 ms | 149 ms | 249 ms | 200 | 217 | 283 |

  - Reload from the file cache is ≤ 0.15 s for every method, so idle unload costs little: InkPeek's times are kept.
    The reload test's budgets (`tests/models_test.cpp`) are about 7× these numbers.
  - Hy-MT2's 1.1 GB of weights are mapped file pages that the GPU reads: they are not in the footprint or the
    resident set, but they take that memory as file cache. The very first run after a build took ~15 s (Metal
    compiles its shaders; macOS caches them).
  - OPUS-MT (no shortlist, a bigger model) is ~5–10× slower than Mozilla and needs ~1.2 GB while it translates;
    its peak (2 GB) is while loading. Mozilla's peak (0.8 GB) is loading both steps.
  - **Open question answered:** InkPeek's converted OPUS-MT int8 models load and translate correctly on Apple
    Silicon (marian with ruy, `gemm-precision: int8shiftAll`) and on x86-64 (intgemm; CI job `linux-x64` translates
    de → ru with `opus-deu_eng_fra_por_spa-sla` and runs the model tests).
- **Threads:** core calls are synchronous; the app runs them on one worker; a new request cancels the previous one.
- **Data:** `~/Library/Application Support/LexiPeek/` (packs, `settings.json`, `log/`, SQLite); the sync folder
  is a setting (M8). Windows: `%APPDATA%\\LexiPeek`, Linux: XDG dirs.
- **Platform parts** stay in the shells; the core never includes OS headers. Paths: `std::filesystem` with UTF-8
  (`u8path` care on Windows).
- **History from v1:** every card writes `history.seen`; the menu's Recents reads it (M2), Notes shows it (M3).

## Records and the sync log
v1 shows no records, but writes the history in the sync log format from the start. The format (records schema,
per-device JSONL log, merge rules) is InkPeek's [`docs/formats.md`](https://github.com/kafeg/inkpeek/blob/main/docs/formats.md);
its cases are `tests/sync/` there, run here through `tests/inkpeek`. Transport later, in layers: a folder synced by
Syncthing or a cloud → mDNS in the home network → iroh (optional). No servers of ours.

## Card and screens
From the study of other translators (private research): the most common complaints are a pop-up that takes focus,
a window on the wrong screen, and a clipboard overwritten by Cmd+C.
- **Look:** system on every OS — fonts, light / dark, accent; on Linux the current desktop's theme. Settings are a
  plain native window. The card: a non-activating panel with the system's material, like Look Up or Spotlight.
- **Card:** under the selection, on the screen with the cursor, kept inside its edges; it can be dragged anywhere
  (an option keeps a dragged card open, as pinned). By the text: a word — header, dictionary first; a phrase (up to 8
  words, one sentence: InkPeek's `Kind.of`) — translation first, word by word in Dictionary; a passage — its
  translation only. At most 45% of the screen high, the rest scrolls inside.
  Tabs Dictionary · Translate (· AI from M5: Analysis, Context — marked, asks only when opened). Translate shows one method with its name; the others are a switch (a switch
  translates again and is kept for the next cards), unavailable ones dimmed with the reason. A slow method keeps the card's height, no jumps.
  No selection → an input field in the card (it takes focus only on a click).
- **Keys:** ⌥D the selection, ⌥A input (both changeable, conflicts checked), ⌘P pins the card; Esc and a click
  outside close it. ⌥D with a card open: another selection → its card at once; the same one or none → closes.
  In the card: ←/→ switch tabs, Return opens the full entry, ⌘S is ☆.
- **Other ways in** (all go to `lookup`): a macOS Service "Look up in LexiPeek" (context menu of any Cocoa app; the
  system hands over the text — no Accessibility, no clipboard) and a URL scheme `lexipeek://lookup?text=…` (a
  one-line PopClip extension uses it); `lexipeek://input` opens the card with its input field.
- **Icon at the selection** (an option, off by default): after a drag or a double / triple click in another app, the
  selection is read by Accessibility (never by Cmd+C: the clipboard is not touched) and a small icon appears next to
  the cursor; a click on it opens the card; it goes after a few seconds, a click elsewhere or a key.
- **Per-app rules:** apps where the hotkey and the icon do nothing, apps where Cmd+C may read the selection.
- **☆ in the header** (★ when saved, a click or ⌘S toggles): a word is saved as its base form (InkPeek's `saveKey`:
  the pack's headword; "das Haus" → Haus when the pack's header starts so), a phrase or passage as it is, with one
  short line: the dictionary's first line of a word, else the translation shown. ★ also when the text itself was saved.
- **Notes** (menu bar → Notes…; a normal window): Words · Phrases · History with counts, newest first; a row is the
  text and one line (the saved line, else the newest kept answer, else "—"), History also its looks; a click opens
  the card, ✕ removes, "Clear history…" (asks) under History. Rows and texts come from the core (`notes_view`).
- **Menu bar:** Translate selection ⌥D · Type text… ⌥A · Recents (5) · Notes… · language pair · Settings… ⌘, · Quit.
- **Settings:** General (hotkeys, open at login, updates) · Packs (a language pair → its dictionary and a model per
  method with sizes; download with progress, remove) · Translation (answer language, method, "Show the English": Fast's English in between,
  smaller, under the translation, as InkPeek's; "Set Up…" while no pair is installed) · Permissions (status; after a macOS update remove LexiPeek from the list and add it
  again; run from `/Applications`).
- **First run** (a window of its own, at launch while no pair is installed, or Accessibility is off and it was never
  finished; again from Settings → Translation): why Accessibility → open System Settings → continue by itself when
  granted (no restart; "Later" skips it) → pick the first pair (answer language: the system's when the packs offer it,
  else English; the text's language from a list by the packs' levels) and download it with progress → "select a word
  and press ⌥D".

## Milestones (each: spec → code → check → commit, stop)
Done: **M0 Engines** (CMake + vcpkg, engines at InkPeek's pins, `lexipeek-cli`, bench; CI on three OSes) · **M1 Core**
(packs, StarDict, header, gloss, separable verbs, language by the dictionaries, methods with reasons, the log and
SQLite, settings) · **M2 macOS app** (menu bar, ⌥D / ⌥A, the icon at the selection, the card by kind of text,
Service, URL scheme, settings, packs).

Parity with InkPeek first, then the other OS, then a release when the owner says so:
- **M2.1 Debts.** The card's view model in the core (`views`: tabs, the open one, labels, every state's text — the
  shell only draws it); a Swift-friendly facade (plain structs, polled progress; no logic in `mac/Bridge`);
  `Core::configure(settings)` instead of rebuilding the core; offline answers versioned by the models' sha256 (as
  InkPeek); log compaction; `scripts/snapshots.sh` — the app's own snapshots of a fixed set of cards (word, phrase,
  passage, same language, no pack, long list, full entry) and of every settings pane, looked at before each hand-over.
- **M3 Card parity.** ☆ save (a word as its base form), Notes (Words · Phrases · History: open, remove, clear), the
  full entry with "←", its sections (related, synonyms, antonyms, expressions, IPA, origin, forms from `.rel`), "form
  of …", frequency and looks, Mozilla's English in between (a setting), first run: pick a pair and download it.
- **M4 Dictionaries and settings parity.** Own StarDict dictionaries (a folder, InkPeek's catalog of public
  sources, import), dictionary order and on / off; settings in InkPeek's sections (Languages · Card · AI · Notes) in
  desktop form; the UI in the packs' answer languages (en, ru, de, fr, es, it, pt, pl); updates and About.
- **M5 AI.** InkPeek's AI layer: AI translation, **Analysis** (Grammar, Words), **Context** (Explanation,
  Examples); providers as in InkPeek (Claude, OpenAI, OpenRouter) and CLI providers (`claude -p`, `codex exec`);
  editable prompts; answers cached through `ai.put`.
- **M6 Windows app.** Qt Widgets shell, Windows platform parts, installer.
- Then: release (signing, notarization, GitHub Releases, update check) · TSV for Anki, export / import · browser
  extension (whole-page translation through a local server) · sync folder (InkPeek too) · Linux (the Qt shell).

## License
MIT. Packs keep their own licenses (Wiktionary CC BY-SA, Mozilla MPL-2.0, OPUS-MT CC BY 4.0, Hy-MT2 Apache-2.0).
