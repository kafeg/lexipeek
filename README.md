# LexiPeek

**Look up and translate any text on your Mac, offline.**

Select a word or a sentence in any app and press a hotkey. A small card opens next to the cursor: the dictionary
entry with its grammar ("das Haus (Hauses, pl. Häuser)"), the base form of an inflected word, a word-by-word gloss of
a sentence, and a translation made on your Mac — no server, no account.

Translation methods: fast Mozilla models, direct OPUS-MT models, or the Hy-MT2 AI translation model, all running
locally. Dictionaries, grammar and models come from the language packs of
[InkPeek](https://github.com/kafeg/inkpeek), the same project's pop-up dictionary for Android e-ink readers.

**Status:** early development, nothing to install yet. The plan is in [docs/spec.md](docs/spec.md).
macOS 14+ on Apple Silicon first; a browser extension, Windows and Linux later.

## License
MIT. Language packs keep their own licenses (Wiktionary CC BY-SA, Mozilla MPL-2.0, OPUS-MT CC BY 4.0,
Hy-MT2 Apache-2.0).
