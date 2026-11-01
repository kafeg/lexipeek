# Testing

The point of the tests: LexiPeek's logic and data formats stay identical to InkPeek's. The parity tests run InkPeek's own
cases from `tests/inkpeek` (the pinned submodule), never edited to make LexiPeek pass:
- **sync** — `store_test`: InkPeek's `tests/sync` cases (the log, HLC, merge, the SQLite view), compaction merging the same;
- **device cases** — `inkpeek_cases_test`: InkPeek's `cases.tsv` (headers, meanings, the text's language, separable verbs,
  translations) on real packs: 211 run on macOS and Linux, 200 on Windows (no translation model there);
- **sentences** — `sentences_test`: InkPeek's sentence cases and Unicode's `SentenceBreakTest.txt`;
- **AI prompts and versions** — `ai_test`, `study_test`: InkPeek's prompts word for word, Java `hashCode` versions;
- **engines** — ctest `engine-pins`: the engine submodules at InkPeek's commits.

The rest of the core (`tests/*_test.cpp`, doctest): lookup and views, nav and `CardSession`, settings, Notes, packs,
dictionaries, first run, l10n, Read aloud's logic, the app facade. A wait in a test is `until()` (`tests/temp.hpp`), never a
fixed sleep.

## CI (`.github/workflows/ci.yml`, every push to `main`)
| job | runs | packs in `LEXIPEEK_MODELS` |
|---|---|---|
| `core macos-arm64` | ctest (core, engine-pins, cli-usage), `make app-test` (LexiPeekKit), the CLI translates | langid, Mozilla de-en and en-ru, OPUS-MT de→sla, six dictionaries (de, en, ru pairs) |
| `core linux-x64` | ctest, the CLI translates | the same |
| `linux-asan` | ctest under ASan + UBSan, only in a run by hand (`gh workflow run ci.yml`; locally `make asan`) | langid, Mozilla de-en, the six dictionaries |
| `core windows-x64` | ctest, the WinUI shell's build (artifact) | langid and the six dictionaries (`lexipeek-cli packs install`) |
| `windows-qt` | the Qt shell's build, only in a run by hand (`gh workflow run ci.yml`) | — |

The device cases assume a device's whole pack set: every job with dictionaries has all six.

## Local only
- Hy-MT2 tests (`LEXIPEEK_MODELS` with `llm/llm-hy-mt2-1.8b-q4.gguf`); without a model a test says "skipped".
- The shells: `make snapshots`, `scripts/selection-test.sh`, `scripts/qt-snapshots.sh`, on Windows `LexiPeek.exe
  --self-test` / `--snapshots`, `scripts/win-e2e.ps1`, `scripts/win-read-e2e.ps1`.
