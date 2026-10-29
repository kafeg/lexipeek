#include "lexipeek/views.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>
#include "lexipeek/dicts.hpp"
#include "lexipeek/l10n.hpp"
#include "text/text.hpp"

namespace lexipeek {

std::string language_name(const std::string &code) {
  auto key = "lang_" + code;
  return has_text(key) ? tr(key) : catalog_language(code);
}

std::vector<PairRow> pack_rows(const Manifest &m, const std::map<std::string, std::string> &installed, const std::optional<Device> &device) {
  auto part = [&](const Component &c) {
    PackPart p{c.id, c.kind, "", c.size, installed.contains(c.id), false, device ? cannot(c, *device) : ""};
    p.update = p.installed && installed.at(c.id) != c.sha256;
    if (c.kind == "dict") p.title = tr("part_dict");
    else if (c.kind == "model") p.title = "Mozilla " + c.from + " → " + c.to;
    else if (c.kind == "langid") p.title = tr("part_langid");
    else if (c.kind == "opus") p.title = "OPUS-MT";
    else p.title = c.name.empty() ? c.id : c.name;
    return p;
  };
  std::vector<PairRow> rows;
  for (auto &pr : pairs(m)) {
    PairRow r{pr.from, pr.to, language_name(pr.from) + " → " + language_name(pr.to), "", true, 0, {}, {}};
    if (auto l = level(m, pr.from, pr.to); l != Level::none) r.level = std::string(level_name(l));
    for (auto &c : needed(m, {pr}, Method::fast)) {
      auto p = part(c);
      if (!p.installed) r.installed = false, r.missing += c.size;
      r.base.push_back(p);
    }
    for (auto kind : {"opus", "llm"})
      if (auto c = covering(m, kind, pr.from, pr.to)) r.extras.push_back(part(*c));
    // The default method's model (OPUS-MT) comes with a pair's download.
    if (auto c = covering(m, "opus", pr.from, pr.to); c && !installed.contains(c->id) && (!device || cannot(*c, *device).empty()))
      r.missing += c->size;
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

std::vector<std::string> missing_ids(const Manifest &m, const std::map<std::string, std::string> &installed, const LangPair &pair,
                                     Method method) {
  std::vector<std::string> ids;
  for (auto &c : needed(m, {pair}, method))
    if (!installed.contains(c.id)) ids.push_back(c.id);
  return ids;
}

std::vector<std::string> freed_ids(const Manifest &m, const std::map<std::string, std::string> &installed, const LangPair &pair) {
  std::vector<LangPair> have;
  for (auto &r : pack_rows(m, installed))
    if (r.installed) have.push_back({r.from, r.to});
  // Every method's models no other pair needs (an OPUS or Hy-MT2 model too).
  std::vector<std::string> ids;
  for (auto method : {Method::fast, Method::direct, Method::llm})
    for (auto &c : freed(m, have, pair, method))
      if (std::find(ids.begin(), ids.end(), c.id) == ids.end()) ids.push_back(c.id);
  return ids;
}

std::vector<MethodInfo> method_infos(const Manifest &m, const std::map<std::string, std::string> &installed, const Device &d) {
  std::vector<LangPair> have;
  for (auto &r : pack_rows(m, installed))
    if (r.installed) have.push_back({r.from, r.to});
  std::vector<MethodInfo> out;
  for (auto method : {Method::fast, Method::direct, Method::llm}) {
    MethodInfo info;
    info.method = method;
    std::vector<Component> comps;
    auto add = [&](const Component &c) {
      if (std::none_of(comps.begin(), comps.end(), [&](auto &x) { return x.id == c.id; })) comps.push_back(c);
    };
    if (method == Method::fast) {
      for (auto &c : needed(m, have, Method::fast))
        if (c.kind == "model") add(c);
    } else {
      auto kind = method == Method::direct ? "opus" : "llm";
      for (auto &p : have) {
        if (auto c = covering(m, kind, p.from, p.to)) add(*c);
        else info.bare.push_back(p.from + "→" + p.to);
      }
      // The device check needs a model even when no pair has one.
      auto any = std::find_if(m.components.begin(), m.components.end(), [&](auto &c) { return c.kind == kind; });
      if (any == m.components.end()) info.cannot = tr("mt_no_models");
      else info.cannot = cannot(comps.empty() ? *any : comps.front(), d);
    }
    for (auto &c : comps) {
      info.models++, info.size += c.size;
      auto i = installed.find(c.id);
      if (i == installed.end() || i->second != c.sha256) info.missing.push_back(c.id), info.left += c.size;
    }
    out.push_back(info);
  }
  return out;
}

std::vector<std::string> answer_languages(const Manifest &m) {
  std::vector<std::string> out;
  for (auto &p : pairs(m))
    if (std::find(out.begin(), out.end(), p.to) == out.end()) out.push_back(p.to);
  std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return language_name(a) < language_name(b); });
  return out;
}

std::string method_title(Method m) { return tr(m == Method::fast ? "mt_fast_t" : m == Method::direct ? "mt_direct_t" : "mt_llm_t"); }

MemoryView memory_view(const Translator::Memory &m) {
  MemoryView v;
  v.held = !m.key.empty();
  if (!v.held) return v.line = v.text = tr("mem_none"), v;
  v.line = tr("mem_line", {m.title.empty() ? m.key : m.title, size_text(m.bytes)});
  v.detail = m.seconds < 0 ? tr("mem_kept") : m.seconds < 60 ? tr("mem_soon") : tr("mem_until", {tr("keep_min", {digits((m.seconds + 59) / 60)})});
  v.unload = tr("mem_unload");
  v.text = v.line + " · " + v.detail;
  return v;
}

CardTab first_tab(const Card &c) {
  if (c.kind == Card::Kind::word) return c.entries.empty() ? CardTab::translate : CardTab::dictionary;
  bool words = !c.gloss.empty() || !c.separable.empty();
  if (c.kind != Card::Kind::phrase) return CardTab::translate;
  // Card → Opening: an AI tab asks at once (the person chose so); one that is off falls back to the translation.
  auto &o = c.prefs.phrase_open;
  if (o == "analysis" && c.ai.analysis) return CardTab::analysis;
  if (o == "context" && c.ai.context) return CardTab::context;
  // Nothing translates offline and no AI: word by word.
  if (words && c.mt_off && !c.ai.translate) return CardTab::dictionary;
  return words && o == "dict" ? CardTab::dictionary : CardTab::translate;
}

CardState first_state(const Card &c) {
  CardState s;
  s.tab = first_tab(c);
  s.full_entry = c.kind == Card::Kind::word && c.prefs.full;
  s.has_method = c.method.has_value();
  s.method = c.method.value_or(Method::fast);
  s.saved = c.saved;
  // Offline translation off (InkPeek's "off"): Translate is the AI translation, when AI is on.
  s.by_ai = c.ai.translate && (c.mt_off || (c.kind == Card::Kind::phrase && c.prefs.phrase_open == "ai" && !c.ai.no_key));
  return s;
}

namespace {

using Style = CardRow::Style;

std::string section_title(const std::string &key) {
  return tr(key == "rel" || key == "syn" || key == "expr" || key == "forms" ? "sec_" + key : std::string("sec_ety"));
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
  auto n = [](const std::vector<std::string> &xs) { return digits(static_cast<long long>(xs.size())); };
  std::vector<std::string> forms;
  for (auto &f : r.forms) forms.push_back(f.first);
  std::string ipa;
  for (auto &i : r.ipa) ipa += (ipa.empty() ? "" : ", ") + i;
  row("rel", few(r.rel), tr("sec_rel") + " · " + n(r.rel));
  row("syn", few(r.syn) + (r.ant.empty() ? "" : " ≠ " + few(r.ant)), tr("sec_syn") + " · " + n(r.syn) + (r.ant.empty() ? "" : " · " + tr("sec_ant") + " · " + n(r.ant)));
  row("expr", few(r.expr, "; "), tr("sec_expr") + " · " + n(r.expr));
  row("forms", few(forms), tr("sec_forms") + (ipa.empty() ? "" : " · " + ipa));
  row("ety", cut(r.origin, 120), tr("sec_ety"));
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

// The full entries' dictionary shown (kept inside the card's).
int page_of(const Card &c, const CardState &s) { return std::clamp(s.entry_page, 0, std::max(0, static_cast<int>(c.entries.size()) - 1)); }

std::vector<CardRow> dictionary_rows(const Card &c, const CardState &s) {
  std::vector<CardRow> rows;
  if (c.kind == Card::Kind::word) {
    if (!s.section.key.empty()) return section_page(s.section);
    if (c.entries.empty()) rows.push_back({Style::note, "", tr(c.no_packs ? "card_no_packs" : "card_no_entry"), ""});
    for (size_t i = 0; i < c.entries.size(); i++) {
      // One dictionary's line clicked: only its full entry; full entries: the one turned to.
      if (s.entry_open >= 0 && static_cast<int>(i) != s.entry_open) continue;
      if (s.entry_open < 0 && s.full_entry && static_cast<int>(i) != page_of(c, s)) continue;
      bool full = s.full_entry || s.entry_open >= 0;
      auto &e = c.entries[i];
      // The selected form names itself first (InkPeek's line): "Häusern: das Haus (…) — дом".
      auto form = text::lower(e.word) == text::lower(c.text) ? "" : c.text + ": ";
      // The header and the form are above: the card's own headword shows only its meaning, and the dictionary's name
      // only when there are more.
      auto label = c.entries.size() > 1 ? e.dictionary : "";
      auto short_line = [&](const std::string &l) {
        auto dash = l.find(" — ");
        return dash != std::string::npos && text::lower(e.word) == text::lower(c.save_text) ? l.substr(dash + 5) : form + l;
      };
      if (!full) {
        CardRow r{Style::entry, label, short_line(e.line), ""};
        r.copy = r.text;
        if (c.entries.size() > 1) r.open = "entry", r.entry = static_cast<int>(i);
        rows.push_back(r);
        continue;
      }
      if (e.articles.empty()) {
        rows.push_back({Style::entry, e.dictionary, "", e.article});
        continue;
      }
      for (size_t j = 0; j < e.articles.size(); j++) {
        auto &a = e.articles[j];
        if (j == 0) rows.push_back({Style::entry, label, short_line(a.line), ""});
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
  if (c.more > 0) rows.push_back({Style::note, "", trn("card_more", c.more), ""});
  return rows;
}

// A card's language by its code in the app's language now, else the name the card was given.
std::string name(const std::string &code, const std::string &named) { return code.empty() ? named : language_name(code); }

// An AI answer: "Thinking…", its error, or its light Markdown in paragraphs under the app's headings.
std::vector<CardRow> ai_rows(ai::Ask a, const AiState &st, bool word) {
  if (!st.answered) return {{Style::progress, "", tr("thinking") + "…", ""}};
  if (!st.ok) return {{Style::error, "", st.error, ""}};
  std::vector<CardRow> rows;
  auto md = ai::render(a, st.json, word);
  size_t at = 0;
  while (at <= md.size()) {
    auto end = md.find("\n\n", at);
    auto para = md.substr(at, end == std::string::npos ? std::string::npos : end - at);
    at = end == std::string::npos ? md.size() + 1 : end + 2;
    // "**Grammar**\n…": the app's heading, then its text.
    if (para.starts_with("**")) {
      auto close = para.find("**\n", 2);
      if (close != std::string::npos) {
        rows.push_back({Style::heading, "", para.substr(2, close - 2), ""});
        para = para.substr(close + 3);
      }
    }
    std::string out;
    size_t from = 0;
    while (from <= para.size()) {
      auto nl = para.find('\n', from);
      auto line = para.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
      from = nl == std::string::npos ? para.size() + 1 : nl + 1;
      if (line.starts_with("- ")) line = "• " + line.substr(2);
      out += (out.empty() ? "" : "\n") + line;
    }
    if (!out.empty()) rows.push_back({Style::markdown, "", out, ""});
  }
  if (rows.empty()) rows.push_back({Style::note, "", "—", ""});
  return rows;
}

std::vector<CardRow> translate_rows(const Card &c, const CardState &s) {
  if (s.by_ai && c.ai.translate) return ai_rows(ai::Ask::quick, s.quick, c.kind == Card::Kind::word);
  auto note = [](std::string t) { return std::vector<CardRow>{{Style::note, "", std::move(t), ""}}; };
  if (!c.lang.empty() && c.lang == c.answer) return note(tr("card_already", {name(c.answer, c.answer_name)}));
  if (c.no_packs) return note(tr("card_no_packs"));
  if (c.lang.empty())
    return note(c.other_name.empty() ? tr("card_no_lang") : tr("card_no_pack", {name(c.other, c.other_name), name(c.answer, c.answer_name)}));
  if (c.mt_off) return note(tr("card_mt_off"));
  if (c.methods.empty()) return note(c.error.empty() ? tr("card_no_method") : c.error);
  if (s.translated)
  {
    if (!s.translation.ok()) return {{Style::error, "", s.translation.error, ""}};
    std::vector<CardRow> rows{{Style::text, "", s.translation.text, ""}};
    rows.front().copy = s.translation.text;
    if (!s.translation.english.empty()) rows.push_back({Style::english, "", s.translation.english, ""});
    return rows;
  }
  if (s.translating) return {{Style::progress, "", tr("mt_working"), ""}};
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
    if (c.looks > 1) facts.push_back(trn("looks", c.looks));
    std::string fl;
    for (auto &f : facts) fl += (fl.empty() ? "" : " · ") + f;
    if (!c.base.empty()) v.subtitle = tr("form_of_sel", {c.text});
    if (!fl.empty()) v.subtitle += (v.subtitle.empty() ? "" : " · ") + fl;
  } else {
    v.style = c.kind == Card::Kind::phrase ? HeaderStyle::phrase : HeaderStyle::passage;
    v.title = c.text;
  }
  // Codes only: "de → ru"; "ru" when the text is in the answer language; "fr → ru" for a language without a pack.
  if (!c.lang.empty()) v.badge = c.lang == c.answer ? c.lang : c.lang + " → " + c.answer;
  else if (!c.other.empty()) v.badge = c.other + " → " + c.answer;
  if (c.books.size() > 1 || c.answers.size() > 1) {
    for (auto &b : c.books) v.books.push_back({b, language_name(b), b == c.lang});
    for (auto &a : c.answers) v.answers.push_back({a, language_name(a), a == c.answer});
  }
  if (c.kind == Card::Kind::word || !c.gloss.empty() || !c.separable.empty()) v.tab_items.push_back({CardTab::dictionary, tr("tab_dictionary")});
  v.tab_items.push_back({CardTab::translate, tr("tab_translate")});
  if (c.ai.analysis) v.tab_items.push_back({CardTab::analysis, tr("a_analysis") + " · " + tr("tab_ai")});
  if (c.ai.context) v.tab_items.push_back({CardTab::context, tr("a_context") + " · " + tr("tab_ai")});
  v.tabs = v.tab_items.size() > 1;
  v.tab = CardTab::translate;
  for (auto &t : v.tab_items)
    if (t.tab == s.tab) v.tab = s.tab;
  bool dict = v.tab == CardTab::dictionary;
  bool word = c.kind == Card::Kind::word;
  if (v.tab == CardTab::analysis) v.rows = ai_rows(ai::Ask::analysis, s.analysis, word);
  else if (v.tab == CardTab::context) v.rows = ai_rows(ai::Ask::context, s.context, word);
  else v.rows = dict ? dictionary_rows(c, s) : translate_rows(c, s);
  // Copy: the first row that says what to copy, else the AI answer as plain text.
  static const text::Regex marks("\\*\\*|\\*");
  for (auto &r : v.rows)
    if (!r.copy.empty()) {
      v.copy = r.copy;
      break;
    }
  if (v.copy.empty())
    for (auto &r : v.rows)
      if (r.style == Style::markdown || r.style == Style::heading)
        v.copy += (v.copy.empty() ? "" : "\n\n") + marks.replace(r.text, "");
  bool ai_tab = v.tab == CardTab::analysis || v.tab == CardTab::context || (v.tab == CardTab::translate && s.by_ai);
  if (ai_tab && !v.rows.empty() && v.rows.front().style == Style::error) v.fix = tr("ai_open_settings");
  // No pack at all, or none from the text's language: the way to Settings → Packs.
  if (c.no_packs || (v.tab == CardTab::translate && !s.by_ai && c.lang.empty() && !c.other_name.empty()))
    v.fix = tr("packs_open"), v.fix_section = SettingsSection::languages, v.fix_tab = 0;
  bool in_section = dict && c.kind == Card::Kind::word && !s.section.key.empty();
  bool one = s.entry_open >= 0;
  if (in_section && s.section.pages > 1) {
    auto &p = s.section;
    v.pager = tr("page_of", {digits(p.first + 1), digits(p.first + static_cast<int>(p.items.size())), digits(p.total)});
    v.prev = p.page > 0, v.next = p.page + 1 < p.pages;
  }
  // Full entries, one dictionary at a time (InkPeek's): "1 / 3", ◀ ▶.
  int n = static_cast<int>(c.entries.size());
  if (dict && c.kind == Card::Kind::word && s.full_entry && !one && !in_section && n > 1) {
    int i = page_of(c, s);
    v.pager = digits(i + 1) + " / " + digits(n);
    v.prev = i > 0, v.next = i + 1 < n;
  }
  if (in_section) v.back = tr("card_back_entry");
  else if (one && dict) v.back = tr("card_back_all");
  else if (!s.before.empty()) v.back = s.before;
  v.menu = v.tab == CardTab::translate && (!c.methods.empty() || c.ai.translate);
  if (v.menu) {
    v.menu_title = tr(c.methods.empty() ? "mt_ask_ai" : "card_no_methods");
    for (auto &m : c.methods) {
      auto name = method_title(m.method);
      v.methods.push_back({m.method, m.available ? name : name + " — " + m.reason, m.available});
      if (s.has_method && m.method == s.method && !s.by_ai) v.menu_title = name;
    }
    if (s.translated && s.translation.ok() && !s.by_ai)
      v.menu_help = s.translation.kept ? tr("mt_kept") : tr("mt_took", {decimal(static_cast<double>(s.translation.ms) / 1000.0)});
    if (c.ai.translate) {
      v.methods.push_back({Method::fast, c.ai.method, true, true});
      if (s.by_ai) v.menu_title = c.ai.method;
    }
  }
  if (!c.save_text.empty()) {
    v.can_save = true, v.saved = s.saved;
    v.star_help = tr(s.saved ? "card_unsave" : "card_save", {c.save_text});
  }
  v.width = c.prefs.width + (v.style == HeaderStyle::passage ? 60 : 0);  // a passage: a wider card
  v.text = c.prefs.text, v.font = c.prefs.font, v.height = c.prefs.height;
  return v;
}

std::vector<ai::Ask> card_asks(const Card &c, const CardState &s) {
  std::vector<ai::Ask> out;
  auto shown = [&](ai::Ask a) {
    return a == ai::Ask::quick ? c.ai.translate : a == ai::Ask::analysis ? c.ai.analysis : c.ai.context;
  };
  std::optional<ai::Ask> open;
  if (s.tab == CardTab::analysis && c.ai.analysis) open = ai::Ask::analysis;
  else if (s.tab == CardTab::context && c.ai.context) open = ai::Ask::context;
  else if (s.tab == CardTab::translate && s.by_ai && c.ai.translate) open = ai::Ask::quick;
  if (!open) return out;
  auto st = card_answer(s, *open);
  if (!st.asking && !(st.answered && st.ok)) out.push_back(*open);
  if (c.ai.prefetch)
    for (auto a : {ai::Ask::quick, ai::Ask::analysis, ai::Ask::context}) {
      auto x = card_answer(s, a);
      if (a != *open && shown(a) && !x.asking && !x.answered) out.push_back(a);
    }
  return out;
}

AiState card_answer(const CardState &s, ai::Ask a) { return a == ai::Ask::quick ? s.quick : a == ai::Ask::analysis ? s.analysis : s.context; }

CardState with_answer(const CardState &s, ai::Ask a, const AiState &answer) {
  auto out = s;
  (a == ai::Ask::quick ? out.quick : a == ai::Ask::analysis ? out.analysis : out.context) = answer;
  return out;
}

std::string shown_translation(const CardState &s) {
  if (s.by_ai) return s.quick.ok ? ai::brief(s.quick.json) : "";
  return s.translated && s.translation.ok() ? s.translation.text : "";
}

std::string size_text(std::int64_t bytes) {
  if (bytes >= 1'000'000'000) return tr("size_gb", {decimal(static_cast<double>(bytes) / 1e9)});
  return tr("size_mb", {digits(std::max<std::int64_t>(1, (bytes + 500'000) / 1'000'000))});
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
  auto key = s.hotkey.empty() ? tr("fr_hotkey") : s.hotkey;
  v.answer_label = tr("answer_t"), v.book_label = tr("fr_book");
  if (!s.trusted && !s.access_later) {
    v.step = FirstStep::access;
    v.title = tr("fr_access_t");
    v.text = tr("fr_access", {key});
    v.note = tr("fr_access_note");
    v.button = tr("fr_open_settings");
    v.secondary = tr("later");
    return v;
  }
  auto rows = pack_rows(m, installed);
  auto answers = answer_languages(m);
  v.answer = std::find(answers.begin(), answers.end(), s.answer) != answers.end() ? s.answer : first_answer(m, s.system);
  for (auto &a : answers) v.answers.push_back({a, language_name(a), ""});
  const PairRow *picked = nullptr;
  for (auto &r : rows) {
    if (r.to != v.answer || r.from == v.answer) continue;
    auto level = r.level.empty() ? std::string() : tr("level_" + r.level);
    auto state = r.installed ? tr("pack_installed") : size_text(r.missing);
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
  // Windows has no Accessibility step: the pair is step 1 of 2.
  bool access = ui_platform() == "mac";
  v.steps = access ? 3 : 2;
  v.number = access ? 2 : 1;
  auto pair = picked ? picked->title : std::string();
  if (s.downloading || !s.error.empty()) {
    v.step = FirstStep::download;
    v.title = tr("fr_downloading", {pair});
    v.progress = s.downloading;
    if (s.total > 0) {
      v.fraction = static_cast<double>(s.done) / static_cast<double>(s.total);
      v.progress_text = tr("n_of_m", {size_text(s.done), size_text(s.total)});
    }
    v.text = tr("fr_download_d");
    v.error = s.error;
    v.button = tr(s.downloading ? "cancel" : "try_again");
    v.secondary = s.downloading ? "" : tr("back");
    return v;
  }
  if (has_pair(m, installed)) {
    v.step = FirstStep::done;
    v.number = v.steps;
    v.title = tr("fr_ready");
    v.text = tr("fr_done", {key});
    v.note = s.trusted ? tr("fr_done_note") : tr("fr_done_untrusted", {s.input});
    v.button = tr("m_close");
    return v;
  }
  v.title = tr("fr_pick");
  if (m.components.empty() || rows.empty()) {
    v.step = FirstStep::list;
    v.progress = s.fetching;
    v.text = s.fetching ? tr("fr_fetching") : "";
    v.error = s.fetching ? "" : s.list_error.empty() ? tr("fr_no_list") : s.list_error;
    v.button = s.fetching ? "" : tr("try_again");
    return v;
  }
  v.step = FirstStep::pair;
  v.text = tr("fr_pair");
  v.note = tr("fr_pair_note");
  v.button = picked ? tr("download_size", {size_text(picked->missing)}) : tr("download");
  v.button_enabled = picked != nullptr;
  return v;
}

std::string frequency(int rank) {
  for (int top : {1000, 5000, 20000})
    if (rank >= 1 && rank <= top) return tr("freq_top", {digits(top)});
  return "";
}

NotesView notes_view(const Store &st, NotesTab tab, const std::string &answer, int page) {
  NotesView v;
  v.tab = tab;
  auto line = [&](const std::string &text, const std::string &meaning, const std::string &lang) {
    if (!meaning.empty()) return meaning;
    auto code = lang.empty() ? answer : lang;
    auto a = st.newest_answer(text, code);
    // Else the AI translation's brief line (AI answers are kept under the language's English name, as InkPeek's).
    if (a.empty()) a = ai::brief(st.newest_answer(text, catalog_language(code), "quick"));
    return a.empty() ? std::string("—") : a;
  };
  struct Item {
    std::string text, meaning, lang, looks;
  };
  std::vector<Item> items;
  std::size_t words = 0, phrases = 0;
  for (auto &e : st.saved()) {
    bool word = e.kind == "word";
    (word ? words : phrases)++;
    if ((tab == NotesTab::words) == word && tab != NotesTab::history) items.push_back({e.text, e.meaning, e.lang, ""});
  }
  auto history = st.history();
  if (tab == NotesTab::history)
    for (auto &e : history) items.push_back({e.text, e.meaning, e.lang, std::to_string(e.count) + "×"});
  auto count = [](const char *key, std::size_t n) { return tr(key) + " (" + digits(static_cast<long long>(n)) + ")"; };
  v.tabs = {count("list_words", words), count("list_phrases", phrases), count("list_history", history.size())};
  if (items.empty()) v.empty = tr(tab == NotesTab::history ? "notes_empty_history" : "notes_empty_saved");
  v.clear = tab == NotesTab::history && !items.empty();
  // A page of 50 (page < 0: all); only its lines are worked out.
  const int per = 50;
  int total = static_cast<int>(items.size()), first = 0, last = total;
  if (page >= 0) {
    v.pages = std::max(1, (total + per - 1) / per);
    v.page = std::clamp(page, 0, v.pages - 1);
    first = v.page * per, last = std::min(total, first + per);
    if (v.pages > 1) v.pager = tr("page_of", {digits(first + 1), digits(last), digits(total)});
  }
  for (int i = first; i < last; i++) {
    auto &e = items[static_cast<size_t>(i)];
    v.rows.push_back({e.text, line(e.text, e.meaning, e.lang), e.looks});
  }
  return v;
}

std::string notes_tsv(const Store &st, NotesTab tab, const std::string &answer) {
  static const text::Regex breaks("[\\t\\r\\n]+");
  auto cell = [](const std::string &s) { return text::trim(breaks.replace(s, " ")); };
  auto day = [](std::int64_t ms) { return text::local_day(ms); };
  auto ai_line = [&](const std::string &text) { return ai::brief(st.newest_answer(text, catalog_language(answer), "quick")); };
  auto rows = notes_view(st, tab, answer, -1).rows;
  std::map<std::string, std::int64_t> times;
  if (tab == NotesTab::history)
    for (auto &e : st.history()) times[e.text] = e.seen;
  else
    for (auto &e : st.saved()) times[e.text] = e.created;
  std::string out;
  for (auto &r : rows) {
    auto meaning = r.line == "—" ? std::string() : r.line;
    for (auto &c : {cell(r.text), cell(meaning), cell(ai_line(r.text)), answer, day(times[r.text])})
      out += c + "\t";
    out.back() = '\n';
  }
  return out;
}

std::string remove_note(Store &st, NotesTab tab, const std::string &text) {
  return tab == NotesTab::history ? st.forget(text) : st.unsave(text);
}

}  // namespace lexipeek
