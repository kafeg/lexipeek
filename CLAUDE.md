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
- App (macOS): `make app` (core + `mac/` by XcodeGen, Release, ad-hoc signed; `SIGN=<identity>` keeps the
  Accessibility grant across builds), `make app-test` (Swift unit tests of `LexiPeekKit`), `make install`
  (into `/Applications` in place, refreshes Services, starts it). Check without Accessibility:
  `open "lexipeek://lookup?text=Das%20Haus"` or `lexipeek://input`; `LEXIPEEK_SNAPSHOT=<dir>` makes the app write
  its own card and settings as PNGs (no Screen Recording needed).
- CLI: `build/cli/lexipeek-cli translate --from de --to ru [--method fast|direct|llm] TEXT`, `langid TEXT`, `bench`.

## Code layout and gotchas
- `core/include/lexipeek/` — the facade (`mt.hpp`: `Translator`, `packs.hpp`); `core/src/<module>/` — one folder per
  module of the spec's Architecture, empty ones are stubs for their milestone.
- `core/src/mt/`: `text` (steps, Marian options, sentences, chunks — pure, tested), `model_host` (one model, idle
  thread), `bergamot`/`llm`/`langid` (the engines, as InkPeek's `*_jni.cpp`), `translator` (the methods).
- Marian's headers do not compile as C++20 (`std::result_of`): `bergamot.cpp` is its own C++17 target.
- Bergamot's `BlockingService` is never destroyed: at exit it would outlive marian's statics and abort.
- Marian needs BLAS and LAPACK: Accelerate on macOS, OpenBLAS + clapack (vcpkg, no Fortran) elsewhere. Windows: static CRT (`/MT`, Bergamot's
  choice) everywhere, triplet `x64-windows-static`; macOS: overlay triplets in `cmake/triplets/` (macOS 14).
- Engines build as C++17 (`cmake/engines.cmake`), our code as C++20. Linux: OpenBLAS `dynamic-arch` (a cached
  build runs on other runners' CPUs, else SIGILL), marian's `USE_STATIC_LIBS` off (glibc's `libm.a`).
- CI (`.github/workflows/ci.yml`): macOS arm64, Linux x64, Windows x64, Linux ASan + UBSan; models from the packs
  release on macOS and Linux (cached), none on Windows.
- `mac/`: `cmake/apple_bundle.cmake` walks `lexipeek_core`'s link closure and `libtool`s it into
  `build/mac/liblexipeek_all.a` + `link.xcconfig` (frameworks, system libs); Swift imports the facade through
  `mac/Bridge` (module `LexiPeekCore`: `lexipeek::mac::Engine`, a copyable `shared_ptr<Core>` built from strings).
  `LexiPeekKit` — pure Swift (shortcuts, placement, recents, URLs), tested without the core. `CoreClient` — two
  serial queues (look-ups, one translation worker), a newer request turns older ones into no-ops.
- A Debug `LexiPeek.app` would also claim `lexipeek://`: the test scheme builds only `LexiPeekKit`.
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
