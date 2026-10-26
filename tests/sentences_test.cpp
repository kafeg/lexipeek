// Sentence breaks: Unicode's own test file and InkPeek's cases (tests/sentences, checked on its devices).
#include <doctest/doctest.h>
#include <fstream>
#include <sstream>
#include "mt/text.hpp"

using namespace lexipeek;

namespace {

std::vector<std::string> split(const std::string &s, char d) {
  std::vector<std::string> out;
  std::string x;
  std::istringstream in(s);
  while (std::getline(in, x, d)) out.push_back(x);
  if (!s.empty() && s.back() == d) out.emplace_back();
  return out;
}

std::string unescape(std::string s) {
  for (size_t at = 0; (at = s.find("\\n", at)) != std::string::npos; at++) s.replace(at, 2, "\n");
  return s;
}

}  // namespace

TEST_CASE("UAX #29: SentenceBreakTest.txt") {
  std::ifstream f(LEXIPEEK_TESTDATA "/SentenceBreakTest.txt");
  REQUIRE(f);
  int cases = 0;
  for (std::string line; std::getline(f, line);) {
    line = line.substr(0, line.find('#'));
    if (line.find_first_not_of(" \t") == std::string::npos) continue;
    std::u32string text;
    std::vector<size_t> want;
    std::istringstream in(line);
    for (std::string tok; in >> tok;) {
      if (tok == "÷") want.push_back(text.size());
      else if (tok != "×") text.push_back(static_cast<char32_t>(std::stoul(tok, nullptr, 16)));
    }
    CAPTURE(line);
    CHECK(mt::sentence_breaks(text) == want);
    cases++;
  }
  CHECK(cases > 400);
}

TEST_CASE("InkPeek's sentence cases") {
  std::ifstream f(LEXIPEEK_INKPEEK "/tests/sentences/sentences.tsv");
  REQUIRE(f);
  int cases = 0;
  for (std::string line; std::getline(f, line);) {
    if (line.empty() || line[0] == '#') continue;
    auto c = split(line, '\t');
    REQUIRE(c.size() >= 3);
    std::vector<std::string> want;
    for (size_t i = 2; i < c.size(); i++) want.push_back(unescape(c[i]));
    CAPTURE(line);
    CHECK(mt::sentences(unescape(c[1])) == want);
    cases++;
  }
  CHECK(cases >= 40);
}
