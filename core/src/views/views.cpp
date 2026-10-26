#include "lexipeek/views.hpp"
#include <algorithm>
#include <cstdio>
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

CardTab first_tab(const Card &c) {
  if (c.kind == Card::Kind::word) return c.entries.empty() ? CardTab::translate : CardTab::dictionary;
  bool words = !c.gloss.empty() || !c.separable.empty();
  return c.kind == Card::Kind::phrase && words && c.prefs.phrase_open == "dict" ? CardTab::dictionary : CardTab::translate;
}

CardState first_state(const Card &c) {
  CardState s;
  s.tab = first_tab(c);
  s.full_entry = c.kind == Card::Kind::word && c.prefs.full;
  s.has_method = c.method.has_value();
  s.method = c.method.value_or(Method::fast);
  s.saved = c.saved;
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
void section_rows(std::vector<CardRow> &rows, const Related &r, int entry, const std::vector<std::string> &shown) {
  auto row = [&](const std::string &key, const std::string &preview, const std::string &caption) {
    if (!preview.empty() && std::find(shown.begin(), shown.end(), key) != shown.end()) rows.push_back({Style::section, preview, caption + " ›", "", "", key, entry});
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
      section_rows(rows, e.related, static_cast<int>(i), c.prefs.sections);
    }
    return rows;
  }
  // As InkPeek's word by word: a verb written apart first, then each word; a form names itself first.
  auto line = [&](const Gloss &g) { return g.header.empty() || !c.prefs.grammar ? g.base : g.header; };
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
    v.title = c.header.empty() ? c.text : c.prefs.grammar ? c.header : c.base.empty() ? c.text : c.base;
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
  else if (!c.other_name.empty()) v.badge = c.other_name;  // a language without a pack, named
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
  if (!c.save_text.empty()) {
    v.star = s.saved ? "★" : "☆";
    v.star_help = s.saved ? "Remove " + c.save_text + " from Notes" : "Save " + c.save_text + " to Notes";
  }
  v.footer = !v.back.empty() || !v.entry_toggle.empty() || v.menu || !v.pager.empty();
  v.width = c.prefs.width + (v.wide ? 60 : 0);
  v.text = c.prefs.text, v.font = c.prefs.font, v.height = c.prefs.height;
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

std::string frequency(int rank) {
  for (int top : {1000, 5000, 20000})
    if (rank >= 1 && rank <= top) return "among the " + number(top) + " most frequent words";
  return "";
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
