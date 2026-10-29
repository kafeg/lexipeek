#include "lexipeek/study.hpp"
#include <algorithm>
#include <set>
#include "text/text.hpp"

namespace lexipeek {

std::vector<std::string> study_parse(const std::vector<std::string> &codes) {
  static const text::Regex code("^[a-z]{2,3}$");
  std::vector<std::string> out;
  for (auto &c : codes) {
    auto x = text::trim(c);
    if (code.matches(x) && std::find(out.begin(), out.end(), x) == out.end() && out.size() < study_max) out.push_back(x);
  }
  return out;
}

std::vector<std::string> study_offered(const std::string &answer, const std::vector<LangPair> &pairs) {
  std::set<std::string> out;
  for (auto &p : pairs)
    if (p.from == answer && p.to != answer) out.insert(p.to);
  return {out.begin(), out.end()};
}

std::vector<std::string> study_effective(const std::vector<std::string> &stored, const std::string &answer, const std::vector<LangPair> &pairs) {
  std::vector<std::string> out;
  for (auto &x : stored)
    if (std::find(pairs.begin(), pairs.end(), LangPair{answer, x}) != pairs.end()) out.push_back(x);
  return out;
}

std::string study_direction(const std::string &answer, const std::vector<std::string> &langs) {
  std::string out;
  for (auto &x : langs) out += (out.empty() ? "" : ", ") + x;
  return answer + " → " + out;
}

std::vector<std::string> study_words(const std::string &meaning, std::size_t max) {
  static const text::Regex notes("\\([^)]*\\)|\\[[^\\]]*\\]"), sep("[,;]"), dots("^\\.+|\\.+$");
  auto plain = notes.replace(meaning, "");
  std::vector<std::string> out;
  std::set<std::string> seen;
  size_t at = 0;
  auto bounds = sep.all(plain);
  for (size_t i = 0; i <= bounds.size(); i++) {
    auto end = i < bounds.size() ? bounds[i].begin : plain.size();
    auto w = dots.replace(text::trim(plain.substr(at, end - at)), "");
    if (i < bounds.size()) at = bounds[i].end;
    if (w.empty() || !seen.insert(text::lower(w)).second) continue;
    if (out.size() < max) out.push_back(w);
  }
  return out;
}

}  // namespace lexipeek
