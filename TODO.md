# TODO

- M1 Core: packs (manifest, resumable download, sha256, install), StarDict, header, gloss, separable verbs, language
  id with the dictionaries (InkPeek's `LangId.source`), method choice with reasons (`ram_mb`, `cpu`), the log, SQLite,
  `views`; cross-tests from `tests/inkpeek` (`packs/tests/golden`, `cases.tsv`, `tests/sync/`).
- M1: our UAX #29 sentence segmenter (`core/src/mt/sentences.*`, table generated from Unicode's
  `SentenceBreakProperty.txt`) instead of ssplit in `mt/text.cpp`; tests: `tests/inkpeek/tests/sentences/sentences.tsv`
  and `SentenceBreakTest.txt`. libarchive (zstd), hash-library into `vcpkg.json`. Library study:
  owlproj `research/lexipeek/2026-10-26-core-libraries.md`.
- Packs (M1 done): Swift can't build `std::function` for `Packs::install`'s progress / cancel — M2 adds a shim if
  needed; Windows: `http.cpp` / `unpack` open files by narrow paths (M4); CI could fetch models with `packs install`.
- M2: mockups are in `docs/spec.md` → Card and screens; refine them on the first prototype.
- M2 Engines bench: on an 8 GB Mac; OPUS-MT's working set (1.2 GB) and peak (2 GB) — check `mini-batch-words`.
- M0 Engines (done) · M1 Core · M2 macOS shell · M3 Release 0.1 (see `docs/spec.md`).
