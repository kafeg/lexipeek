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
| `app/` | Qt 6 Quick / QML (LGPL, dynamic linking), tray via `QSystemTrayIcon` |
| `app/platform/mac/` | Objective-C++: hotkey (Carbon `RegisterEventHotKey`), selected text (AX `kAXSelectedTextAttribute`, else Cmd+C and the clipboard restored), non-activating `NSPanel`, Accessibility prompt |
| build | CMake + `make build/test/install`; GitHub Actions; `.app` signed with Developer ID, notarized, on GitHub Releases |

Not used: Qt HTTP Server (GPL-3; the extension later uses cpp-httplib, MIT), Electron, Tauri, JVM.

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

## Records and the sync log
v1 shows no records, but writes the history in the sync log format from the start. The format (records schema,
per-device JSONL log, merge rules) is InkPeek's [`docs/formats.md`](https://github.com/kafeg/inkpeek/blob/main/docs/formats.md);
its cases are `tests/sync/` there, run here through `tests/inkpeek`. Transport later, in layers: a folder synced by
Syncthing or a cloud → mDNS in the home network → iroh (optional). No servers of ours.

## Card and screens
Mockups come before M2, from a study of how other desktop translators work (popup, placement, tabs, settings,
downloads, permissions). Until then: the card has Dictionary and Translate tabs; Esc or a click outside closes it.
Settings: packs (download, remove, sizes), languages, translation method, hotkey, appearance.

## Milestones (each: spec → code → check → commit, stop)
v1:
- **M0 Engines.** CMake, submodules, Bergamot, fastText, llama.cpp (Metal) for macOS arm64; `lexipeek-cli translate`
  on InkPeek's pack models. Check: InkPeek's converted OPUS-MT int8 models load on Apple Silicon; speed and memory
  of the three methods.
- **M1 Core.** Packs (manifest, resumable download, sha256, install), StarDict with `.syn`, header, word-by-word
  gloss, separable verbs, language id, method choice with reasons, the log and SQLite. Cross-tests green.
- **M2 macOS shell.** Mockups first. Menu bar, hotkey, selected text, non-activating card at the cursor
  (Dictionary, Translate), settings, Accessibility onboarding. A build for daily use.
- **M3 Release 0.1.** Signing, notarization, GitHub Releases, update check.

Later, in this order: M4 AI (API providers as in InkPeek; CLI providers `claude -p`, `codex exec`; modules,
prompts, cache through `ai.put`) · M5 records in the UI (☆, history, TSV for Anki, export / import) · M6 browser
extension for Chromium and Firefox: selection → card (link to the app: localhost with a token or native messaging)
· M7 whole-page translation · M8 Windows · M9 sync folder (InkPeek too) · M10 Linux (X11; Wayland partly).

## License
MIT. Packs keep their own licenses (Wiktionary CC BY-SA, Mozilla MPL-2.0, OPUS-MT CC BY 4.0, Hy-MT2 Apache-2.0).
