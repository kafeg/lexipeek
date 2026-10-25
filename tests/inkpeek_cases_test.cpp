// InkPeek's device cases (app/src/androidTest/assets/cases.tsv) through LexiPeek's core, on the packs in LEXIPEEK_MODELS.
// A case whose packs are not there is skipped; "?kind" is a known gap of the data (reported, does not fail).
#include <doctest/doctest.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
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
