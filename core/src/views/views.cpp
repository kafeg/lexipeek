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
  s.saved = c.saved;
  return s;
}

namespace {

using Style = CardRow::Style;

std::vector<CardRow> dictionary_rows(const Card &c, const CardState &s) {
  std::vector<CardRow> rows;
  if (c.kind == Card::Kind::word) {
    if (c.entries.empty()) rows.push_back({Style::note, "", "No entry in the dictionaries", ""});
    for (auto &e : c.entries) rows.push_back({Style::entry, e.dictionary, s.full_entry ? "" : e.line, s.full_entry ? e.article : ""});
    return rows;
  }
  auto all = c.separable;
  all.insert(all.end(), c.gloss.begin(), c.gloss.end());
  for (auto &g : all) rows.push_back({Style::gloss, g.base.empty() || g.base == g.word ? g.word : g.word + " (" + g.base + ")", g.meaning, ""});
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
    if (!c.base.empty()) v.subtitle = "→ " + c.base;
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
  if (dict && c.kind == Card::Kind::word && !c.entries.empty()) v.entry_toggle = s.full_entry ? "Short ⏎" : "Full entry ⏎";
  v.menu = !dict && !c.methods.empty();
  if (v.menu) {
    v.menu_title = "No method available";
    for (auto &m : c.methods) {
      v.methods.push_back({m.method, m.available ? m.name : m.name + " — " + m.reason, m.available});
      if (s.has_method && m.method == s.method) v.menu_title = m.name;
    }
  }
  if (c.kind == Card::Kind::word && c.looks > 1) v.looks = "Looked up " + std::to_string(c.looks) + "×";
  if (!c.save_text.empty()) {
    v.star = s.saved ? "★" : "☆";
    v.star_help = s.saved ? "Remove " + c.save_text + " from Notes" : "Save " + c.save_text + " to Notes";
  }
  return v;
}

NotesView notes_view(const Store &st, NotesTab tab, const std::string &answer) {
  NotesView v;
  v.tab = tab;
  auto line = [&](const std::string &text, const std::string &meaning, const std::string &lang) {
    if (!meaning.empty()) return meaning;
    auto a = st.newest_answer(text, lang.empty() ? answer : lang);
    return a.empty() ? std::string("—") : a;
  };
  std::size_t words = 0, phrases = 0;
  for (auto &e : st.saved()) {
    bool word = e.kind == "word";
    (word ? words : phrases)++;
    if ((tab == NotesTab::words) == word && tab != NotesTab::history) v.rows.push_back({e.text, line(e.text, e.meaning, e.lang), ""});
  }
  auto history = st.history();
  if (tab == NotesTab::history)
    for (auto &e : history) v.rows.push_back({e.text, line(e.text, e.meaning, e.lang), std::to_string(e.count) + "×"});
  v.tabs = {"Words (" + std::to_string(words) + ")", "Phrases (" + std::to_string(phrases) + ")",
            "History (" + std::to_string(history.size()) + ")"};
  if (v.rows.empty())
    v.empty = tab == NotesTab::history ? "Nothing looked up yet." : "Nothing saved yet: ☆ in a card saves its text here.";
  v.clear = tab == NotesTab::history && !v.rows.empty();
  return v;
}

std::string remove_note(Store &st, NotesTab tab, const std::string &text) {
  return tab == NotesTab::history ? st.forget(text) : st.unsave(text);
}

}  // namespace lexipeek
