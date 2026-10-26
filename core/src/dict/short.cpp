// Other dictionaries (imported, downloaded, a folder) have no pack first line: their first meaning instead (InkPeek's Short.kt).
#include <algorithm>
#include "dict/library.hpp"
#include "text/text.hpp"

namespace lexipeek::dict {

namespace {

// The first n code points.
std::string take(const std::string &s, size_t n) {
  size_t i = 0;
  for (size_t cps = 0; i < s.size(); i++)
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && cps++ == n) break;
  return s.substr(0, i);
}

size_t length(const std::string &s) {
  size_t n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

std::string trim(const std::string &s) {
  static const text::Regex edge("^\\s+|\\s+$");
  return edge.replace(s, "");
}

std::string before(const std::string &s, const std::string &mark) { return s.substr(0, s.find(mark)); }

std::string plain(const std::string &h) {
  static const text::Regex li("<li[^>]*>", true), br("<br\\s*/?>|</(p|li|div|h[1-6])>", true), tag("<[^>]+>");
  auto s = tag.replace(br.replace(li.replace(h, "\n• "), "\n"), "");
  for (auto [a, b] : {std::pair{"&nbsp;", " "}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&amp;", "&"}})
    for (size_t at = 0; (at = s.find(a, at)) != std::string::npos; at += std::string(b).size()) s.replace(at, std::string(a).size(), b);
  return s;
}

// Mueller-style marks: "_v." (part of speech), "1>" (sense number); the first sense only; "(full form)" is a note.
std::string clean(const std::string &s) {
  static const text::Regex lead("^(•|\\d+[.)]|[-–])\\s*"), pos("_\\p{L}+\\.?\\s*"), num("(^|\\s)\\d+>\\s*"), space("\\s+"), note("^\\([^)]*\\)\\s*");
  auto x = trim(space.replace(num.replace(pos.replace(lead.replace(before(s, "◆"), ""), ""), " "), " "));
  return take(trim(before(note.replace(x, ""), ";")), 90);
}

}  // namespace

std::string short_meaning(const std::string &html, const std::string &headword, const std::string &pos) {
  if (html.find("class=\"grammar\"") != std::string::npos) {
    static const text::Regex grammar("class=\"grammar\"[^>]*>([^<]+)<"), div("<div>([^<]+)</div>");
    auto marks = grammar.all(html);
    const text::Regex::Match *m = nullptr;
    if (!pos.empty())
      for (auto &x : marks)
        if (text::lower(x.groups[1]).find(text::lower(pos)) != std::string::npos) {
          m = &x;
          break;
        }
    if (!m && !marks.empty()) m = &marks.front();
    std::string part = html;
    if (m) {
      size_t end = html.size();
      for (auto &x : marks)
        if (x.begin > m->end - 1) {
          end = x.begin;
          break;
        }
      part = html.substr(m->end - 1, end - (m->end - 1));
    }
    std::vector<std::string> out;
    for (auto &d : div.all(part))
      if (auto t = trim(d.groups[1]); !t.empty() && std::find(out.begin(), out.end(), t) == out.end() && out.size() < 3) out.push_back(t);
    std::string joined;
    for (auto &t : out) joined += (joined.empty() ? "" : ", ") + t;
    if (!joined.empty()) return take(joined, 90);
  }
  // Wiktionary and most HTML dictionaries keep senses in <ol><li>; transcription and forms come before it in <ul>.
  static const text::Regex ol("<ol[^>]*>.*?<li[^>]*>(.*?)(</li>|<ol|<ul)", true, true);
  for (auto &x : ol.all(html))
    if (auto c = clean(plain(x.groups[1])); length(c) > 1) return c;
  // A leading [transcription] is not a sense (Mueller: "[went] _p. от go 1").
  static const text::Regex bracket("^\\[[^]]*]\\s*"), nl("\\r\\n|\\r|\\n"),
      skip("^(•\\s*)?(IPA|Hyphenation|Rhymes|Audio|Homophone|Anagrams|Etymology|МФА|Произношение|Семантические|Морфолог)|IPA:|МФА", true),
      one_word("^[\\p{L} ]{1,20}$"), numbered("^(•|\\d+[.)]|[-–]) ?\\S");
  std::vector<std::string> lines;
  auto all = nl.replace(plain(html), "\n");
  for (size_t at = 0; at <= all.size();) {
    auto e = all.find('\n', at);
    if (e == std::string::npos) e = all.size();
    if (auto l = bracket.replace(trim(all.substr(at, e - at)), ""); length(l) > 1) lines.push_back(l);
    at = e + 1;
  }
  text::Regex::Match mm;
  for (auto &l : lines)
    if (numbered.find(l, mm) && !skip.find(l, mm) && length(l) > 3) return clean(l);
  for (auto &l : lines)
    if (text::lower(l) != text::lower(headword) && !skip.find(l, mm) && !l.starts_with("[") && !one_word.matches(l)) return clean(l);
  return "";
}

std::optional<Head> head(const Hit &h) {
  if (auto l = head_line(h.html)) return Head{h.word, *l, ""};
  std::string m;
  try {
    m = short_meaning(h.html, h.word);
  } catch (const std::exception &) {
  }
  if (trim(m).empty()) return std::nullopt;
  return Head{h.word, h.word + " — " + m, ""};
}

}  // namespace lexipeek::dict
