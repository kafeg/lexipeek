# TODO

Done on macOS: M0–M5 (engines, core, the app, card parity, dictionaries and settings, AI), the LexiPeek window, Notes,
sync with InkPeek and Anki (Settings → Sync, with Export / Import); Windows (M8); Linux x64 (M10, released). Milestones: `docs/spec.md` → Milestones.

## From the architecture review (2026-11-20)
Critical:
- `index.ipk` is read without bounds: `load()` takes `count_` from the file and `key_at`/`entry_at` `memcpy` by its
  offsets (`core/src/dict/stardict.cpp:49, 311, 318`); `build()` renames `index.tmp` without checking the `ofstream`
  (:302). Check `size >= 8 + 4*count_` and the offsets, and the write.
- `StarDict::stamp()` (:224) has no index format version: the next `MAGIC` bump leaves installed copies with a matching
  stamp, so `load()` throws forever and `Library::lookup` turns it into "nothing found" (`dict/library.cpp:200`). Put
  `MAGIC` in the stamp; rebuild once on a magic mismatch.
- The packs release is a live dependency with no format or minimum-app field (`packs.hpp:14`, `:57`): a component for a
  newer engine would be downloaded and fail to load. Needs a field in InkPeek's `packs.json` and a `cannot()` reason.

Important:
- No log: ~half of the core's 38 catches are silent, and a look-up that throws just restores the card instead of
  showing `Card::error` (`views/session.cpp:123, 159`). One ring log `<data>/lexipeek.log` + "Show log" in General.
- Into the core, once for three shells: the launch (`name_device` → `sync(true)` → `prepare` → the 30 s timer →
  `anki_start`, `configure`/`reload`: `qt/core_client.cpp`, `win/core_client.cpp`, `mac/CoreClient.swift`,
  `qt/main.cpp` ≈ `win/App.xaml.cpp`) as an `app::AppSession` like `CardSession`; the download job
  (busy/progress/error/finished in `qt/packs_pane.cpp:127`, `win/packs_pane.cpp:93`, `mac/PacksView.swift:31`, and the
  same in `dicts_pane` and `first_run`) as an `app::Job`.
- The shown settings sections are hardcoded per shell (`qt/settings_window.hpp:78`, `win/settings_window.hpp:31`,
  `mac/SettingsView.swift:14`) and already differ: Qt has no `speech`, while Windows' Qt build links the TTS dll and the
  core's card offers "Open Settings → Read aloud" (`views/read.cpp:64`). The list belongs in the core, gated by
  `speech::built()`.
- A setting row's button is dispatched by `if (r.key == "…")` with no default in each shell, so a new key is a dead
  control nothing catches (`speech_add`/`speech_remove` are missing in Qt today). An action enum in the core, or a test
  that matches the keys the core emits against the shells.
- CI never compiles the Mac app: the macOS job runs `make app-test` and the scheme builds only `LexiPeekKitTests`
  (`ci.yml:130`, `mac/project.yml:103`). Add `make app SIGN=-`.
- `linux-asan` and `windows-qt` only run on `workflow_dispatch` (`ci.yml:178, 208`): give ASan a nightly schedule.
- The first `notes()` blocks the UI thread until the store's opener finishes (`store/store.cpp:210`, called from
  `qt/main_window.cpp:191`, `win/MainWindow.xaml.cpp:197`, `mac/NotesPane.swift:64`): a `Store::ready()` state, or a
  queue.
- One detached `std::thread` per AI ask, up to three a card, no pool (`views/session.cpp:222`); `set_cli_runner` is an
  unsynchronised process global (`ai/transport.cpp:22, 67`).
- The core cannot be built without the MT engines (`core/CMakeLists.txt:6`): no `LEXIPEEK_MT` next to `LEXIPEEK_TTS`
  with its `built()` stub. Needed for a core without models (iOS).
- `CardSession` — the staleness counters all three shells depend on — has three linear tests
  (`tests/session_test.cpp:74, 150, 205`): add races (a new look-up while translating, "←" while a section arrives,
  `cancel()` during AI).
- `installed.json` is a read-modify-write with no lock (`packs/packs.cpp:352, 433`), so two installs at once lose a
  mark; `Packs` only claims one at a time (`packs.hpp:115`).

Small:
- Raw English in the UI, around `tr()`: `c.error = e.what()` (`lookup/lookup.cpp:343`, shown by `views.cpp:387`) and
  the packs errors (`packs.cpp:363, 378`). `l10n_test` greps Swift for English literals, not the core.
- `lexipeek-cli` sets no `cli_runner` (the shells do: `qt/main.cpp:103`, `win/App.xaml.cpp:113`), so a CLI AI provider
  on Windows gets the core's POSIX line through `_popen` (`ai/transport.cpp:37`).
- The snapshot error fixtures are duplicated as literals (`qt/snapshots.cpp:145`, `win/snapshots.cpp:152`) while the
  cases come from `scripts/snapshot-cases.sh`.
- `vcpkg.json`'s `builtin-baseline` and the `vcpkg` submodule commit are not checked against each other: put it in
  `tests/pins.cmake`.
- `settings.json` is written tmp+rename without fsync (`settings/settings.cpp:210`): a power loss can leave it empty
  and the settings back at the defaults.
- `Data::read` allocates by the uint32 size from a `.idx` (`dict/stardict.cpp:170`): cap it by the `.dict`'s size.
- `docs/spec.md:61` promises clang-tidy; it is configured nowhere.
- `views.hpp:227`'s comment on `markdown_spans` ("as the Qt and Mac cards draw it") makes three parsers the norm; see
  the light Markdown line under M6.

## M10 Linux (spec M10)
Done: the OS parts, the Qt shell on Linux, `LexiPeek-Linux-x64.tar.gz` in the release (release.yml calls linux.yml;
release.sh checks it), notices, docs; run on an Ubuntu 24.04 X11 desktop (hotkeys, PRIMARY in an editor and Chromium,
first run, tray, Notes, sync, `lexipeek://`).
- Not run yet: Wayland (the GlobalShortcuts portal), the tray by StatusNotifierItem (only XEmbed was).
- Open: Read aloud (qwentts builds on Linux; the Qt shell has no player); an AppImage; the icon at the selection (XInput2).

## M8 Native Windows shell (`win/`, WinUI 3), by phases
Each phase builds, runs on Windows (`--self-test`, `--snapshots`, `scripts/win-e2e.ps1`) and is committed.
1. Done: the process (one instance, `lexipeek://`, install / uninstall, `--data`, `--self-test`, `--probe-selection`,
   `--snapshots`), the OS parts shared with `qt/` (`platform/`), the tray icon and its menu (`MenuFlyout`), hotkeys and the
   selection read; the card a stand-in, the other windows placeholders saying their phase. `scripts\build-win.cmd` builds
   it by default (`qt`: the Qt shell).
2. Done: the card (`card`: every row style, header cut and whole, tabs, links, sections and pagers, the bottom row with its
   menus, copy, ☆, the pin, fix buttons to Settings' panes, the field with recents), the pop-up (`popup`: borderless, no
   focus unless typing, at the selection inside the screen, its height kept while loading, acrylic, Esc / Ctrl+P while
   open, ←/→ Ctrl+S Backspace, outside click, drag and pin), the icon at the selection; `--snapshots --cases` draws the card
   cases (not `noaccess`: the Mac's Accessibility). Open: Mica / acrylic, corners and shadow seen only on Windows 10 (a
   VM: acrylic falls back to its colour); the card's ▶ once Read aloud runs on Windows.
3. Done: the LexiPeek window (`MainWindow.xaml*`: a `NavigationView` with the memory line and Settings · About at its
   foot; Look up — a query box, Return / Shift+Return, the embedded card; Words · Phrases · History — `notes_view` in a
   `ListView` with the counts in the nav, ✕ / Delete, the pager, Clear History; Help — `help_view`
   with its "Open …" links; the no-pack `InfoBar`, F1; the card and query kept in the shell's `MainKeep`, its work stopped
   on close); `window-*` / `notes-*` snapshots; win-e2e.ps1 drives it (a look-up, History, F1, close).
4. Done: Settings as Windows 11's (sections in a `NavigationView`, sub-tabs in a `SelectorBar`, the core's rows as setting
   cards, Packs, Other dictionaries and the catalog, AI with the key in the Credential Manager, hotkeys recorded in a
   dialog, the prompt editor and "Choose…" as `ContentDialog`s) and About; closing hides them (downloads go on). Open:
   Read aloud's section once the voice model runs on Windows.
5. Done: the first run (`first_run`: qt/first_run's steps as Windows' setup pages, its `firstrun-*` snapshots); CI's Windows
   job builds the core by Ninja with `-DLEXIPEEK_QT=OFF`, then the WinUI shell (`build-win.cmd build`, the folder as an
   artifact; the Qt shell only in a run by hand); release.yml ships `LexiPeek-Windows.zip` (the folder), release.sh checks
   it; `LEXIPEEK_QT` is off by default on Windows too.
- Open: the tray menu on Windows 11 and by a real click on the icon (tried on Windows 10 with Explorer's messages to the
  icon's window: items, invoked items, light dismiss, Quit; the icon is in the taskbar); WinUI windows on a VM's
  basic display adapter come out blank in screen captures (`--snapshots` renders them by XAML instead); the host window is
  a pixel at the click (a `MenuFlyout` needs a XAML root).

## M9 Read aloud (spec M9)
Done on macOS: the engine (qwentts.cpp as `libqwen.dylib`), the model's download, the voices in the app, voices from a
recording, ▶ in the card by context (⌘.), Settings → Read aloud, kept readings, snapshots.
- Windows: tried on a VM without a GPU or a sound device (the CPU; `scripts/win-read-e2e.ps1` all ok, `--snapshots`
  read-* / study-* / settings-read looked at; a voice from a recording by the CLI). Not yet: the sound itself (PlaySound on
  a machine with a sound device), the WAV picker's dialog, Vulkan on a real GPU. "On the GPU" off does nothing there yet
  (`GGML_BACKEND` is read by qwen.dll's own C runtime, which does not see the exe's `_putenv`).
- Measure on Windows (Vulkan, a real GPU and the CPU): load, a word, a sentence.
- Polish: the model does not name it (read with "auto"); check its words by native speakers.
- A pack that writes its marker without brackets keeps it in the reading ("идти irreg."): only the brackets are cut
  (`spoken` in `core/src/views/read.cpp`). Dropping a trailing abbreviation needs care — a headword may be one itself
  ("z. B.") — and it changes what ☆ keeps and the kept WAVs, so it is its own change.

## M6 Windows (Qt shell, `qt/`)
Done and tried on Windows 10 in a signed-in session: every window in the Fluent look (light, dark), the hotkeys
(Win+Shift+D / Win+Shift+A), the selection (Notepad, WordPad, Edge, Chrome, Firefox), the icon at the selection, `lexipeek://`
links, one instance, the Credential Manager, the Run key, CLI providers, the tray's glyph, the .ico and version, the
per-user install and uninstall.
- Office, Explorer, terminals; DPI scaling on two screens; Windows 11.
- First release (the owner's): a release job (tag → the Mac app signed with Developer ID, notarized, zipped; the
  Windows exe, signed when there is a certificate; THIRD_PARTY_NOTICES.txt) and GitHub Releases.
- The card's keys in its texts (`card_pin`, `card_back`, `hc_keys`): Windows has its own (`@win`); Linux will need its own.
- Into the core, once for both shells (now in Swift and in Qt): the hotkey's decision
  (ignored app, the same selection closes, nothing read → typing), the tray / menu bar menu's items, the settings
  writes done by hand (method picked in the card, first run done, ignored apps), the snapshot driver, light Markdown.
- Qt: two installs at once into one data dir (Packs, Settings → method, first run guard only themselves); downloads
  share the AI pool.
- The path rule (`path_of`) in stardict, store, translator, dicts: a Cyrillic data path works on Windows (tried:
  every engine) through the UTF-8 manifest; the code still has `.string()` paths. The answer language default ("ru").
- Vulkan tried on a software device (Mesa lavapipe, forced by `GGML_VK_VISIBLE_DEVICES`): a real GPU (NVIDIA, AMD,
  Intel) not yet.
- Settings saved whole by the shells (`app::save`) keep the core's own writes (AI's recall, catalog downloads), but a
  field the shell does not know is still overwritten: shells could write only their own fields.

## Open
- Hy-MT2 tests run only locally (1.1 GB; the cache's 10 GB is shared with vcpkg's).
- No request sent to a real AI provider from here: try each once.
- Own dictionaries are indexed in the background one at a time (a card waits for one index at most); no
  "indexing…" state in their rows.
- `Library::all()` rescans the folders once per look-up (~1 ms now; grows with folders).
- Check the selection and ⌥D's position (AX bounds) in Safari, Chrome, Firefox, Vivaldi and native apps.
- OPUS-MT on an 8 GB Mac: its working set (1.2 GB) and peak (2 GB) — check `mini-batch-words`.
- Translations of the UI not reviewed by native speakers; low-level errors (libcurl, libarchive) stay English.

## Speed (macOS, M2 Pro; `lexipeek-cli lookup --time`, `bench --text`)
- Look-up, warm: word 7 ms, phrase 20 ms, 100-word passage ~200 ms (a third of it the separable verbs' `.rel`
  searches). A process's first look-up 0.5–0.9 s (indexes read whole: the app does it at launch, `prepare()`).
- 100 words, model loaded: Fast 0.24 s, Direct 6.0 s (one Marian thread: the slow one), AI model 2.6 s. Loading:
  Fast 0.1 s, Direct 0.13 s, Hy-MT2 0.2–0.7 s.
- Memory, MB (loaded / translating / peak RSS): Fast 102 / 428 / 872, Direct 252 / 1161 / 2326, Hy-MT2 199 / 223 / 282
  (+ its Metal buffers).
- AI by `claude -p` (quick, a word): ~9 s; again from the store.
- Direct side by side (two copies of the model, a piece each) was ~2× faster on long texts for ~1.1 GB more, but not
  word for word the same as one copy on Linux CI (one clause of a long sentence differed at random): removed, one copy.
  Likely why: vcpkg's OpenBLAS is built single-threaded without `USE_LOCKING`, which OpenBLAS needs for calls from
  several threads at once (its buffers are shared); the Mac's Accelerate is not affected.

## Kept as InkPeek, by decision
- Unknown text ("+++ 42 ### 17", made-up words) gets fastText's best book language when no dictionary knows a word:
  detecting "not a language" would misfire on real text.
- A separable verb's finite form is listed again on its own ("stand … auf" and "stand: stehen"), as InkPeek's gloss.
- dict-de-ru's headword "ist" ("ист") wins over the form of *sein* in the gloss: InkPeek's data, fix it there.
