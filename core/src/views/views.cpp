#include "lexipeek/views.hpp"
#include <algorithm>

namespace lexipeek {

std::string language_name(const std::string &code) {
  static const std::map<std::string, std::string> names{
      {"bg", "Bulgarian"}, {"cs", "Czech"},   {"da", "Danish"},     {"de", "German"},     {"en", "English"},   {"es", "Spanish"},
      {"fi", "Finnish"},   {"fr", "French"},  {"hu", "Hungarian"},  {"it", "Italian"},    {"nl", "Dutch"},     {"pl", "Polish"},
      {"pt", "Portuguese"}, {"ro", "Romanian"}, {"ru", "Russian"},   {"sv", "Swedish"},    {"uk", "Ukrainian"}};
  auto i = names.find(code);
  return i == names.end() ? code : i->second;
}

std::vector<PairRow> pack_rows(const Manifest &m, const std::map<std::string, std::string> &installed, const std::optional<Device> &device) {
  auto part = [&](const Component &c) {
    PackPart p{c.id, c.kind, "", c.size, installed.contains(c.id), false, device ? cannot(c, *device) : ""};
    p.update = p.installed && installed.at(c.id) != c.sha256;
    if (c.kind == "dict") p.title = "Dictionary";
    else if (c.kind == "model") p.title = "Mozilla " + c.from + " → " + c.to;
    else if (c.kind == "langid") p.title = "Language id";
    else if (c.kind == "opus") p.title = "OPUS-MT";
    else p.title = c.name.empty() ? c.id : c.name;
    return p;
  };
  std::vector<PairRow> rows;
  for (auto &pr : pairs(m)) {
    PairRow r{pr.from, pr.to, language_name(pr.from) + " → " + language_name(pr.to), "", true, 0, {}, {}, {}};
    if (auto l = level(m, pr.from, pr.to); l != Level::none) r.level = std::string(level_name(l));
    for (auto &c : needed(m, {pr}, Method::fast)) {
      auto p = part(c);
      if (!p.installed) r.installed = false, r.missing += c.size;
      r.base.push_back(p);
    }
    if (auto c = covering(m, "opus", pr.from, pr.to)) r.direct = part(*c);
    if (auto c = covering(m, "llm", pr.from, pr.to)) r.llm = part(*c);
    rows.push_back(r);
  }
  auto rank = [](const std::string &l) { return l == "full" ? 0 : l == "basic" ? 1 : l == "experimental" ? 2 : 3; };
  std::stable_sort(rows.begin(), rows.end(), [&](const PairRow &a, const PairRow &b) {
    if (a.installed != b.installed) return a.installed;
    if (rank(a.level) != rank(b.level)) return rank(a.level) < rank(b.level);
    return a.title < b.title;
  });
  return rows;
}

}  // namespace lexipeek
