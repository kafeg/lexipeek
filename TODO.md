# TODO

- M1 Core: packs (manifest, resumable download, sha256, install), StarDict, header, gloss, separable verbs, language
  id with the dictionaries (InkPeek's `LangId.source`), method choice with reasons (`ram_mb`, `cpu`), the log, SQLite,
  `views`; cross-tests from `tests/inkpeek` (`packs/tests/golden`, `cases.tsv`, `tests/sync/`).
- M1: our UAX #29 sentence segmenter (`core/src/mt/sentences.*`, table generated from Unicode's
  `SentenceBreakProperty.txt`) instead of ssplit in `mt/text.cpp`; tests: `tests/inkpeek/tests/sentences/sentences.tsv`
  and `SentenceBreakTest.txt`. libarchive (zstd), hash-library into `vcpkg.json`. Library study:
  owlproj `research/lexipeek/2026-10-26-core-libraries.md`.
- M2 open: Packs pane (list, download, remove) on the packs API; Recents from `history.seen` (the store) instead of
  the shell's own list; answer language and method in M1's `settings.json` (now the shell's UserDefaults); first run's
  "pick a pair, download it" step; check the selection in Safari, Chrome, Firefox, Vivaldi and native apps; ⌥D's
  position under the selection (AX bounds) on real apps; the dictionary tab once `dict` fills the card.
- M2 Engines bench: on an 8 GB Mac; OPUS-MT's working set (1.2 GB) and peak (2 GB) — check `mini-batch-words`.
- M0 Engines (done) · M1 Core · M2 macOS shell · M3 Release 0.1 (see `docs/spec.md`).
