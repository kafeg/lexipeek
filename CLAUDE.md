# LexiPeek

Pop-up dictionary and offline translator for the desktop (macOS first, then Windows, Linux): select text in any app, press a hotkey, a card opens at the
cursor. Desktop sibling of InkPeek, uses its language packs. Spec — `docs/spec.md` (source of truth).
Public repo (MIT): no secrets, keys, personal paths or IPs in commits.

## Stack
C++20 core with all the logic + a thin native shell per OS. `core/` — no UI, no OS headers; vcpkg: sqlite3,
nlohmann/json, libcurl, libarchive (zstd), hash-library, utf8proc, doctest (from M1: the spec's Stack); facade `core/include/lexipeek/` (plain types, Swift-importable).
`mac/` — Swift, SwiftUI + AppKit, XcodeGen, C++ interop (as panerail). `qt/` (M4) — Qt 6 Widgets for Windows and Linux.
Engines as submodules at InkPeek's commits: Bergamot, PCRE2, fastText, llama.cpp. macOS 14+, Apple Silicon only.

## Build and run
Submodules: `vcpkg/` (manifest mode, `vcpkg.json`, toolchain set in `CMakeLists.txt`), `engines/` (Bergamot,
fastText, llama.cpp, PCRE2 at InkPeek's commits), `tests/inkpeek` (InkPeek, test data and the engine pins).
- `make deps` — submodules (Bergamot recursively; not `--recursive` at the root: InkPeek's own engines are not needed).
- `make build` — CMake + Ninja into `build/`; the first build compiles the vcpkg ports and the engines (~8 min).
- `make test` — ctest: `core` (doctest, `tests/*_test.cpp`), `engine-pins`, `cli-usage`. Model tests run when
  `LEXIPEEK_MODELS` names a data dir with the models, else they say "skipped".
- `make asan` — the same with ASan + UBSan on our code (`build-asan/`). `make bench` — `lexipeek-cli bench`.
- Models (M0, by hand; M1: the pack manager): `scripts/fetch-models.sh <data-dir> <id>…` from InkPeek's `packs`
  release, sha256 checked; layout as InkPeek's (`models/<from><to>/`, `opus/<id>/`, `llm/<id>.gguf`, `langid.ftz`).
  Default data dir: `~/Library/Application Support/LexiPeek` (or `LEXIPEEK_DATA`, `--data`).
- CLI: `build/cli/lexipeek-cli translate --from de --to ru [--method fast|direct|llm] TEXT`, `langid TEXT`, `bench`, `merge FOLDER`, `history`.

## Code layout and gotchas
- `core/include/lexipeek/` — the facade (`mt.hpp`: `Translator`, `packs.hpp`); `core/src/<module>/` — one folder per
  module of the spec's Architecture, empty ones are stubs for their milestone.
- `core/src/dict/` (InkPeek's `stardict/`): `stardict` (own sorted index in `<data>/index/<id>/`, `.dict.dz` by dictzip
  chunks), `library` (lookup across packs, heads, gloss, separable verbs, rank). `core/src/text/`: PCRE2 wrapper, InkPeek's
  lookup key (NFC, no soft hyphen, lowercase), query cleaning. `core/src/lookup/`: `Core` — request → `Card`, language by
  the dictionaries (InkPeek's `LangId.source`). Cross-test: `tests/inkpeek_cases_test.cpp` runs InkPeek's `cases.tsv`.
- `core/src/mt/`: `text` (steps, Marian options, sentences, chunks — pure, tested), `model_host` (one model, idle
  thread), `bergamot`/`llm`/`langid` (the engines, as InkPeek's `*_jni.cpp`), `translator` (the methods).
- Sentences: `mt/sentences.cpp`, UAX #29 as ICU without suppressions; table `mt/sentence_break.inc` from
  `scripts/gen-sentence-break.py <SentenceBreakProperty.txt>` (Unicode 17); tests: `tests/data/SentenceBreakTest.txt`, InkPeek's cases.
- `core/src/store/`: `log` (lines, HLC, read, append, merge — pure, InkPeek's `tests/sync` cases), `store` (`Store`:
  own lines to `<data>/log/<device>.jsonl`, `<data>/lexipeek.db` is a view: rebuilt from the logs on open and
  `merge()`, a text's rows re-merged from its lines on each write; the 1,000-entry limit only in the view).
- Marian's headers do not compile as C++20 (`std::result_of`): `bergamot.cpp` is its own C++17 target.
- Bergamot's `BlockingService` is never destroyed: at exit it would outlive marian's statics and abort.
- Marian needs BLAS and LAPACK: Accelerate on macOS, OpenBLAS + clapack (vcpkg, no Fortran) elsewhere. Windows: static CRT (`/MT`, Bergamot's
  choice) everywhere, triplet `x64-windows-static`; macOS: overlay triplets in `cmake/triplets/` (macOS 14).
- Engines build as C++17 (`cmake/engines.cmake`), our code as C++20. Linux: OpenBLAS `dynamic-arch` (a cached
  build runs on other runners' CPUs, else SIGILL), marian's `USE_STATIC_LIBS` off (glibc's `libm.a`).
- CI (`.github/workflows/ci.yml`): macOS arm64, Linux x64, Windows x64, Linux ASan + UBSan; models from the packs
  release on macOS and Linux (cached), none on Windows.
- Warnings as errors only for our targets (`lexipeek_flags`); engines are `SYSTEM` and keep their flags.

## Rules
- Branch model: `main` only, commit and push to `main`.
- Spec first: a new feature → `docs/spec.md` → implement by milestone, stop after each.
- **InkPeek is the reference.** No shared code. Behaviour and data formats (`packs.json`, sync log, records) are
  defined in InkPeek first; LexiPeek matches them. `tests/inkpeek` (submodule, pinned) gives the cross-test data;
  moving its pin = catching up: bump, run tests, fix LexiPeek. Never change a case to make LexiPeek pass.
- **No AI in core.** Look-up, header and translation work offline from the packs. AI is an optional tab, only on
  the person's action. No guessing heuristics in data.
- **An app, not a machine.** Everything comes from the app's own downloads (InkPeek's `packs` release, sha256).
- **Logic lives in the core**, tested there; a shell only draws `views` and calls the facade. `core/` never
  includes UI or OS headers; strings are UTF-8, normalised with utf8proc (NFC, lowercase), regex only PCRE2, sentences by our UAX #29 segmenter. KISS; comments — one line at most.
- The API key never goes to logs, UI or commits.
- **Nothing of the author's infrastructure** in the repo: no host or VM names, IPs, runner labels, paths. Build
  scripts and CI are generic (GitHub-hosted runners); building on private machines is documented outside this repo.
