# TODO

Done on macOS: M0–M5 (engines, core, the app, card parity, dictionaries and settings, AI), the LexiPeek window, Notes
with TSV / Export / Import. Next: M6 Windows (Qt). Milestones: `docs/spec.md` → Milestones.

## Before the Windows shell (logic still in Swift that Qt would have to write again)
- The card's navigation (the trail of cards, "←" from a section / a card, what is asked again) is in `CardModel.swift`:
  move it to the core as pure transitions (`card_open`, `card_back`, `card_pick_method`).
- Settings → Packs builds its sections and actions in `PacksView.swift`: a core `packs_view` (rows with their actions).
- `PairRow::direct/llm` are `std::optional` in the facade (Bridge readers): make them a plain list.
- Paths from UTF-8 strings (`app.cpp`, `http.cpp`, `unpack`) read the system code page on Windows: one UTF-8 path helper.
- The device's CPU features: `app::device` knows only ARM's "asimddp"; each platform gives its own list.
- Download progress polling is written four times in Swift (Packs, method cards, first run, catalog): one helper.
- Small helpers repeated in the core: trimming, joining with ", ", sizes (`size_text`, `mb`, the `gb` lambda), the day.

## Open
- CI on Linux / Windows not run since M0 (macOS first); CLI providers (`claude -p`, `codex exec`) have no timeout and
  their shell line is POSIX only.
- No request sent to a real AI provider from here: try each once.
- Indexes of own dictionaries are built on the look-up queue (`Core::prepare` holds the library for seconds at launch
  and after a packs change; a card waits); no "indexing…" state.
- `Library::all()` rescans the folders and `translate()` re-reads packs.json on every call (~1 ms now; grows with
  folders).
- An open card's ★ is not refreshed when Notes removes its word; History lists all 1,000 at once.
- An entry's line is not clickable to its own article (Full entry opens all).
- Check the selection and ⌥D's position (AX bounds) in Safari, Chrome, Firefox, Vivaldi and native apps.
- OPUS-MT on an 8 GB Mac: its working set (1.2 GB) and peak (2 GB) — check `mini-batch-words`.
- Translations of the UI not reviewed by native speakers; low-level errors (libcurl, libarchive) stay English.
- Help and the theme (InkPeek's).

## Kept as InkPeek, by decision
- Unknown text ("+++ 42 ### 17", made-up words) gets fastText's best book language when no dictionary knows a word:
  detecting "not a language" would misfire on real text.
- A separable verb's finite form is listed again on its own ("stand … auf" and "stand: stehen"), as InkPeek's gloss.
- dict-de-ru's headword "ist" ("ист") wins over the form of *sein* in the gloss: InkPeek's data, fix it there.
