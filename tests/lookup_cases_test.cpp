// InkPeek's selection cases (tests/lookup, docs/formats.md "Selection and lookup"): kind, title, query and the words a card shows.
#include <doctest/doctest.h>
#include <fstream>
#include <sstream>
#include "temp.hpp"
#include "dict/library.hpp"
#include "lexipeek/core.hpp"
#include "text/text.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

namespace {

void be32(std::string &s, uint32_t v) {
  for (int i = 3; i >= 0; i--) s += static_cast<char>((v >> (i * 8)) & 0xff);
}

std::vector<std::string> split(const std::string &s, char sep) {
  std::vector<std::string> out;
  std::stringstream in(s);
  for (std::string x; std::getline(in, x, sep);) out.push_back(x);
  if (!s.empty() && s.back() == sep) out.emplace_back();
  return out;
}

// A data dir with one own dictionary of [words], each article "<p class="head">word — x</p>".
fs::path data_of(const std::vector<std::string> &words, int n) {
  auto d = fs::temp_directory_path() / ("lexipeek-lookup-cases-" + std::to_string(n));
  wipe(d);
  auto dir = d / "dicts-own" / "d";
  fs::create_directories(dir);
  fs::create_directories(d / "dicts");
  auto sorted = words;
  std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) { return text::norm_key(a) < text::norm_key(b); });
  std::string idx, dict;
  for (auto &w : sorted) {
    auto a = "<p class=\"head\">" + w + " — x</p>";
    idx += w + '\0';
    be32(idx, static_cast<uint32_t>(dict.size()));
    be32(idx, static_cast<uint32_t>(a.size()));
    dict += a;
  }
  std::ofstream(dir / "d.idx", std::ios::binary) << idx;
  std::ofstream(dir / "d.dict", std::ios::binary) << dict;
  std::ofstream(dir / "d.ifo") << "StarDict's dict ifo file\nversion=3.0.0\nbookname=d\nwordcount=" << sorted.size()
                               << "\nidxfilesize=" << idx.size() << "\nsametypesequence=h\n";
  return d;
}

}  // namespace

TEST_CASE("InkPeek's selection cases") {
  static const char *kinds[] = {"empty", "word", "phrase", "passage"};
  int n = 0, failed = 0;
  for (auto file : {"selection.tsv", "dashes.tsv"}) {
    std::ifstream f(std::string(LEXIPEEK_INKPEEK "/tests/lookup/") + file);
    REQUIRE(f);
    for (std::string line; std::getline(f, line);) {
      if (line.empty() || line[0] == '#') continue;
      auto c = split(line, '\t');
      c.resize(6);
      std::vector<std::string> known;
      for (auto &w : split(c[0], '|'))
        if (!w.empty()) known.push_back(w);
      auto data = data_of(known, n++);
      CoreOptions o;
      o.data = data;
      o.answer = "ru";
      std::string kind, title, query, words;
      {
        Core core(o);
        auto card = core.lookup({c[1], Source::cli, "ru"});
        kind = kinds[static_cast<int>(card.kind)], title = card.title, query = card.text;
      }
      dict::Library lib(data / "dicts", data / "index-cases", dict::Own{{data / "dicts-own"}, {}, {}});
      std::vector<std::string> shown;
      if (kind == "word")
        for (auto &[d, hits] : lib.lookup(query, "ru")) shown.push_back(hits.front().word);
      else
        for (auto &g : lib.gloss(query, "ru")) shown.push_back(g.word + (g.head ? "" : "?"));
      words = text::join(shown, "|");
      std::vector<std::string> got{kind, title, query, words}, want{c[2], c[3], c[4], c[5]};
      if (got != want) {
        failed++;
        MESSAGE(file << ": " << c[1] << " → " << text::join(got, " / ") << ", expected " << text::join(want, " / "));
      }
      wipe(data);
    }
  }
  CHECK(failed == 0);
}
