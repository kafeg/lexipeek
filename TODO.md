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
- M3 card parity, Dictionary tab (branch m3-entry): full entry as senses and examples, `.rel` sections and their pages,
  a clicked gloss / section word opens in the card with "←", "form: …" + frequency + looks under the header, the text's
  language's packs first, "… and N more words". Open: an entry's line is not clickable to its own article (the card
  toggles all with "Full entry ⏎"); the origin section is shown (InkPeek hides it by default: its setting is M4); other
  dictionaries' articles stay HTML (M4, with InkPeek's `shortMeaning`); a word's header is cut at two lines (InkPeek: up to 3).
  The origin section is now off by default (Settings → Card → Article).
- Kept as InkPeek, by decision: unknown text ("+++ 42 ### 17", made-up words) gets fastText's best book language
  when it is unsure and no dictionary knows a word (`LangId.source`'s last branch) — detecting "not a language" would
  misfire on real text; a separable verb's finite form is listed again on its own ("stand … auf" and "stand: stehen"), as InkPeek's gloss.
- M3 ☆ and Notes done (core `toggle_saved`, `notes_view`; Notes window). Open: Notes has no TSV export / import
  (spec's "Then"); an open card's ★ is not refreshed when Notes removes its word; History lists all 1,000 at once.
- M3 first run and Mozilla's English: done in the core (tested) and the window. Open: the first-run snapshot cases
  and `passage-english` not yet looked at (run invisibly by the owner's session); the models test of the English
  (`firstrun_test.cpp`) not run here; the Accessibility step's "goes on by itself" not tried on a real grant.
- M4 own dictionaries (branch m4-dicts): dicts-own + folders, on / off, order with the packs, short meaning in the card
  and the gloss, import (zip, tar.gz/bz2/xz/zst, loose files), InkPeek's catalog, `lexipeek-cli dicts`, the pane
  `OtherDictionariesPane.swift`. Open: indexes are built on the look-up queue (`Core::prepare` after a reload; a big
  folder delays the next card; InkPeek builds in the background with a status per dictionary); no "indexing…" state in
  the rows; the catalog's list is fetched on every opening (no cache); index folders of dictionaries gone from a
  folder stay in `<data>/index`; the pane and the catalog sheet not yet looked at in snapshots (cases to add:
  `settings-other` with an own dictionary and a folder, `settings-catalog` with a fixture list, an own line in a card).
- M4 settings (branch m4-settings): InkPeek's sections (Languages · Card · Notes · General) with sub-tabs, rows from the
  core (`settings_view`), the card's settings honoured (phrase opening, full entry, grammar, sections, width, text
  size, font, height limit); the dictionary order is own dictionaries' (Article links to it); Notes moved into Settings. Open: the settings snapshots not yet looked at (run invisibly by
  the owner's session); InkPeek's Opening also has AI choices (M5).
- M4 UI languages, About, updates (branch m4-l10n): `core/src/l10n` (en ru de fr es it pt pl, InkPeek's translations
  where the meaning is the same), every `views` text and every Swift string through it; General → App language (as the
  system by default; a new install answers in it); About with the packs' licenses; "Check now" asks GitHub's latest
  release. Open: no snapshot run in another language looked at yet (`UI_LANG=ru scripts/snapshots.sh`); the translations
  not reviewed by native speakers; low-level errors (libcurl, libarchive, "Cannot write …", exceptions of the engines) stay
  English; the catalog's rarer languages (outside the 32 named in the table) keep their English names; the Service's
  menu title "Look up in LexiPeek" (Info.plist) and SwiftUI's own buttons (a dialog's Cancel) follow the bundle's
  English; an open card keeps its texts until it is drawn again.
- M5 AI (branch m5-ai): `core/src/ai` (Claude, OpenAI, OpenRouter, an OpenAI-compatible server, `claude -p`,
  `codex exec`; InkPeek's prompts and versions, least reasoning per host, errors, answers through `ai.put`), the card's
  Analysis · AI and Context · AI tabs, AI translation as a Translate method and as Card → Opening choices, Settings → AI
  (Connection · Modules, ✎ prompts, the model list, a check), keys in the Keychain, `lexipeek-cli ai`. Open: no request
  was sent to a real provider from here (no key in this session; the request shapes are tested against a fake
  transport) — try each provider once; the AI snapshot cases (`ai-*`, `settings-ai-*`) not yet run or looked at; the
  CLI providers have no timeout (a hung `claude -p` keeps one AI worker busy; the card just waits) and their flags
  (`--output-format json`, `-c model_reasoning_effort=minimal`) are from the tools' docs, not tried here; Windows: the
  shell command line (`popen`, `{ …; } 2>file`, POSIX quoting) is POSIX only (M6); InkPeek's "online" check is not
  ported (no network → curl's "could not resolve host" → the offline text); a phrase whose AI answer failed does not
  show the offline gloss under the error (InkPeek does); the model list has a filter and "free only" but not InkPeek's
  maker filter and sorting; AI's base form does not re-order the dictionaries (InkPeek's quick-answer re-sort).
- Milestones: `docs/spec.md` → Milestones (M2.1 debts → M3 card parity → M4 dictionaries and settings → M5 AI → M6 Windows).
