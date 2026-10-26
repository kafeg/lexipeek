// The packs' dictionaries (as InkPeek's stardict/Library.kt, Separable.kt): lookup, header, gloss, separable verbs.
#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "dict/stardict.hpp"

namespace lexipeek::dict {

// A pack article's first line: "das Haus (Hauses, мн. Häuser) — дом, здание"; word: the headword.
struct Head {
  std::string word, line, book;
  std::string header() const;
  std::string meaning() const;
};

// The pack's first line of an article ("<p class=\"head\">…</p>") as text, if any.
std::optional<std::string> head_line(const std::string &html);

// A pack article's senses: "<ol><li>meaning<br><small>◆ example</small></li>…</ol>" as text.
struct Sense {
  std::string text;
  std::vector<std::string> examples;
};
std::vector<Sense> senses(const std::string &html);

// The text's distinct words (case aside), as the gloss counts them.
size_t gloss_words(const std::string &text);

struct GlossLine {
  std::string word;
  std::optional<Head> head;
};

struct SeparableFind {
  std::string form, particle, verb;
};

// Separable verbs written apart (Separable.kt): bases = the lemmas of a form, particle = a verb's particle or "".
std::vector<SeparableFind> find_separable(const std::string &text, const std::function<std::vector<std::string>(const std::string &)> &bases,
                                          const std::function<std::string(const std::string &)> &particle);

class Library {
 public:
  // packs: <data>/dicts (dict-<book>-<answer>/ folders); work: <data>/index.
  Library(std::filesystem::path packs, std::filesystem::path work);
  ~Library();

  // The dictionaries on disk (scanned again when the folder changed).
  std::vector<StarDict *> all();
  // Hits per dictionary: the packs from book into answer (book "": any) and the other dictionaries; then the other packs
  // into answer; the rest only when none of those knows the word.
  std::vector<std::pair<StarDict *, std::vector<Hit>>> lookup(const std::string &query, const std::string &answer, const std::string &book = "");
  // The word's header, one per book language, book's first.
  std::vector<Head> heads(const std::string &query, const std::string &answer, const std::string &book = "");
  std::vector<GlossLine> gloss(const std::string &text, const std::string &answer, size_t limit = 60, const std::string &book = "");
  std::vector<std::pair<SeparableFind, std::optional<Head>>> separable(const std::string &text, const std::string &answer);
  int rank(const std::string &word, const std::string &book);
  // Whether the dictionary of book → answer knows the word (its forms too).
  bool knows(const std::string &book, const std::string &answer, const std::string &word);

 private:
  std::vector<std::pair<StarDict *, std::vector<Hit>>> lookup(const std::string &query, const std::string &answer,
                                                              const std::vector<StarDict *> &among, const std::string &book);
  std::filesystem::path packs_, work_;
  std::string stamp_;
  std::vector<std::unique_ptr<StarDict>> dicts_;
};

}  // namespace lexipeek::dict
