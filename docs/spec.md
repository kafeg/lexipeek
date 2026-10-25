# LexiPeek — spec

A pop-up dictionary and offline translator for the desktop, macOS first, then Windows and Linux. Select text in any app, press a hotkey: a card opens next to
the cursor with the dictionary entry and a translation made on this Mac. Close it and you are back where you were.
LexiPeek is the desktop sibling of [InkPeek](https://github.com/kafeg/inkpeek) (Android e-ink readers) and uses its
language packs.

## Goals (v1)
- One card over any app, opened by a global hotkey on the selected text. The card does not take focus.
- Dictionary first: the header with grammar from the packs ("das Haus (Hauses, pl. Häuser)"), one short line per
  dictionary, the full entry on request, the base form of an inflected word (*Träumen* → *Traum*). A sentence gets a
  word-by-word gloss, separable verbs included.
- Offline translation of a phrase or a paragraph, three methods as in InkPeek: Fast · Mozilla (through English),
  Direct · OPUS-MT, AI model · Hy-MT2 (llama.cpp, Metal). The text's language from fastText. A method this Mac
  cannot run is shown dimmed with the reason.
- Everything comes from the app's own downloads: InkPeek's `packs` release, checked by sha256.

## Non-goals (v1)
AI tab, saved words and history in the UI, browser extension, whole-page translation, Linux, sync
(Windows comes right after v1, M4).
They are planned — see Milestones.

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
| `core/` | C++20, CMake, no UI and no OS headers. Libraries (vcpkg manifest, the same on every OS): sqlite3, nlohmann/json, libcurl, zstd, **utf8proc** (NFC, case folding — strings are UTF-8 `std::string` everywhere), doctest. Regex — PCRE2 (never `std::regex`); formatting — `std::format`. Built and tested on its own (`lexipeek-cli`) |
| `core/include/lexipeek/` | the facade the shells call: plain structs, enums, `std::string`, `std::vector`, `std::optional`, errors as values — importable by Swift's C++ interop and used as is by C++ shells. No C API |
| engines | git submodules, the same pinned commits as InkPeek: Bergamot (RTranslator fork), PCRE2, fastText, llama.cpp (Metal on macOS) |
| `mac/` | Swift, SwiftUI + AppKit, as panerail: XcodeGen `project.yml`, `make build/test/install`. Calls the core through Swift's C++ interop. Menu bar (`LSUIElement`), non-activating `NSPanel` with a system material, hotkey (Carbon `RegisterEventHotKey`), selected text (AX; Cmd+C with the clipboard restored only where allowed), Service, URL scheme, Accessibility, memory pressure |
| `qt/` | (M4) Windows and Linux: Qt 6 Widgets in C++ — the core is called directly; Windows 11 style, on Linux the desktop's theme. Platform parts per OS (Windows: `RegisterHotKey`, UI Automation; Linux: X11 / portals) |
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
- **History from v1:** every card writes `history.seen`; the menu's Recents reads it (M2), the records UI is M6.

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
- **Card:** under the selection, on the screen with the cursor, kept inside its edges; no remembered coordinates.
  Tabs Dictionary · Translate (· AI from M5: Analysis, Context — marked, asks only when opened). Translate shows one method with its name; the others are a switch (a switch
  translates again), unavailable ones dimmed with the reason. A slow method keeps the card's height, no jumps.
  No selection → an input field in the card (it takes focus only on a click).
- **Keys:** ⌥D the selection, ⌥A input (both changeable, conflicts checked), ⌘P pins the card; Esc, a click
  outside, the hotkey again close it.
  In the card: ←/→ switch tabs, Return opens the full entry.
- **Other ways in** (all go to `lookup`): a macOS Service "Look up in LexiPeek" (context menu of any Cocoa app; the
  system hands over the text — no Accessibility, no clipboard) and a URL scheme `lexipeek://lookup?text=…` (a
  one-line PopClip extension uses it).
- **Per-app rules:** apps where the hotkey does nothing, apps where Cmd+C is allowed (off by default).
- **Menu bar:** Translate selection ⌥D · Type text… ⌥A · Recents (5) · language pair · Settings… ⌘, · Quit.
- **Settings:** General (hotkeys, open at login, updates) · Packs (a language pair → its dictionary and a model per
  method with sizes; download with progress, remove) · Translation (method) · Permissions (status; after a macOS update remove LexiPeek from the list and add it
  again; run from `/Applications`).
- **First run:** why Accessibility → open System Settings → continue by itself when granted (no restart) → pick the
  first pair and download it → "select a word and press ⌥D".
- Not done: an icon at the selection, a global event tap, Cmd+C by default, all methods at once.

## Milestones (each: spec → code → check → commit, stop)
v1:
- **M0 Engines.** CMake + vcpkg, submodules, Bergamot, fastText, llama.cpp; `lexipeek-cli translate` and `bench` on
  InkPeek's pack models. CI builds the core and runs its tests on macOS, Windows and Linux. Check: InkPeek's
  converted OPUS-MT int8 models load on Apple Silicon and x86-64; load, reload, speed and memory of the three methods.
- **M1 Core.** Packs (manifest, resumable download, sha256, install), StarDict with `.syn`, header, word-by-word
  gloss, separable verbs, language id, method choice with reasons, the log and SQLite, `views`. Cross-tests green
  on all three OSes.
- **M2 macOS app.** Swift shell: menu bar, hotkey, selected text, non-activating card at the cursor (Dictionary,
  Translate), input field, Service and URL scheme, per-app rules, Recents, settings, Accessibility onboarding.
  Check the selection in Safari, Chrome, Firefox, Vivaldi and native apps. A build for daily use.
- **M3 Release 0.1 (macOS).** Signing, notarization, GitHub Releases, update check.
- **M4 Windows app.** Qt Widgets shell, Windows platform parts, installer; release 0.2. The second shell right after
  the first, to find what the core and its facade got wrong while it is cheap to change.

Then, in this order: M5 AI — the extra value, as in InkPeek: the AI tab with **Analysis** (Grammar, Words) and
**Context** (Explanation, Examples), AI translation; modules and parts with editable prompts; API providers as in
InkPeek and CLI providers (`claude -p`, `codex exec`); answers cached through `ai.put` · M6 records in the UI (☆,
history, TSV for Anki, export / import) · M7 browser extension for Chromium and Firefox: whole-page translation
through the app's local server; selection in the browser only if M2 shows the system hotkey misses it there ·
M8 sync folder (InkPeek too) · M9 Linux (the Qt shell; X11, Wayland through portals where they allow).

## License
MIT. Packs keep their own licenses (Wiktionary CC BY-SA, Mozilla MPL-2.0, OPUS-MT CC BY 4.0, Hy-MT2 Apache-2.0).
