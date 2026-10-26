// The one entry for every shell: a request → the card (dictionary first, at once), then its translation (slow).
#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
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
};

// One dictionary's part of the card: the short line and the whole article.
struct Entry {
  std::string dictionary;
  std::string line;
  std::string article;
};

// A word of a sentence, word by word (separable verbs as one: "fängt … an" → "anfangen").
struct Gloss {
  std::string word, base, meaning;
};

struct MethodState {
  Method method = Method::fast;
  std::string name;    // "Fast · Mozilla", "Direct · OPUS-MT", "AI model · Hy-MT2 1.8B"
  bool available = false;
  std::string reason;  // why not, when not available
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
  std::string lang_name, answer_name, other_name;  // English names ("German"), "" when unknown
  std::string header;  // "das Haus (Hauses, pl. Häuser)"
  std::string base;    // the base form of an inflected word ("Träumen" → "Traum")
  std::vector<Entry> entries;
  std::vector<Gloss> gloss;
  std::vector<Gloss> separable;  // separable verbs written apart: word "mache … zu", base "zumachen"
  int rank = 0;                  // the word's rank in its language's frequency list (0: not in it)
  std::int64_t looks = 0;        // looks of the word here, across its forms (this one included)
  std::vector<MethodState> methods;
  std::optional<Method> method;  // the one to translate with (the setting, else the first available)
  std::string error;
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
};

class Core {
 public:
  explicit Core(CoreOptions o);
  ~Core();
  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;

  // Changed settings (answer language, method, the English in between) and, when given, this machine: the next cards use them.
  void configure(const Settings &s, const std::optional<Device> &device = std::nullopt);
  // Fast (no model loads): language, dictionary, gloss, methods.
  Card lookup(const Request &r);
  LanguageGuess language(const std::string &text, const std::string &answer);
  // Slow: the card's text with a method (default: card.method); kept in the store, so it comes back at once.
  // Fast through English: the English is kept as the text's translation into English (InkPeek's), shown when asked.
  Translation translate(const Card &card, std::optional<Method> m = std::nullopt);
  // A newer request: stops a running AI translation.
  void cancel();
  // Memory pressure, idle: frees the held model.
  void release();
  // The newest looks, one per word (the menu's Recents).
  std::vector<Seen> recents(int n);
  Store &store();
  // Builds the dictionaries' indexes and loads language id: at launch, off the main thread (slow once per pack).
  void prepare();
  // The packs changed on disk (installed, removed): look at the dictionaries again, free the held models.
  void reload();
  Translator &translator();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lexipeek
