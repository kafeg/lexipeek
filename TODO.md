# TODO

- M1 Core (done on macOS): packs, StarDict, header, gloss, separable verbs, language by the dictionaries, methods with
  reasons, the log + SQLite, settings, views (card, packs list); InkPeek's sync, sentence and device cases pass. Open:
  CI on Linux / Windows not run since M0 (the owner: macOS first); InkPeek's `packs/tests/golden` check the pack build,
  not the app (no cross-test).
- Store (M1 done): the logs folder is `<data>/log` until the sync folder setting (M8).
- Packs (M1 done): Windows: `http.cpp` / `unpack` open files by narrow paths (M6); CI could fetch models with `packs install`.
- M2 open: check the selection and ⌥D's
  position under it (AX bounds) in Safari, Chrome, Firefox, Vivaldi and native apps; per-app hotkey off is checked
  on the frontmost app only; the card's keys (←/→, Return) work only once it has the focus (a click).
- InkPeek's data (fix there, not here): dict-de-ru has a headword "ist" ("ист") that wins over the form of *sein*
  in the gloss ("Das Haus ist alt"); InkPeek's own card does the same.
- M2 Engines bench: on an 8 GB Mac; OPUS-MT's working set (1.2 GB) and peak (2 GB) — check `mini-batch-words`.
- M2.1 core part: card view model, `lexipeek::app` facade, `Core::configure`, answers' `ver` done.
  Open: the Settings panes (Translation's method list, Packs' rows text) still build their labels in Swift (views for them: M4).
- M3 first run and Mozilla's English: done in the core (tested) and the window. Open: the first-run snapshot cases
  and `passage-english` not yet looked at (run invisibly by the owner's session); the models test of the English
  (`firstrun_test.cpp`) not run here; the Accessibility step's "goes on by itself" not tried on a real grant.
- Milestones: `docs/spec.md` → Milestones (M2.1 debts → M3 card parity → M4 dictionaries and settings → M5 AI → M6 Windows).
