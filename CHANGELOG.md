# Changelog

## 0.2 — unreleased
The first release: macOS 14+ (Apple Silicon) and Windows 10 (version 1903 or later) or 11, x64. Linux is not in this
release. 0.1.351 was a pre-release of it.
- Select text in any app and press ⌥D (Win+Shift+D on Windows): a card at the selection with the dictionary entry,
  its grammar and the base form of an inflected word, a phrase word by word, and an offline translation. ⌥A
  (Win+Shift+A): a card to type in.
- The card draws a thin line between separate results — each dictionary, each word of a phrase, each section — as
  InkPeek does.
- InkPeek's language packs: dictionaries with grammar from Wiktionary, Mozilla, OPUS-MT and Hy-MT2 translation, all on
  the computer; the AI model on the GPU (Metal on a Mac, Vulkan on Windows) when there is one.
- Own StarDict dictionaries and folders, and a catalog of free ones.
- The language you learn: text already in your own language opens in it — its dictionary lines with each word's own
  grammar and frequency, and its translation.
- Read aloud: a word or a sentence in a natural voice (female or male), made on the computer; your own voice from a
  short recording.
- Optional AI with your own key or a local server: AI translation, Analysis, Context.
- Notes: ☆ words and phrases, History, backup and restore; synced with InkPeek through a folder, and into Anki by
  themselves through AnkiConnect, each note with its reading (Settings → Sync).
- The interface in English, Russian, German, French, Spanish, Italian, Portuguese and Polish.

Since the pre-release 0.1.351:
- "Keep the model in memory" sits next to the model it keeps: the translation model's in Settings → Languages →
  Translation, the voice model's in Settings → Read aloud.
- A long selection's card opens in under a second (a very long one took up to a minute); the menu-bar icon appears at
  once at launch; Notes and look-ups no longer wait while another device's notes are read; Anki's notes are prepared
  only when something changed.
- Windows: a pack update no longer fails with "Cannot move" while LexiPeek runs; two AI requests through a command-line
  provider no longer hold each other up; messages from other programs are checked; a Windows older than 10 version 1903
  gets a clear message at start.
