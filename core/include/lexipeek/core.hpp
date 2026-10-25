// The one entry for every shell: a request → the card (dictionary first, at once), then its translation (slow).
#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "lexipeek/mt.hpp"

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
  enum class Kind { empty, word, phrase };
  Kind kind = Kind::empty;
  std::string text;    // as asked, trimmed
  std::string lang;    // the text's language, "" = unknown
  std::string other;   // a language named by language id that has no pack
  std::string answer;  // the answer language
  std::string header;  // "das Haus (Hauses, pl. Häuser)"
  std::string base;    // the base form of an inflected word ("Träumen" → "Traum")
  std::vector<Entry> entries;
  std::vector<Gloss> gloss;
  std::vector<Gloss> separable;  // separable verbs written apart: word "mache … zu", base "zumachen"
  int rank = 0;                  // the word's rank in its language's frequency list (0: not in it)
  std::vector<MethodState> methods;
  std::optional<Method> method;  // the one to translate with (the setting, else the first available)
  std::string error;
};

// The text's language among the packs' book languages into an answer language; other: named, without a pack.
struct LanguageGuess {
  std::string lang, other;
};

struct CoreOptions {
  std::filesystem::path data;  // packs, settings, logs
  std::string answer = "ru";
  std::optional<Method> method;  // the preferred method; unavailable → the first available
  TranslatorOptions translator;  // data is filled from [data]
};

class Core {
 public:
  explicit Core(CoreOptions o);
  ~Core();
  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;

  // Fast (no model loads): language, dictionary, gloss, methods.
  Card lookup(const Request &r);
  LanguageGuess language(const std::string &text, const std::string &answer);
  // Slow: the card's text with a method (default: card.method).
  Translation translate(const Card &card, std::optional<Method> m = std::nullopt);
  // Memory pressure, idle: frees the held model.
  void release();
  // The packs changed on disk (installed, removed): look at the dictionaries again.
  void reload();
  Translator &translator();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lexipeek
