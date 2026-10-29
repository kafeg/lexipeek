# TODO

Done on macOS: M0–M5 (engines, core, the app, card parity, dictionaries and settings, AI), the LexiPeek window, Notes
with TSV / Export / Import. Now: M6 Windows (Qt). Milestones: `docs/spec.md` → Milestones.

## M8 Native Windows shell (`win/`, WinUI 3), by phases
Each phase builds, runs on Windows (`--self-test`, `--snapshots`, `scripts/win-e2e.ps1`) and is committed.
1. Done: the process (one instance, `lexipeek://`, install / uninstall, `--data`, `--self-test`, `--probe-selection`,
   `--snapshots`), the OS parts shared with `qt/` (`platform/`), the tray icon and its menu (`MenuFlyout`), hotkeys and the
   selection read; the card a stand-in, the other windows placeholders saying their phase. `scripts\build-win.cmd` builds
   it by default (`qt`: the Qt shell).
2. The card: the pop-up (borderless, no focus, at the selection, Mica/acrylic, its keys), the card to type in, the icon at
   the selection (`watch_input`); `CardSession` polled; the snapshot cases of `scripts/snapshot-cases.sh`.
3. The LexiPeek window: Look up (an embedded card), Words · Phrases · History (`notes_view`), Help; the no-pack banner.
4. Done: Settings as Windows 11's (sections in a `NavigationView`, sub-tabs in a `SelectorBar`, the core's rows as setting
   cards, Packs, Other dictionaries and the catalog, AI with the key in the Credential Manager, hotkeys recorded in a
   dialog, the prompt editor and "Choose…" as `ContentDialog`s) and About; closing hides them (downloads go on). Open:
   Read aloud's section once the voice model runs on Windows; an own dictionary's `.dict` stays on disk when it is deleted
   while the engine has it open (as in `qt/`).
5. The first run; every snapshot case; then CI and releases switch to it (below) and `qt/` is Linux only.
- CI, when phase 5 lands: ci.yml's Windows job configures with `-DLEXIPEEK_QT=OFF`, builds and tests the core as now,
  then `scripts\build-win.cmd build-win` for the shell (Ninja, its own dir; one `build-win.cmd build-qt qt` job while the
  fallback stays); release.yml zips `build-win\win\LexiPeek\` as `LexiPeek-Windows.zip` instead of copying
  `build/qt/Release/LexiPeek.exe`; then `LEXIPEEK_QT` is off by default on Windows too (CMakeLists.txt) and README's
  Windows lines change.
- Open: the tray menu on Windows 11 and by a real click on the icon (tried on Windows 10 with Explorer's messages to the
  icon's window: items, invoked items, light dismiss, Quit; the icon is in the taskbar); WinUI windows on a VM's
  basic display adapter come out blank in screen captures (`--snapshots` renders them by XAML instead); the host window is
  a pixel at the click (a `MenuFlyout` needs a XAML root).

## M9 Read aloud (spec M9)
Done on macOS: the engine (qwentts.cpp as `libqwen.dylib`), the model's download, the voices in the app, voices from a
recording, ▶ in the card by context (⌘.), Settings → Read aloud, kept readings, snapshots.
- Windows, after M8's card (`win/card*`) is committed: `LEXIPEEK_TTS` on for Windows — `qwen.dll` beside the exe, linked
  through `win/lexipeek-core.props` (link_closure skips shared libraries now: add its import library) and delay-loaded,
  its Vulkan delay-loaded as llama's (`cmake/vulkan_win.cpp`: no driver, the CPU); the card's ▶ / ■ and Ctrl+. in
  `win/card*`, the WAV played (`PlaySound` with `SND_ASYNC`, or WinRT `MediaPlayer`), `CardSession::take_play/played` as
  the Mac's `CardModel`; Settings → Read aloud (the core's rows) with the WAV picker; release.yml ships `qwen.dll`.
- Measure on Windows (Vulkan, a real GPU and the CPU): load, a word, a sentence.
- Polish: the model does not name it (read with "auto"); check its words by native speakers.

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
- CI: Linux, its ASan and macOS green; Windows builds the core and the Qt shell (no models there).
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
- Open: Direct on long passages (more threads or a worker pool).

## Kept as InkPeek, by decision
- Unknown text ("+++ 42 ### 17", made-up words) gets fastText's best book language when no dictionary knows a word:
  detecting "not a language" would misfire on real text.
- A separable verb's finite form is listed again on its own ("stand … auf" and "stand: stehen"), as InkPeek's gloss.
- dict-de-ru's headword "ist" ("ист") wins over the form of *sein* in the gloss: InkPeek's data, fix it there.
