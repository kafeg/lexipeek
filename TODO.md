# TODO

Done on macOS: M0–M5 (engines, core, the app, card parity, dictionaries and settings, AI), the LexiPeek window, Notes
with TSV / Export / Import. Now: M6 Windows (Qt). Milestones: `docs/spec.md` → Milestones.

## M6 Windows (Qt shell, `qt/`)
Drawn so far (snapshots on macOS): the card, the pop-up, the tray menu, the LexiPeek window (Look up, Notes, Help),
Settings (every pane), About, the first run. Windows parts written, not yet tried on Windows: hotkeys, the selection by
UI Automation, the Credential Manager, the Run key, rounded corners.
- Build and run on Windows; check every window there (Windows 11 style, dark mode), the hotkeys and the selection
  in browsers, Office, Explorer, terminals; DPI scaling on two screens.
- Defaults on Windows: Alt+D (lookup) is the browsers' address bar — pick other keys for Windows.
- The icon at the selection (a low-level mouse hook); `lexipeek://` links; the app's .ico and version resource.
- Installer (later in M6): per-user, Start menu, uninstall; signed when there is a certificate.
- llama.cpp with Vulkan (NVIDIA, AMD, Intel GPUs in one build), CPU when no GPU: with a GPU the AI model's CPU needs
  (`cpu_by_arch`) do not apply; the method's card says where it runs.
- The card's keys in its texts (`card_pin`, `card_back`, `hc_keys`): Windows has its own (`@win`); Linux will need its own.

## Open
- CI on Linux / Windows not run since M0 (macOS first); the CLI providers' shell line (and its 2-minute stop) is
  POSIX only.
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
