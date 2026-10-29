// A StarDict 3 dictionary (as InkPeek's stardict/StarDict.kt): .ifo, .idx, .dict[.dz], .syn, a pack's .rel.
#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace lexipeek::dict {

// One headword hit: the word as the dictionary writes it, its article as HTML.
struct Hit {
  std::string word, html;
};

// A pack's .rel line (see InkPeek's docs/formats.md): related words, forms, rank, a separable verb's particle.
struct Related {
  std::vector<std::string> rel, syn, ant, expr, ipa;
  std::string origin;
  std::vector<std::pair<std::string, std::string>> forms;  // form → tags
  int rank = 0;
  std::string sep;
};

class StarDict {
 public:
  // work: where its own sorted index lives (never the dictionary's folder); pack: a language pack's (its languages from its folder).
  StarDict(const std::filesystem::path &ifo, const std::filesystem::path &work, bool pack = true);
  ~StarDict();

  const std::string &name() const { return name_; }
  const std::filesystem::path &ifo() const { return ifo_; }
  const std::string &id() const { return id_; }
  // A pack's languages from its folder dict-<book>-<answer>; "" for other dictionaries.
  const std::string &book() const { return book_; }
  const std::string &answer() const { return answer_; }
  bool pack() const { return pack_; }
  // The .ifo's wordcount, 0 when it has none.
  long words() const { return words_; }
  // Its own index is built (a lookup needs no build first).
  bool ready();

  // Builds the index when it is missing or from other files (once; slow for a big dictionary).
  void prepare();
  // The entries for a word (its forms through .syn), the best 8, best first.
  std::vector<Hit> lookup(const std::string &query);
  std::optional<Related> related(const std::string &word);

 private:
  struct Entry {
    std::string word;
    uint64_t off;
    uint32_t size;
  };
  std::string stamp() const;
  void build();
  void load();
  std::string key_at(size_t i) const;
  Entry entry_at(size_t i) const;
  std::string read(uint64_t off, uint32_t size);

  std::filesystem::path ifo_, base_, work_;
  std::string name_, id_, book_, answer_, types_;
  bool offset64_ = false, pack_ = true;
  long words_ = 0;
  std::mutex mu_;
  std::string index_;           // the index file: "IPK2", count, offsets, blob
  size_t count_ = 0, blob_ = 0;
  struct Data;
  std::unique_ptr<Data> data_;
  // The .rel file, kept open (a passage asks it for every word): read by blocks, not by characters.
  std::mutex rel_mu_;
  std::ifstream rel_;
  uint64_t rel_len_ = 0;
  bool rel_tried_ = false;
};

// Every .ifo under root.
std::vector<std::filesystem::path> scan(const std::filesystem::path &root);

}  // namespace lexipeek::dict
