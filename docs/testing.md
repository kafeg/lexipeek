# Testing

What checks each part of LexiPeek, and where it runs. The logic lives in the core and is tested there (doctest,
`tests/*_test.cpp`, `make test`); the shells are checked by drawing every screen (snapshots) and by their own small tests.

## CI jobs (`.github/workflows/ci.yml`, every push to `main`)
| job | runs | packs in `LEXIPEEK_MODELS` |
|---|---|---|
| `core macos-arm64` | ctest (core, engine-pins, cli-usage); `make app-test` (LexiPeekKit); `make app` + `scripts/snapshots.sh` (artifact `snapshots-macos`); `scripts/selection-test.sh` (Accessibility granted through TCC's database); the CLI translates | langid, Mozilla de-en and en-ru, OPUS-MT de→sla, six dictionaries (de, en, ru pairs) |
| `core linux-x64` | ctest; the CLI translates | the same |
| `core linux-x64-asan` | ctest under ASan + UBSan | langid, Mozilla de-en, the six dictionaries |
| `core windows-x64` | ctest; the WinUI build; `scripts/win-checks.ps1`: `LexiPeek.exe --self-test`, `--snapshots` of the card cases (artifact `snapshots-windows`) | langid and the six dictionaries (by `lexipeek-cli packs install`), no translation model |
| `windows-qt` | the Qt shell's build, only in a run by hand (`gh workflow run ci.yml`) | — |

A snapshot run fails on a case that never finished (a crash, a hang, `failed.txt`); the images are not compared with
anything: look at them (the artifact, or locally `make snapshots`). InkPeek's device cases assume a device's whole pack
set: every job with dictionaries has all six (211 cases run on macOS and Linux, 200 on Windows: no translation model).
Times with warm caches: see the job's steps; the shells add ~11 min to macOS (app build, snapshots, selection) and
~4 min to Windows (self-test, 81 cases × light / dark).

## The core, by module
| module / feature (spec) | tests | CI |
|---|---|---|
| lookup: word, phrase, passage, the text's language, entries per dictionary, sections, article + noun, gloss, separable verbs | `inkpeek_cases_test` (InkPeek's device cases, 211 on real packs), `views_test`, `notes_test`, `dict_test` | all (device cases: wherever dictionaries are; ASan de-en / de-ru only) |
| dict: StarDict, own dictionaries, folders, junctions, import, catalog, order | `dicts_test`, `dict_test` | all |
| mt: steps, Marian options, OPUS-MT pieces and tags, `unlead`, sentences, ModelHost | `core_test`, `sentences_test` (UAX #29 + InkPeek's cases) | all |
| mt with models: Fast, Direct, langid, idle unload and reload budget, kept translations | `models_test`, `firstrun_test` ("with models") | macOS, Linux; ASan Fast de-en only; Windows none |
| mt: Hy-MT2 (AI model) | `models_test` (`llm:`, the card session's method) | local only (`LEXIPEEK_MODELS` with `llm/llm-hy-mt2-1.8b-q4.gguf`) |
| packs: list, install (every kind), mirror, resume, sha256, cancel, space, updates, levels, `cannot` | `packs_test` (a fixture release by `file://`), `core_test` | all |
| store: the sync log, HLC, merge (InkPeek's sync cases), the SQLite view, compaction, backup / import, daily copy | `store_test` | all |
| ☆ and Notes: save key, three lists, pager, TSV, remove, clear | `notes_test`, `study_test` | all |
| views: card (every state, tabs, footer, pager, copy, badge), help, rich text | `views_test`, `ai_test` | all |
| nav and session: every `CardNav` step; `CardSession`: look-ups, sections and pages, full entries, one entry, tabs, re-lookup, ☆ again, method, AI, cancel, ← | `views_test` (nav), `session_test`, `notes_test`, `ai_test`, `speech_test`, `study_test` | all |
| settings: every pane's rows, every toggle and choice round trip, the card honours them, hotkey defaults | `settings_test`, `core_test` | all |
| first run: steps, sizes, at launch, the default answer | `firstrun_test` | all |
| app facade: data dir, one-setting writes, the app's language, Engine remove / remove_dict, memory lines, hotkey labels, links, file names | `app_test`, `views_test`, `dicts_test` | all |
| l10n: every key in eight languages, placeholders, plurals, every screen, the keys Swift and WinUI ask for, no English literal in Swift | `l10n_test` | all |
| AI: InkPeek's prompts and versions, each provider's request, least reasoning and recall, errors, the store, Markdown, models list, file answers, a CLI program run for real | `ai_test`, `session_test` | all (the real program: not Windows, whose shell runs it by its own runner) |
| study (one language you learn) | `study_test` | all |
| Read aloud: what ▶ reads, phases, length, `.rvq`, WAV in / out, voices, kept and saved readings, the note's reading, settings | `speech_test`, `study_test` | all (the engine is built on macOS only: its kept-reading checks run there) |
| about: the update check's answer | `settings_test` | all |
| engines at InkPeek's pins | ctest `engine-pins` | all |

## The shells
| shell | checks | CI |
|---|---|---|
| macOS, `LexiPeekKit` (shortcuts, placement, the card's keys, Dock) | `mac/Tests` (`make app-test`) | macOS |
| macOS app: every card case, the window, Notes, every Settings pane, About, catalog, first run, AI and Read aloud states, light and dark | `scripts/snapshots.sh` | macOS (drawn; looked at by eye) |
| macOS: a web view's selection read by the hotkey's code (22 pages) | `scripts/selection-test.sh` | macOS |
| Windows: the OS parts (Credential Manager, Run key, hotkeys, CLI runner, `lexipeek://`, icon font) | `LexiPeek.exe --self-test` | Windows |
| Windows: every card case, Settings panes, About, menu, install question, light and dark | `LexiPeek.exe --snapshots` | Windows (the card cases; the first-run and AI cases only locally) |
| Windows: tray, hotkeys, a real selection in Notepad, Esc, links, a second launch; Read aloud | `scripts/win-e2e.ps1`, `scripts/win-read-e2e.ps1` | local only (a signed-in desktop where keys can be sent) |
| Qt shell | `scripts/qt-snapshots.sh` | local only (its build: `windows-qt` by hand) |

## Local only
- Model tests need `LEXIPEEK_MODELS` (a data dir with packs); without it they say "skipped".
- Hy-MT2, the voice model (Read aloud for real: `lexipeek-cli say`, `say-bench`), Vulkan and Metal speed.
- The app's selection and hotkeys in other apps, Windows' tray by a real click, `UI_LANG=<lang>` snapshot runs.
