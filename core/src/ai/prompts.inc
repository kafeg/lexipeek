// InkPeek's Ask (ai/Ai.kt), word for word: the answers' versions are the hashes of these prompts.
{"Answer with one JSON object, these keys only:", {
  {"base", "s", "the dictionary form in the text's own language (Häusern → Haus, went → go, ging → gehen); \"\" for a phrase or passage.", false},
  {"article", "s", "the article of the base that shows its gender where the language has one (German der/die/das, French le/la, Spanish el/la…); \"\" otherwise or for non-nouns.", false},
  {"plural", "s", "a noun's plural in the text's language, with the article for German (die Häuser); else \"\".", false},
  {"pos", "s", "part of speech in {lang}, short; for a noun add the gender of the ORIGINAL word, not of its translation.", false},
  {"forms", "s", "a verb's principal parts in the text's language — German: infinitive · 3rd person singular present · Präteritum · Perfekt with its auxiliary (\"gehen · geht · ging · ist gegangen\"); English: \"go · went · gone\"; an adjective with irregular comparison: \"gut · besser · am besten\"; else \"\".", false},
  {"irregular", "b", "true for an irregular or strong verb (or irregular comparison), else false.", false},
  {"form_of", "s", "when the selected word is an inflected form, which form, in {lang} (plural, dative; past tense…); else \"\".", false},
  {"meanings", "a", "1–3 main meanings in {lang}, a few words each; [] for a phrase or passage.", false},
  {"translation", "s", "a phrase or passage: a natural translation into {lang} that keeps the sentence type (a question stays a question); \"\" for a single word.", false},
}, "Grammar, examples and comments do not belong here: other tabs give them.", 1024},
{"Answer with one JSON object, these keys only; text in {lang} with light Markdown (**bold**, *italic*, \"- \" lists), no headings: the app adds them.", {
  {"grammar", "s", "at most 7 short lines, the words you talk about in **bold**. A single word: which form of which **base form** it is, how it is built, and the forms a learner of this language needs to use it correctly. A phrase: how it is built and why — the constructions that matter in this language (word order, agreement, which word requires which form, tense, aspect or mood) and the rule behind them. A passage: every construction that matters, up to 10 lines.", false},
  {"words", "a", "a phrase or a passage: the words a learner may not know, \"**base form** — short meaning\" each (at most 20 for a passage), very common words skipped; a single word: its **base form**, 2–3 synonyms and related words, each \"**word** — short meaning\".", false},
  {"related", "a", "words of the same root in the text's own language, the most useful first (German: prefixed verbs like anmachen, zumachen; English: phrasal verbs, derived words), at most 8, \"**word** — short meaning\" each; [] for a passage.", true},
  {"synonyms", "a", "synonyms, then antonyms marked \"≠\", in the text's own language, at most 6, \"**word** — how it differs\" each; [] for a passage.", true},
}, "", 3000},
{"Answer with one JSON object, these keys only; text in {lang} with light Markdown (**bold**, *italic*), no headings: the app adds them.", {
  {"explanation", "s", "what it means here, register, idioms and nuances; a passage: what happens and what is implied, briefly.", false},
  {"examples", "e", "a word or phrase: 3 short everyday sentences in the text's own language with it in **bold**, each with its translation into {lang}; [] for a passage.", false},
  {"idioms", "e", "set expressions, idioms and proverbs with the word in the text's own language, the common ones, at most 5, with **the word** in bold, each with its meaning in {lang}; [] for a passage.", true},
  {"origin", "s", "where the word comes from, in 1–3 sentences, and a memorable link to a word the reader may know; \"\" for a passage.", true},
}, "", 3000},
