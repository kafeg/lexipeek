# Testing

The point of the tests: LexiPeek's logic and data formats stay identical to InkPeek's. The parity tests run InkPeek's own
cases from `tests/inkpeek` (the pinned submodule), never edited to make LexiPeek pass:
- **sync** — `store_test`: InkPeek's `tests/sync` cases — `reader` (the merge, and the store's SQLite view of the folder),
  `identity` (keep / new, by the pure check and by the store at start), `writer` (the device's files byte for byte,
  written by the store), `generated` (24 cases made by InkPeek's `tests/sync/generate.py`: the merge, the same read twice
  and with every device compacted) —, compaction merging the same, two stores on one folder;
- **Anki** — `anki_test`: InkPeek's `tests/anki` cases — Keys, a note's Front / Back / Example / Key from an entry, the
  rule for duplicates (`note_of`, `dedupe_plan`);
- **device cases** — `inkpeek_cases_test`: InkPeek's `cases.tsv` (headers, meanings, the text's language, separable verbs,
  translations) on real packs: 211 run on macOS and Linux, 200 on Windows (no translation model there);
- **selection** — `lookup_cases_test`: InkPeek's `tests/lookup` cases (a card's kind, title, text looked up and its
  words: edge quotes, dashed words) on a small dictionary made per case;
- **sentences** — `sentences_test`: InkPeek's sentence cases and Unicode's `SentenceBreakTest.txt`;
- **AI prompts and versions** — `ai_test`, `study_test`: InkPeek's prompts word for word, Java `hashCode` versions;
- **engines** — ctest `engine-pins`: the engine submodules at InkPeek's commits.

The rest of the core (`tests/*_test.cpp`, doctest): lookup and views, nav and `CardSession`, settings, Notes, packs,
dictionaries, first run, l10n, Read aloud's logic, the app facade, Anki (`anki_test`: a fake AnkiConnect on 127.0.0.1 — decks,
note types, notes, media in memory — reached through libcurl). A wait in a test is `until()` (`tests/temp.hpp`), never a
fixed sleep. A test never writes `LEXIPEEK_MODELS`: it runs on a data dir of its own with links to the packs only
(`packs_of`, `tests/temp.hpp`; the index in the temp folder), and `LEXIPEEK_DATA` is a temp dir for the whole run.

## CI (`.github/workflows/ci.yml`, every push to `main`)
| job | runs | packs in `LEXIPEEK_MODELS` |
|---|---|---|
| `core macos-arm64` | ctest (core, engine-pins, cli-usage), `make app-test` (LexiPeekKit), the CLI translates | langid, Mozilla de-en and en-ru, OPUS-MT de→sla, six dictionaries (de, en, ru pairs) |
| `core linux-x64` | ctest, the CLI translates | the same |
| `linux-asan` | ctest under ASan + UBSan, only in a run by hand (`gh workflow run ci.yml`; locally `make asan`) | langid, Mozilla de-en, the six dictionaries |
| `core windows-x64` | ctest, the WinUI shell's build (artifact) and its installer (`win/LexiPeek.iss`, artifact) | langid and the six dictionaries (`lexipeek-cli packs install`) |
| `windows-qt` | the Qt shell's build, only in a run by hand (`gh workflow run ci.yml`) | — |
| `linux-app` (`linux.yml`) | the Linux app (`scripts/build-linux.sh` on Ubuntu 22.04) and its `--self-test` under Xvfb (X11 hotkeys, keys, autostart, `lexipeek://`), release.yml's Linux job, and on a push to `main` that touches `qt/`, `platform/`, `core/include/` or the build; by hand (`gh workflow run linux.yml`) its dry run; the tar.gz as an artifact | — |

The device cases assume a device's whole pack set: every job with dictionaries has all six.

## Local only
- `LEXIPEEK_STRESS=1`: `store_test` makes a sync case of 50,000 ops (`python3`, InkPeek's `generate.py --stress`) and
  merges it (~5 s).
- Hy-MT2 tests (`LEXIPEEK_MODELS` with `llm/llm-hy-mt2-1.8b-q4.gguf`); without a model a test says "skipped".
- The shells: `make snapshots`, `scripts/selection-test.sh`, `scripts/qt-snapshots.sh`, on Windows `LexiPeek.exe
  --self-test` / `--snapshots`, `scripts/win-e2e.ps1` (with the first card's height, About and Settings in front of the
  window, a taken hotkey's fallback), `scripts/win-read-e2e.ps1`, `scripts/win-install-e2e.ps1` (the installer — CI's
  `LexiPeek-Windows-Setup` artifact — on a machine without LexiPeek: install, first run, again over the running app,
  uninstall).
