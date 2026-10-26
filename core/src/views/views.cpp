#include "lexipeek/views.hpp"
#include <algorithm>
#include <cstdio>

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
  {
    if (!s.translation.ok()) return {{Style::error, "", s.translation.error, ""}};
    std::vector<CardRow> rows{{Style::text, "", s.translation.text, ""}};
    if (!s.translation.english.empty()) rows.push_back({Style::english, "", s.translation.english, ""});
    return rows;
  }
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
  return v;
}

std::string size_text(std::int64_t bytes) {
  char buf[32];
  if (bytes >= 1'000'000'000) std::snprintf(buf, sizeof buf, "%.1f GB", static_cast<double>(bytes) / 1e9);
  else std::snprintf(buf, sizeof buf, "%lld MB", static_cast<long long>(std::max<std::int64_t>(1, (bytes + 500'000) / 1'000'000)));
  return buf;
}

bool has_pair(const Manifest &m, const std::map<std::string, std::string> &installed) {
  auto rows = pack_rows(m, installed);
  return std::any_of(rows.begin(), rows.end(), [](auto &r) { return r.installed; });
}

bool first_run_at_launch(bool has_pair, bool trusted, bool finished) { return !has_pair || (!trusted && !finished); }

std::string first_answer(const Manifest &m, const std::string &system) {
  auto a = answer_languages(m);
  return std::find(a.begin(), a.end(), system) != a.end() ? system : "en";
}

FirstRunView first_run_view(const Manifest &m, const std::map<std::string, std::string> &installed, const FirstRunState &s) {
  FirstRunView v;
  auto key = s.hotkey.empty() ? std::string("the hotkey") : s.hotkey;
  if (!s.trusted && !s.access_later) {
    v.step = FirstStep::access;
    v.title = "Let LexiPeek read the selection";
    v.text = "When you press " + key + ", LexiPeek reads the text selected in the app in front, by Accessibility. "
             "It never touches the clipboard, reads nothing else, and nothing leaves this Mac.";
    v.note = "Turn LexiPeek on in System Settings → Privacy & Security → Accessibility. This window goes on by itself.";
    v.button = "Open System Settings";
    v.secondary = "Later";
    return v;
  }
  auto rows = pack_rows(m, installed);
  auto answers = answer_languages(m);
  v.answer = std::find(answers.begin(), answers.end(), s.answer) != answers.end() ? s.answer : first_answer(m, s.system);
  for (auto &a : answers) v.answers.push_back({a, language_name(a), ""});
  const PairRow *picked = nullptr;
  for (auto &r : rows) {
    if (r.to != v.answer || r.from == v.answer) continue;
    auto level = r.level.empty() ? std::string() : std::string(1, static_cast<char>(r.level[0] - 'a' + 'A')) + r.level.substr(1);
    auto state = r.installed ? std::string("installed") : size_text(r.missing);
    v.books.push_back({r.from, language_name(r.from), level.empty() ? state : level + " · " + state});
    if (r.from == s.book || (!picked && s.book.empty())) picked = &r;
  }
  if (!picked)
    for (auto &r : rows)
      if (r.to == v.answer && r.from != v.answer) {
        picked = &r;
        break;
      }
  if (picked) v.book = picked->from;
  v.number = 2;
  auto pair = picked ? picked->title : std::string();
  if (s.downloading || !s.error.empty()) {
    v.step = FirstStep::download;
    v.title = "Downloading " + pair;
    v.progress = s.downloading;
    if (s.total > 0) {
      v.fraction = static_cast<double>(s.done) / static_cast<double>(s.total);
      v.progress_text = size_text(s.done) + " of " + size_text(s.total);
    }
    v.text = "The dictionary and the translation models, to work offline.";
    v.error = s.error;
    v.button = s.downloading ? "Cancel" : "Try Again";
    v.secondary = s.downloading ? "" : "Back";
    return v;
  }
  if (has_pair(m, installed)) {
    v.step = FirstStep::done;
    v.number = 3;
    v.title = "Ready";
    v.text = "Select a word or a sentence in any app and press " + key + ".";
    v.note = s.trusted ? "More language pairs in Settings → Packs."
                       : "Until LexiPeek is allowed in Accessibility, the hotkey does nothing: " + s.input + " opens a card to type in, "
                         "and “Look up in LexiPeek” is in the Services menu of any app.";
    v.button = "Done";
    return v;
  }
  v.title = "Pick your first language pair";
  if (m.components.empty() || rows.empty()) {
    v.step = FirstStep::list;
    v.progress = s.fetching;
    v.text = s.fetching ? "Getting the list of language packs…" : "";
    v.error = s.fetching ? "" : s.list_error.empty() ? "No list of language packs yet." : s.list_error;
    v.button = "Try Again";
    v.button_enabled = !s.fetching;
    return v;
  }
  v.step = FirstStep::pair;
  v.text = "The language you read in and the one to translate into. Dictionaries and translation work offline, on this Mac.";
  v.note = "More pairs later in Settings → Packs.";
  v.button = picked ? "Download " + size_text(picked->missing) : "Download";
  v.button_enabled = picked != nullptr;
  return v;
}

}  // namespace lexipeek
