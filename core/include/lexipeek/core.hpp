// The one entry for every shell: a request → the card (dictionary first, at once), then its translation (slow).
#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "lexipeek/ai.hpp"
#include "lexipeek/mt.hpp"
#include "lexipeek/packs.hpp"
#include "lexipeek/settings.hpp"
#include "lexipeek/store.hpp"

namespace lexipeek {

enum class Source { hotkey, input, service, url, extension, cli };

struct Request {
  std::string text;
  Source source = Source::input;
  std::string answer;  // the answer language ("ru"); "" = the settings' one
  std::string lang = "";  // the text's language picked in the card ("" = guessed)
};

// A sense of a pack's article: its meaning and its examples ("◆ Ich gehe nach Hause — Я иду домой.").
struct Sense {
  std::string text;
  std::vector<std::string> examples;
};

// One article of a dictionary: its headword, its first line, its senses (a pack's; empty for other dictionaries).
struct Article {
  std::string word, line;
  std::vector<Sense> senses;
};

// A pack's word family and more for a headword (its .rel, as InkPeek's Related.Entry): frequent first.
struct Related {
  std::vector<std::string> rel, syn, ant, expr, ipa;
  std::string origin;
  std::vector<std::pair<std::string, std::string>> forms;  // form → tags
};

// One dictionary's part of the card: the short line and the whole article.
struct Entry {
  std::string dictionary;
  std::string line;
  std::string article;
  std::string id = "";            // the dictionary's id (its sections are read from it)
  std::string word = "";          // the headword of its first article
  std::vector<Article> articles = {};  // its articles, parsed when they are a pack's
  Related related = {};           // the first headword's sections (packs only)
};

// A word of a sentence, word by word (separable verbs as one: "fängt … an" → "anfangen").
struct Gloss {
  std::string word, base, meaning;
  std::string header = "";  // the base form's header: "der Hund (Hundes, мн. Hunde)", "" when unknown
};

// A section of an entry opened in the card (InkPeek's DictionaryPage.section): words with their first meaning from the
// same dictionary, 20 per page; forms with their tags; the origin as text. key "" = none open.
struct SectionPage {
  std::string key;  // rel, syn, expr, forms, ety
  std::string word;
  int entry = 0;  // the card's entry it belongs to
  int page = 0, pages = 0, total = 0, first = 0;  // first: the index of the page's first item
  std::vector<Gloss> items;  // word, its meaning ("≠ word" for an antonym)
  std::string text;          // forms: the IPA; ety: the origin
};

struct MethodState {
  Method method = Method::fast;
  std::string name;    // "Fast · Mozilla", "Direct · OPUS-MT", "AI model · Hy-MT2 1.8B"
  bool available = false;
  std::string reason;  // why not, when not available
};

// What the card offers of the AI layer (the settings when it was looked up): each only when on.
struct CardAi {
  bool translate = false;  // AI translation, a Translate method
  bool analysis = false, context = false;  // the AI tabs
  bool prefetch = true;    // opening one asks the others too
  std::string method;      // "AI · claude-sonnet-5-5"
  bool key = false;        // its provider needs an API key
  bool no_key = false;     // and the shell has none: AI is offered, never opened by itself
};

struct Card {
  // As InkPeek's Kind.of: a word; a phrase (up to 8 words, one sentence); a passage (more).
  enum class Kind { empty, word, phrase, passage };
  Kind kind = Kind::empty;
  Source source = Source::input;
  std::string text;    // as asked, trimmed
  std::string lang;    // the text's language, "" = unknown
  std::string other;   // a language named by language id that has no pack
  std::string answer;  // the answer language
  std::string answer_name, other_name;  // English names ("German"), "" when unknown
  std::string header;  // "das Haus (Hauses, pl. Häuser)"
  std::string base;    // the base form of an inflected word ("Träumen" → "Traum")
  std::vector<Entry> entries;
  std::vector<Gloss> gloss;
  std::vector<Gloss> separable;  // separable verbs written apart: word "mache … zu", base "zumachen"
  int more = 0;                  // the text's words past the gloss's limit
  int rank = 0;                  // the word's rank in its language's frequency list (0: not in it)
  std::int64_t looks = 0;        // looks of the word here, across its forms (this one included)
  // What ☆ saves (InkPeek's saveKey): a word as its base form, also after its article ("das Haus" → "Haus"), else the text.
  std::string save_text, save_kind;  // save_kind: "word" / "phrase"
  std::string save_line;             // the dictionary's first line of that word, "" for a phrase
  bool saved = false;                // saved as save_text or as the text itself
  std::vector<MethodState> methods;
  std::optional<Method> method;  // the one to translate with (the setting, else the first available)
  std::string error;
  CardPrefs prefs = {};  // the card's settings when it was looked up (what it opens on, shows, its look)
  CardAi ai = {};
  // The packs' languages for the badge's menu: the texts' into answer, the answers' (codes).
  std::vector<std::string> books = {}, answers = {};
  bool mt_off = false;  // offline translation is off: Translate is AI only
  bool no_packs = false;  // no language pack installed: the card says so and opens Settings → Packs
};


// The version an offline answer is kept under, as InkPeek's OfflineTranslation: Java's hashCode of the
// installed models' sha256 (fast: the steps' model-* joined by ", "; direct, llm: the covering component's).
std::int64_t answer_version(const Manifest &m, const std::map<std::string, std::string> &installed, const std::string &from,
                            const std::string &to, Method method);

// The text's language among the packs' book languages into an answer language; other: named, without a pack.
struct LanguageGuess {
  std::string lang, other;
};

struct CoreOptions {
  std::filesystem::path data;  // packs, settings, logs
  std::string answer = "ru";
  std::optional<Method> method;  // the preferred method; unavailable → the first available
  TranslatorOptions translator;  // data is filled from [data]
  std::optional<Device> device;  // this machine (from the shell); none: not checked
  bool mt_english = false;       // Translation::english filled (fast through English), as InkPeek's mtEnglish
  bool mt_off = false;           // no offline translation (the card has no methods)
  DictSettings dicts;            // the own dictionaries: folders, off, order
  CardPrefs card;                // copied into every card
  AiSettings ai;                 // the AI layer
  std::filesystem::path ai_replies;  // answers from files instead of a provider (snapshots, tests): ai/ai.hpp file_transport
};

class Core {
 public:
  explicit Core(CoreOptions o);
  ~Core();
  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;

  // Changed settings (answer language, method, the English in between, the card's, own dictionaries) and, when given, this machine: the next cards use them.
  void configure(const Settings &s, const std::optional<Device> &device = std::nullopt);
  // Fast (no model loads): language, dictionary, gloss, methods.
  Card lookup(const Request &r);
  LanguageGuess language(const std::string &text, const std::string &answer);
  // A section of the card's entry: page n (from 0) of its words with their meanings.
  SectionPage section(const Card &card, int entry, const std::string &key, int page);
  // Slow: the card's text with a method (default: card.method); kept in the store, so it comes back at once.
  // Fast through English: the English is kept as the text's translation into English (InkPeek's), shown when asked.
  Translation translate(const Card &card, std::optional<Method> m = std::nullopt, int tag = 0);
  // Blocking, only on the person's action: the card's text's AI answer, the saved one when there is (offline); key: the
  // provider's API key, from the shell (never kept here).
  ai::Answer ask(const Card &card, ai::Ask a, const std::string &key);
  // A newer request: stops a running AI translation (tag's only; 0: any).
  void cancel(int tag = 0);
  // Memory pressure, idle: frees the held model.
  void release();
  Translator::Memory memory() const;
  // ☆: saves the card's save_text with one short line (its translation, else the dictionary's), or removes it
  // (and the text itself, saved so before); → whether it is saved now.
  bool toggle_saved(const Card &card, const std::string &translation);
  // The newest looks, one per word (the menu's Recents).
  std::vector<Seen> recents(int n);
  Store &store();
  std::string answer();  // the answer language of the next cards
  // Builds the dictionaries' indexes and loads language id: at launch, off the main thread (slow once per pack).
  void prepare();
  // The packs changed on disk (installed, removed): look at the dictionaries again, free the held models.
  void reload();
  Translator &translator();
  std::filesystem::path data() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lexipeek
