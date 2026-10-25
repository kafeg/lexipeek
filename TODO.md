# TODO

- M1 Core: packs (manifest, resumable download, sha256, install), StarDict, header, gloss, separable verbs, language
  id with the dictionaries (InkPeek's `LangId.source`), method choice with reasons (`ram_mb`, `cpu`), the log, SQLite,
  `views`; cross-tests from `tests/inkpeek` (`packs/tests/golden`, `cases.tsv`, `tests/sync/`).
- M1: sentence splitting for OPUS-MT tags and Hy-MT2 chunks uses ssplit without a prefix file (InkPeek: Android's
  BreakIterator); compare on InkPeek's cases, add ssplit's per-language prefix files if they differ.
- M2: mockups are in `docs/spec.md` → Card and screens; refine them on the first prototype.
- M2 Engines bench: on an 8 GB Mac; OPUS-MT's working set (1.2 GB) and peak (2 GB) — check `mini-batch-words`.
- M0 Engines (done) · M1 Core · M2 macOS shell · M3 Release 0.1 (see `docs/spec.md`).
