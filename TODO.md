# TODO

- M1 Core (done on macOS): packs, StarDict, header, gloss, separable verbs, language by the dictionaries, methods with
  reasons, the log + SQLite, settings, views (card, packs list); InkPeek's sync, sentence and device cases pass. Open:
  CI on Linux / Windows not run since M0 (the owner: macOS first); InkPeek's `packs/tests/golden` check the pack build,
  not the app (no cross-test); the offline answers' `ver` is the method only (an updated model does not translate again).
- Store (M1 done): log compaction (formats.md → Compaction) not done yet; the logs folder is `<data>/log` until the
  sync folder setting (M8).
- Packs (M1 done): Swift can't build `std::function` for `Packs::install`'s progress / cancel — M2 adds a shim if
  needed; Windows: `http.cpp` / `unpack` open files by narrow paths (M4); CI could fetch models with `packs install`.
- M2: mockups are in `docs/spec.md` → Card and screens; refine them on the first prototype.
- M2 Engines bench: on an 8 GB Mac; OPUS-MT's working set (1.2 GB) and peak (2 GB) — check `mini-batch-words`.
- M0 Engines (done) · M1 Core · M2 macOS shell · M3 Release 0.1 (see `docs/spec.md`).
