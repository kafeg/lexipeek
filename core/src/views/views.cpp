#include "lexipeek/views.hpp"
#include <algorithm>
#include "text/text.hpp"

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

std::vector<std::string> missing_ids(const Manifest &m, const std::map<std::string, std::string> &installed, const LangPair &pair) {
  std::vector<std::string> ids;
  for (auto &c : needed(m, {pair}))
    if (!installed.contains(c.id)) ids.push_back(c.id);
  return ids;
}

std::vector<std::string> freed_ids(const Manifest &m, const std::map<std::string, std::string> &installed, const LangPair &pair) {
  std::vector<LangPair> have;
  for (auto &r : pack_rows(m, installed))
    if (r.installed) have.push_back({r.from, r.to});
  std::vector<std::string> ids;
  for (auto &c : freed(m, have, pair)) ids.push_back(c.id);
  return ids;
}

std::vector<std::string> answer_languages(const Manifest &m) {
  std::vector<std::string> out;
  for (auto &p : pairs(m))
    if (std::find(out.begin(), out.end(), p.to) == out.end()) out.push_back(p.to);
  std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return language_name(a) < language_name(b); });
  return out;
}

CardTab first_tab(const Card &c) { return c.kind == Card::Kind::word && !c.entries.empty() ? CardTab::dictionary : CardTab::translate; }

CardState first_state(const Card &c) {
  CardState s;
  s.tab = first_tab(c);
  s.has_method = c.method.has_value();
  s.method = c.method.value_or(Method::fast);
  return s;
}

namespace {

using Style = CardRow::Style;

std::string section_title(const std::string &key) {
  return key == "rel" ? "Related words" : key == "syn" ? "Synonyms" : key == "expr" ? "Expressions and proverbs" : key == "forms" ? "Forms and pronunciation" : "Origin";
}

std::string number(int n) {
  auto s = std::to_string(n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
  return s;
}

// The first n characters (code points), " …" after more.
std::string cut(const std::string &s, size_t n) {
  size_t cps = 0;
  for (size_t i = 0; i < s.size(); i++)
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && cps++ == n) return s.substr(0, i) + " …";
  return s;
}

// The first ten, " …" after more (InkPeek's few()).
std::string few(const std::vector<std::string> &xs, const std::string &sep = ", ") {
  std::string out;
  for (size_t i = 0; i < xs.size() && i < 10; i++) out += (i ? sep : "") + xs[i];
  return out + (xs.size() > 10 ? " …" : "");
}

// Under a pack's article: a row per section it has (InkPeek's DictionaryPage.sections), a preview and its caption.
void section_rows(std::vector<CardRow> &rows, const Related &r, int entry) {
  auto row = [&](const std::string &key, const std::string &preview, const std::string &caption) {
    if (!preview.empty()) rows.push_back({Style::section, preview, caption + " ›", "", "", key, entry});
  };
  auto n = [](const std::vector<std::string> &xs) { return std::to_string(xs.size()); };
  std::vector<std::string> forms;
  for (auto &f : r.forms) forms.push_back(f.first);
  std::string ipa;
  for (auto &i : r.ipa) ipa += (ipa.empty() ? "" : ", ") + i;
  row("rel", few(r.rel), "Related words · " + n(r.rel));
  row("syn", few(r.syn) + (r.ant.empty() ? "" : " ≠ " + few(r.ant)), "Synonyms · " + n(r.syn) + (r.ant.empty() ? "" : " · antonyms " + n(r.ant)));
  row("expr", few(r.expr, "; "), "Expressions and proverbs · " + n(r.expr));
  row("forms", few(forms), "Forms and pronunciation" + (ipa.empty() ? "" : " · " + ipa));
  row("ety", cut(r.origin, 120), "Origin");
}

std::vector<CardRow> section_page(const SectionPage &p) {
  std::vector<CardRow> rows{{Style::heading, "", p.word + " · " + section_title(p.key), ""}};
  if (!p.text.empty()) rows.push_back({Style::text, "", p.text, ""});
  for (auto &i : p.items) {
    CardRow r{Style::gloss, i.word, i.meaning.empty() ? "" : "— " + i.meaning, ""};
    r.link = i.base;
    rows.push_back(r);
  }
  return rows;
}

std::vector<CardRow> dictionary_rows(const Card &c, const CardState &s) {
  std::vector<CardRow> rows;
  if (c.kind == Card::Kind::word) {
    if (!s.section.key.empty()) return section_page(s.section);
    if (c.entries.empty()) rows.push_back({Style::note, "", "No entry in the dictionaries", ""});
    for (size_t i = 0; i < c.entries.size(); i++) {
      auto &e = c.entries[i];
      // The selected form names itself first (InkPeek's line): "Häusern: das Haus (…) — дом".
      auto form = text::lower(e.word) == text::lower(c.text) ? "" : c.text + ": ";
      if (!s.full_entry) {
        rows.push_back({Style::entry, e.dictionary, form + e.line, ""});
        continue;
      }
      if (e.articles.empty()) {
        rows.push_back({Style::entry, e.dictionary, "", e.article});
        continue;
      }
      for (size_t j = 0; j < e.articles.size(); j++) {
        auto &a = e.articles[j];
        if (j == 0) rows.push_back({Style::entry, e.dictionary, form + a.line, ""});
        else rows.push_back({Style::heading, "", a.line, ""});
        for (size_t k = 0; k < a.senses.size(); k++) {
          rows.push_back({Style::sense, std::to_string(k + 1) + ".", a.senses[k].text, ""});
          for (auto &x : a.senses[k].examples) rows.push_back({Style::example, "", x, ""});
        }
      }
      section_rows(rows, e.related, static_cast<int>(i));
    }
    return rows;
  }
  // As InkPeek's word by word: a verb written apart first, then each word; a form names itself first.
  auto line = [](const Gloss &g) { return g.header.empty() ? g.base : g.header; };
  for (auto &g : c.separable) {
    CardRow r{Style::gloss, g.word + ": " + line(g), g.meaning.empty() ? "" : "— " + g.meaning, ""};
    r.link = g.base;
    rows.push_back(r);
  }
  for (auto &g : c.gloss) {
    bool known = !g.header.empty() || !g.meaning.empty();
    auto label = !known ? g.word : text::lower(g.base) == text::lower(g.word) ? line(g) : g.word + ": " + line(g);
    CardRow r{Style::gloss, label, g.meaning.empty() ? "" : "— " + g.meaning, ""};
    r.link = g.base.empty() ? g.word : g.base;
    rows.push_back(r);
  }
  if (c.more > 0) rows.push_back({Style::note, "", "… and " + std::to_string(c.more) + (c.more == 1 ? " more word" : " more words"), ""});
  return rows;
}

std::vector<CardRow> translate_rows(const Card &c, const CardState &s) {
  auto note = [](std::string t) { return std::vector<CardRow>{{Style::note, "", std::move(t), ""}}; };
  if (!c.lang.empty() && c.lang == c.answer) return note("Already in " + c.answer_name + ".");
  if (c.lang.empty())
    return note(c.other_name.empty() ? "Could not tell the language of this text."
                                     : "No language pack from " + c.other_name + " into " + c.answer_name + " yet.");
  if (c.methods.empty()) return note(c.error.empty() ? "Nothing can translate this text here." : c.error);
  if (s.translated)
    return {s.translation.ok() ? CardRow{Style::text, "", s.translation.text, ""} : CardRow{Style::error, "", s.translation.error, ""}};
  if (s.translating) return {{Style::progress, "", "Translating…", ""}};
  if (!s.has_method) return note(c.methods.front().reason);
  return {};
}

}  // namespace

CardView card_view(const Card &c, const CardState &s) {
  CardView v;
  v.tooltip = c.text;
  if (c.kind == Card::Kind::word) {
    v.style = HeaderStyle::word;
    v.title = c.header.empty() ? c.text : c.header;
    // InkPeek's head(): the selected form, then how common the word is and how often it was looked up here.
    std::vector<std::string> facts;
    if (auto f = frequency(c.rank); !f.empty()) facts.push_back(f);
    if (c.looks > 1) facts.push_back("looked up " + std::to_string(c.looks) + " times");
    std::string fl;
    for (auto &f : facts) fl += (fl.empty() ? "" : " · ") + f;
    if (!c.base.empty()) v.subtitle = "form: " + c.text;
    if (!fl.empty()) v.subtitle += (v.subtitle.empty() ? "" : "\n") + fl;
  } else {
    v.style = c.kind == Card::Kind::phrase ? HeaderStyle::phrase : HeaderStyle::passage;
    v.title = c.text;
  }
  if (!c.lang.empty()) v.badge = c.lang == c.answer ? c.lang_name : c.lang + " → " + c.answer;
  v.wide = v.style == HeaderStyle::passage;
  v.tabs = c.kind == Card::Kind::word || !c.gloss.empty() || !c.separable.empty();
  v.tab = v.tabs ? s.tab : CardTab::translate;
  bool dict = v.tab == CardTab::dictionary;
  v.rows = dict ? dictionary_rows(c, s) : translate_rows(c, s);
  bool in_section = dict && c.kind == Card::Kind::word && !s.section.key.empty();
  if (dict && c.kind == Card::Kind::word && !c.entries.empty() && !in_section) v.entry_toggle = s.full_entry ? "Short ⏎" : "Full entry ⏎";
  if (in_section && s.section.pages > 1) {
    auto &p = s.section;
    v.pager = std::to_string(p.first + 1) + "–" + std::to_string(p.first + static_cast<int>(p.items.size())) + " of " + std::to_string(p.total);
    v.prev = p.page > 0, v.next = p.page + 1 < p.pages;
  }
  if (in_section) v.back = "← Entry";
  else if (!s.before.empty()) v.back = "← " + s.before;
  v.menu = !dict && !c.methods.empty();
  if (v.menu) {
    v.menu_title = "No method available";
    for (auto &m : c.methods) {
      v.methods.push_back({m.method, m.available ? m.name : m.name + " — " + m.reason, m.available});
      if (s.has_method && m.method == s.method) v.menu_title = m.name;
    }
  }
  return v;
}

std::string frequency(int rank) {
  for (int top : {1000, 5000, 20000})
    if (rank >= 1 && rank <= top) return "among the " + number(top) + " most frequent words";
  return "";
}

}  // namespace lexipeek
