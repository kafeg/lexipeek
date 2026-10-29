# TODO

Done on macOS: M0–M5 (engines, core, the app, card parity, dictionaries and settings, AI), the LexiPeek window, Notes
with TSV / Export / Import. Now: M6 Windows (Qt). Milestones: `docs/spec.md` → Milestones.

## M6 Windows (Qt shell, `qt/`)
Done and tried on Windows 10 in a signed-in session: every window in the Fluent look (light, dark), the hotkeys
(Win+Shift+D / Win+Shift+A), the selection (Notepad, WordPad, Edge, Chrome, Firefox), the icon at the selection, `lexipeek://`
links, one instance, the Credential Manager, the Run key, CLI providers, the tray's glyph, the .ico and version, the
per-user install and uninstall.
- Office, Explorer, terminals; DPI scaling on two screens; Windows 11.
- First release (the owner's): a release job (tag → the Mac app signed with Developer ID, notarized, zipped; the
  Windows exe, signed when there is a certificate; THIRD_PARTY_NOTICES.txt) and GitHub Releases.
- llama.cpp with Vulkan (NVIDIA, AMD, Intel GPUs in one build), CPU when no GPU: with a GPU the AI model's CPU needs
  (`cpu_by_arch`) do not apply; the method's card says where it runs.
- The card's keys in its texts (`card_pin`, `card_back`, `hc_keys`): Windows has its own (`@win`); Linux will need its own.
- Into the core, once for both shells (now in Swift and in Qt): the hotkey's decision
  (ignored app, the same selection closes, nothing read → typing), the tray / menu bar menu's items, the settings
  writes done by hand (method picked in the card, first run done, ignored apps), the snapshot driver, light Markdown.
- Qt: two installs at once into one data dir (Packs, Settings → method, first run guard only themselves); downloads
  share the AI pool.
- From the review: the path rule (`path_of`) in stardict, store, translator, dicts (a Cyrillic data path works on
  Windows through the UTF-8 manifest); packs.json parsed once per look-up; the answer language default ("ru").
- Work still running at quit (detached queue and AI threads); OPUS-MT counted in a pair's size where the machine
  cannot run it.

## Open
- CI: Linux, its ASan and macOS green; Windows builds the core and the Qt shell (no models there).
- No request sent to a real AI provider from here: try each once.
- Own dictionaries are indexed in the background one at a time (a card waits for one index at most); no
  "indexing…" state in their rows.
- `Library::all()` rescans the folders and `translate()` re-reads packs.json on every call (~1 ms now; grows with
  folders).
- Check the selection and ⌥D's position (AX bounds) in Safari, Chrome, Firefox, Vivaldi and native apps.
- OPUS-MT on an 8 GB Mac: its working set (1.2 GB) and peak (2 GB) — check `mini-batch-words`.
- Translations of the UI not reviewed by native speakers; low-level errors (libcurl, libarchive) stay English.

## Kept as InkPeek, by decision
- Unknown text ("+++ 42 ### 17", made-up words) gets fastText's best book language when no dictionary knows a word:
  detecting "not a language" would misfire on real text.
- A separable verb's finite form is listed again on its own ("stand … auf" and "stand: stehen"), as InkPeek's gloss.
- dict-de-ru's headword "ist" ("ист") wins over the form of *sein* in the gloss: InkPeek's data, fix it there.
