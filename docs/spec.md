# LexiPeek — spec

A pop-up dictionary and offline translator for macOS. Select text in any app, press a hotkey: a card opens next to
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
AI tab, saved words and history in the UI, browser extension, whole-page translation, Windows, Linux, sync.
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
| part | what |
|---|---|
| `core/` | C++20, CMake, **no Qt**: std, sqlite3, nlohmann/json, libcurl, zstd. Built and tested on its own (`lexipeek-cli`). The UI layer can be replaced without touching it |
| engines | git submodules, the same pinned commits as InkPeek: Bergamot (RTranslator fork), PCRE2, fastText, llama.cpp |
| `app/` | Qt 6 Widgets (LGPL, dynamic linking): native look on every OS and Linux desktop (macOS style, Windows 11 style, the KDE / GTK platform theme); tray via `QSystemTrayIcon` |
| `app/platform/mac/` | Objective-C++: hotkey (Carbon `RegisterEventHotKey`), selected text (AX `kAXSelectedTextAttribute`; Cmd+C with the clipboard restored only when turned on), non-activating `NSPanel` with a system material, Accessibility prompt, memory-pressure signal |
| build | CMake + `make build/test/install`; GitHub Actions; `.app` signed with Developer ID, notarized, on GitHub Releases |

Why Widgets, not QML: settings must look native on every OS and on the current Linux desktop; Qt Quick has native
styles only for macOS and Windows. Not used: Qt HTTP Server (GPL-3; the extension later uses cpp-httplib, MIT), Electron, Tauri, JVM.

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
app/ (Qt Widgets)  tray · card · settings · onboarding · server/ (M6, 127.0.0.1 + token)
  └ platform/ mac · win · linux: hotkey, selected text, panel, permission, memory pressure
core/ (C++, no Qt)
  lookup   Request{text, source: hotkey | input | service | url | extension} → Card{kind, dictionary, translation, ai?}; the one entry for the card,
           the input field, the CLI and later the extension
  dict     Library, StarDict (.syn), header, gloss, separable verbs
  packs    manifest, resumable download, sha256, install, requirements → reasons
  mt       Translator (Bergamot: fast, direct; Llm: Hy-MT2), LangId, ModelHost, method choice
  store    sync log (append, HLC), merge, SQLite view; history, saved, ai answers
  ai       (M4) Provider: HTTP (Anthropic, OpenAI-compatible) and CLI (claude -p, codex exec); modules, prompts
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
- **Threads:** core calls are synchronous; the app runs them on one worker; a new request cancels the previous one.
- **Data:** `~/Library/Application Support/LexiPeek/` (packs, `settings.json`, `log/`, SQLite); the sync folder
  is a setting (M9).
- **Platform layer** is an interface; macOS first, Windows and Linux add implementations, not branches in shared code.
- **History from v1:** every card writes `history.seen`; the menu's Recents reads it (M2), the records UI is M5.

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
  Tabs Dictionary · Translate (· AI from M4: Analysis, Context — marked, asks only when opened). Translate shows one method with its name; the others are a switch (a switch
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
- **M0 Engines.** CMake, submodules, Bergamot, fastText, llama.cpp (Metal) for macOS arm64; `lexipeek-cli translate`
  on InkPeek's pack models; `lexipeek-cli bench`. Check: InkPeek's converted OPUS-MT int8 models load on Apple
  Silicon; load, reload, speed and memory of the three methods.
- **M1 Core.** Packs (manifest, resumable download, sha256, install), StarDict with `.syn`, header, word-by-word
  gloss, separable verbs, language id, method choice with reasons, the log and SQLite. Cross-tests green.
- **M2 macOS shell.** Mockups first. Menu bar, hotkey, selected text, non-activating card at the cursor
  (Dictionary, Translate), input field, Service and URL scheme, per-app rules, Recents, settings, Accessibility onboarding. Check the selection in Safari,
  Chrome, Firefox, Vivaldi and native apps. A build for daily use.
- **M3 Release 0.1.** Signing, notarization, GitHub Releases, update check.

Later, in this order: M4 AI — the extra value, as in InkPeek: the AI tab with **Analysis** (Grammar, Words)
and **Context** (Explanation, Examples), AI translation; modules and parts with editable prompts; API providers
as in InkPeek and CLI providers (`claude -p`, `codex exec`); answers cached through `ai.put` · M5 records in the UI (☆, history, TSV for Anki, export / import) · M6 browser
extension for Chromium and Firefox: whole-page translation through the app's `server/`; selection in the browser
only if M2 shows the system hotkey misses it there
· M7 Windows · M8 sync folder (InkPeek too) · M9 Linux (X11; Wayland partly).

## License
MIT. Packs keep their own licenses (Wiktionary CC BY-SA, Mozilla MPL-2.0, OPUS-MT CC BY 4.0, Hy-MT2 Apache-2.0).
