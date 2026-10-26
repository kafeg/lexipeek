# TODO

- M1 Core (done on macOS): packs, StarDict, header, gloss, separable verbs, language by the dictionaries, methods with
  reasons, the log + SQLite, settings, views (card, packs list); InkPeek's sync, sentence and device cases pass. Open:
  CI on Linux / Windows not run since M0 (the owner: macOS first); InkPeek's `packs/tests/golden` check the pack build,
  not the app (no cross-test); the offline answers' `ver` is the method only (an updated model does not translate again).
- Store (M1 done): the logs folder is `<data>/log` until the sync folder setting (M8).
- Packs (M1 done): Swift can't build `std::function`: `mac/Bridge` wraps `Packs::install` with atomics for progress / cancel; Windows: `http.cpp` / `unpack` open files by narrow paths (M4); CI could fetch models with `packs install`.
- M2 open: first run's "pick a pair, download it" step (Permissions only now); check the selection and ⌥D's
  position under it (AX bounds) in Safari, Chrome, Firefox, Vivaldi and native apps; per-app hotkey off is checked
  on the frontmost app only; the card's keys (←/→, Return) work only once it has the focus (a click).
- InkPeek's data (fix there, not here): dict-de-ru has a headword "ist" ("ист") that wins over the form of *sein*
  in the gloss ("Das Haus ist alt"); InkPeek's own card does the same.
- M2 Engines bench: on an 8 GB Mac; OPUS-MT's working set (1.2 GB) and peak (2 GB) — check `mini-batch-words`.
- Milestones: `docs/spec.md` → Milestones (M2.1 debts → M3 card parity → M4 dictionaries and settings → M5 AI → M6 Windows).
