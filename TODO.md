# TODO

Done on macOS: M0–M5 (engines, core, the app, card parity, dictionaries and settings, AI), the LexiPeek window, Notes
with TSV / Export / Import. Now: M6 Windows (Qt). Milestones: `docs/spec.md` → Milestones.

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
   VM: acrylic falls back to its colour); the card's ▶ once Read aloud runs on Windows; an own dictionary in a junction
   (`mklink /J`) is not found by the core.
3. Done: the LexiPeek window (`MainWindow.xaml*`: a `NavigationView` with the memory line and Settings · About at its
   foot; Look up — a query box, Return / Shift+Return, the embedded card; Words · Phrases · History — `notes_view` in a
   `ListView` with the counts in the nav, ✕ / Delete, TSV, the pager, Clear History, backup and restore; Help — `help_view`
   with its "Open …" links; the no-pack `InfoBar`, F1; the card and query kept in the shell's `MainKeep`, its work stopped
   on close); `window-*` / `notes-*` snapshots; win-e2e.ps1 drives it (a look-up, History, TSV, F1, close).
4. Done: Settings as Windows 11's (sections in a `NavigationView`, sub-tabs in a `SelectorBar`, the core's rows as setting
   cards, Packs, Other dictionaries and the catalog, AI with the key in the Credential Manager, hotkeys recorded in a
   dialog, the prompt editor and "Choose…" as `ContentDialog`s) and About; closing hides them (downloads go on). Open:
   Read aloud's section once the voice model runs on Windows; an own dictionary's `.dict` stays on disk when it is deleted
   while the engine has it open (as in `qt/`).
5. Done: the first run (`first_run`: qt/first_run's steps as Windows' setup pages, its `firstrun-*` snapshots); CI's Windows
   job builds the core by Ninja with `-DLEXIPEEK_QT=OFF`, then the WinUI shell (`build-win.cmd build`, the folder as an
   artifact; the Qt shell only in a run by hand); release.yml ships `LexiPeek-Windows.zip` (the folder), release.sh checks
   it. Open: THIRD_PARTY_NOTICES.txt still names Qt for the Windows app and not the Windows App SDK (MIT) and C++/WinRT
   it ships now.
- Open: the tray menu on Windows 11 and by a real click on the icon (tried on Windows 10 with Explorer's messages to the
  icon's window: items, invoked items, light dismiss, Quit; the icon is in the taskbar); WinUI windows on a VM's
  basic display adapter come out blank in screen captures (`--snapshots` renders them by XAML instead); the host window is
  a pixel at the click (a `MenuFlyout` needs a XAML root).

## M9 Read aloud (spec M9)
Done on macOS: the engine (qwentts.cpp as `libqwen.dylib`), the model's download, the voices in the app, voices from a
recording, ▶ in the card by context (⌘.), Settings → Read aloud, kept readings, snapshots.
- Windows (built, not yet run): `qwen.dll` beside each program (its Vulkan delay-loaded with the core's hook), the card's
  ▶ / ■ and Ctrl+., `PlaySound` and a timer by the WAV's length, Settings → Read aloud with the WAV picker. Run it on
  Windows: the download, a reading, Ctrl+., a voice from a recording, `--snapshots`. "On the GPU" off does nothing there yet
  (`GGML_BACKEND` is read by qwen.dll's own C runtime, which does not see the exe's `_putenv`).
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
