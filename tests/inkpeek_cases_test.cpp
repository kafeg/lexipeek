// InkPeek's device cases (app/src/androidTest/assets/cases.tsv) through LexiPeek's core, on the packs in LEXIPEEK_MODELS.
// A case whose packs are not there is skipped; "?kind" is a known gap of the data (reported, does not fail).
#include <doctest/doctest.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include "dict/library.hpp"
#include "lexipeek/core.hpp"
#include "text/text.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> fields(const std::string &s) {
  std::vector<std::string> out;
  std::stringstream in(s);
  for (std::string x; std::getline(in, x, '\t');) out.push_back(x);
  return out;
}

std::vector<std::string> any_of(const std::string &s) {
  std::vector<std::string> out;
  for (size_t at = 0;;) {
    auto i = s.find(" | ", at);
    out.push_back(s.substr(at, i == std::string::npos ? std::string::npos : i - at));
    if (i == std::string::npos) return out;
    at = i + 3;
  }
}

}  // namespace

// InkPeek's cases come from ARM devices; x86-64's int8 kernels (intgemm) round differently, and these translations pick
// another word there (the same model: en→de "Where is the station?" gives "Station", not "Bahnhof"). Reported as gaps.
const std::set<std::string> x86_gaps = {"Где находится вокзал?"};

TEST_CASE("InkPeek's device cases") {
  const char *v = std::getenv("LEXIPEEK_MODELS");  // NOLINT(concurrency-mt-unsafe)
  if (!v || !*v) {
    MESSAGE("skipped: LEXIPEEK_MODELS is not set");
    return;
  }
  fs::path data(v);
  if (!fs::exists(data / "dicts")) {
    MESSAGE("skipped: no dictionaries in LEXIPEEK_MODELS");
    return;
  }
  CoreOptions o;
  o.data = data;
  Core core(o);
  dict::Library lib(data / "dicts", data / "index");
  std::ifstream f(LEXIPEEK_INKPEEK "/app/src/androidTest/assets/cases.tsv");
  REQUIRE(f);
  int run = 0, skipped = 0, gaps = 0, failed = 0;
  for (std::string line; std::getline(f, line);) {
    if (line.empty() || line[0] == '#') continue;
    auto c = fields(line);
    if (c.size() < 4) continue;
    bool gap = c[0].starts_with("?");
    auto kind = gap ? c[0].substr(1) : c[0];
    gap = gap || (kind == "tr" && cpu_arch() == "x86_64" && x86_gaps.contains(c[2]));
    auto &pair = c[1], &input = c[2];
    auto from = pair.substr(0, pair.find('-')), to = pair.substr(pair.find('-') + 1);
    std::string actual;
    if (kind == "head" || kind == "mean") {
      if (!fs::exists(data / "dicts" / ("dict-" + pair))) { skipped++; continue; }
      actual = "(not found)";
      for (auto &[d, hits] : lib.lookup(input, to))
        if (d->ifo().parent_path().filename() == "dict-" + pair) {
          for (auto &h : hits)
            if (auto l = dict::head_line(h.html)) {
              dict::Head hd{h.word, *l, ""};
              actual = kind == "head" ? l->substr(0, l->find(" — ")) : hd.meaning();
              break;
            }
          break;
        }
    } else if (kind == "lang") {
      if (!c[3].starts_with("!") && !fs::exists(data / "dicts" / ("dict-" + c[3] + "-" + pair))) { skipped++; continue; }
      auto g = core.language(input, pair);
      actual = g.lang.empty() ? "!" + g.other : g.lang;
    } else if (kind == "sep") {
      if (!fs::exists(data / "dicts" / ("dict-" + pair))) { skipped++; continue; }
      std::vector<std::string> verbs;
      for (auto &[s, h] : lib.separable(input, to)) verbs.push_back(s.verb);
      std::sort(verbs.begin(), verbs.end());
      for (auto &x : verbs) actual += (actual.empty() ? "" : ", ") + x;
      if (actual.empty()) actual = "(none)";
    } else if (kind == "tr") {
      auto t = core.translator().why_not(from, to, Method::fast);
      if (!t.empty()) { skipped++; continue; }
      auto r = core.translator().translate(from, to, input, Method::fast);
      actual = r.ok() ? r.text : "ERROR " + r.error;
    } else {
      continue;
    }
    bool ok = false;
    for (auto &e : any_of(c[3])) {
      if (kind == "head") ok |= actual.starts_with(e);
      else if (kind == "lang" || kind == "sep") ok |= actual == e;
      else ok |= text::lower(actual).find(text::lower(e)) != std::string::npos;
    }
    run++;
    if (!ok && gap) gaps++;
    if (!ok && !gap) {
      failed++;
      MESSAGE("FAIL " << kind << " " << pair << " " << input << ": want " << c[3] << ", got " << actual);
    }
  }
  MESSAGE(run << " cases run, " << skipped << " skipped (packs not installed), " << gaps << " known gaps, " << failed << " failed");
  CHECK(failed == 0);
  CHECK(run > 50);
}

TEST_CASE("the card on the packs: the text's language first, one entry per dictionary, articles, sections") {
  const char *v = std::getenv("LEXIPEEK_MODELS");  // NOLINT(concurrency-mt-unsafe)
  if (!v || !*v || !fs::exists(fs::path(v) / "dicts/dict-de-ru") || !fs::exists(fs::path(v) / "dicts/dict-en-ru")) {
    MESSAGE("skipped: no dict-de-ru and dict-en-ru in LEXIPEEK_MODELS");
    return;
  }
  CoreOptions o;
  o.data = v;
  Core core(o);
  auto c = core.lookup({"Haus", Source::cli, "ru"});
  CHECK(c.lang == "de");
  // Only the packs' entries: the data dir may hold own dictionaries too.
  std::vector<int> packs;
  for (size_t i = 0; i < c.entries.size(); i++)
    if (c.entries[i].dictionary.starts_with("InkPeek · ")) packs.push_back(static_cast<int>(i));
  REQUIRE(!packs.empty());
  // English "hausen" (en → ru) is not a German word's entry.
  for (int i : packs) CHECK(c.entries[static_cast<size_t>(i)].dictionary == "InkPeek · de → ru");
  CHECK(packs.size() == 1);
  int at = packs[0];
  auto &e = c.entries[static_cast<size_t>(at)];
  CHECK(e.word == "Haus");
  REQUIRE(!e.articles.empty());
  REQUIRE(e.articles[0].senses.size() > 2);
  CHECK(e.articles[0].senses[0].text.find("дом") != std::string::npos);
  CHECK(!e.articles[0].senses[0].examples.empty());
  CHECK(e.articles[0].senses[0].examples[0].starts_with("◆ "));
  CHECK(e.related.rel.size() > 20);
  CHECK(c.rank > 0);
  auto p = core.section(c, at, "rel", 1);
  CHECK(p.key == "rel");
  CHECK(p.word == "Haus");
  CHECK(p.page == 1);
  CHECK(p.first == 20);
  CHECK(p.items.size() == 20);
  CHECK(p.total == static_cast<int>(e.related.rel.size()));
  CHECK(std::any_of(p.items.begin(), p.items.end(), [](auto &i) { return !i.meaning.empty(); }));
  CHECK(core.section(c, at, "forms", 0).items.size() == e.related.forms.size());
  CHECK(core.section(c, static_cast<int>(c.entries.size()), "rel", 0).key.empty());
  // An inflected form: its base form's header.
  auto f = core.lookup({"Häusern", Source::cli, "ru"});
  CHECK(f.base == "Haus");
  CHECK(f.header.starts_with("das Haus"));
}

TEST_CASE("the gloss says how many words it left out") {
  const char *v = std::getenv("LEXIPEEK_MODELS");  // NOLINT(concurrency-mt-unsafe)
  if (!v || !*v || !fs::exists(fs::path(v) / "dicts/dict-de-ru")) {
    MESSAGE("skipped: no dict-de-ru in LEXIPEEK_MODELS");
    return;
  }
  CoreOptions o;
  o.data = v;
  Core core(o);
  std::string text = "Der Hund";
  for (int i = 0; i < 70; i++) text += " wort" + std::string{static_cast<char>('a' + i / 26), static_cast<char>('a' + i % 26)};
  auto c = core.lookup({text, Source::cli, "ru"});
  CHECK(c.gloss.size() == 60);
  CHECK(c.more == 12);
  CHECK(dict::gloss_words("Der der DER Hund, hund.") == 2);
}
