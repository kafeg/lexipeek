// Settings' panes (InkPeek's MainActivity sections and *Settings.kt, its ⚙ page as General): rows and their changes.
#include <algorithm>
#include <array>
#include "lexipeek/l10n.hpp"
#include "lexipeek/views.hpp"

namespace lexipeek {

namespace {

using Kind = SettingRow::Kind;

// Choices: the value and its text's key.
const std::vector<std::pair<std::string, std::string>> phrase_opens{{"offline", "open_offline"}, {"dict", "open_dict"}};
const std::vector<std::pair<std::string, std::string>> methods{
    {"", "mt_first"}, {"fast", "mt_fast_t"}, {"direct", "mt_direct_t"}, {"llm", "mt_llm_t"}};
const std::vector<int> widths{360, 400, 460, 520};
const std::vector<std::string> width_names{"width_narrow", "width_medium", "width_wide", "width_widest"};
const std::vector<int> sizes{12, 13, 14, 15, 16, 18};
const std::vector<std::pair<std::string, std::string>> fonts{{"", "ui_lang_system"}, {"serif", "font_serif"}, {"rounded", "font_rounded"}, {"mono", "font_mono"}};
const std::vector<int> heights{30, 45, 60, 75};
// Each UI language by its own name.
const std::vector<std::pair<std::string, std::string>> ui_langs{{"", ""},         {"en", "English"},  {"ru", "Русский"},   {"de", "Deutsch"},
                                                                {"fr", "Français"}, {"es", "Español"}, {"it", "Italiano"}, {"pt", "Português"},
                                                                {"pl", "Polski"}};
// InkPeek's pack sections (CardSettings.buildArticle): key; sec_<key> and sec_<key>_d are their texts.
const std::vector<std::string> sections{"rel", "syn", "expr", "forms", "ety"};
// About: the packs' sources and their licenses (title, about key, URL).
const std::vector<std::array<std::string, 3>> licenses{
    {"Wiktionary", "lic_wiktionary", "https://creativecommons.org/licenses/by-sa/4.0/"},
    {"Mozilla", "lic_mozilla", "https://www.mozilla.org/MPL/2.0/"},
    {"OPUS-MT", "lic_opus", "https://creativecommons.org/licenses/by/4.0/"},
    {"Hy-MT2", "lic_hymt", "https://www.apache.org/licenses/LICENSE-2.0"}};

template <class T>
int index_of(const std::vector<T> &xs, const T &x) {
  auto i = std::find(xs.begin(), xs.end(), x);
  return i == xs.end() ? 0 : static_cast<int>(i - xs.begin());
}

template <class K>
int index_of(const std::vector<std::pair<K, std::string>> &xs, const K &k) {
  for (size_t i = 0; i < xs.size(); i++)
    if (xs[i].first == k) return static_cast<int>(i);
  return 0;
}

// The choices' texts by their keys.
template <class K>
std::vector<std::string> names(const std::vector<std::pair<K, std::string>> &xs) {
  std::vector<std::string> out;
  for (auto &x : xs) out.push_back(tr(x.second));
  return out;
}

std::vector<std::string> names(const std::vector<std::string> &keys) {
  std::vector<std::string> out;
  for (auto &k : keys) out.push_back(tr(k));
  return out;
}

// The answer languages offered: the packs', the current one first when they lack it.
std::vector<std::string> answer_codes(const Settings &s, const SettingsState &st) {
  auto a = st.answers;
  if (std::find(a.begin(), a.end(), s.answer) == a.end()) a.insert(a.begin(), s.answer);
  return a;
}

SettingRow heading(std::string title, std::string about = "") { return {Kind::heading, "", std::move(title), std::move(about)}; }

SettingRow toggle(std::string key, std::string title, std::string about, bool on) {
  SettingRow r{Kind::toggle, std::move(key), std::move(title), std::move(about)};
  r.on = on;
  return r;
}

SettingRow choice(std::string key, std::string title, std::string about, std::vector<std::string> choices, int selected) {
  SettingRow r{Kind::choice, std::move(key), std::move(title), std::move(about)};
  r.choices = std::move(choices), r.selected = selected;
  return r;
}

SettingRow valued(Kind kind, std::string key, std::string title, std::string about, std::string value) {
  SettingRow r{kind, std::move(key), std::move(title), std::move(about)};
  r.value = std::move(value);
  return r;
}

std::vector<SettingRow> translation(const Settings &s, const SettingsState &st) {
  std::vector<SettingRow> rows;
  if (!st.has_pair)
    rows.push_back(valued(Kind::button, "setup", tr("setup_none_t"), tr("setup_none_d"), tr("setup_btn")));
  std::vector<std::string> langs;
  auto codes = answer_codes(s, st);
  for (auto &c : codes) langs.push_back(language_name(c));
  rows.push_back(choice("answer", tr("answer_t"), tr("answer_d"), langs, index_of(codes, s.answer)));
  rows.push_back(choice("method", tr("mt_t"), tr("method_d"), names(methods), index_of(methods, s.method ? std::string(method_name(*s.method)) : std::string())));
  rows.push_back(toggle("mt_english", tr("mt_english_t"), tr("mt_english_about"), s.mt_english));
  if (st.has_pair) rows.push_back(valued(Kind::button, "setup", tr("firstrun_t"), tr("firstrun_d"), tr("open_btn")));
  return rows;
}

std::vector<SettingRow> opening(const Settings &s) {
  return {choice("phrase_open", tr("phrase_open"), tr("phrase_open_about"), names(phrase_opens), index_of(phrase_opens, s.card.phrase_open))};
}

std::vector<SettingRow> article(const Settings &s) {
  std::vector<SettingRow> rows{choice("full", tr("dict_view_t"), tr("dict_view_d"), names({"dict_short", "dict_full_entries"}), s.card.full ? 1 : 0)};
  rows.push_back(heading(tr("pack_use")));
  rows.push_back(toggle("grammar", tr("pack_gram_t"), tr("pack_gram_d"), s.card.grammar));
  for (auto &key : sections) {
    bool on = std::find(s.card.sections.begin(), s.card.sections.end(), key) != s.card.sections.end();
    rows.push_back(toggle("section." + key, tr("sec_" + key), tr("sec_" + key + "_d"), on));
  }
  rows.push_back(heading(tr("lang_own_tab")));
  rows.push_back(valued(Kind::button, "order", tr("order_t"), tr("order_about"), tr("open_btn")));
  return rows;
}

std::vector<SettingRow> appearance(const Settings &s) {
  std::vector<std::string> size_names, height_names;
  for (auto n : sizes) size_names.push_back(tr("unit_pt", {std::to_string(n)}));
  for (auto n : heights) height_names.push_back(std::to_string(n) + " %");
  return {choice("width", tr("look_width"), tr("width_d"), names(width_names), index_of(widths, s.card.width)),
          choice("text", tr("look_text"), tr("look_text_d"), size_names, index_of(sizes, s.card.text)),
          choice("font", tr("look_font"), tr("font_d"), names(fonts), index_of(fonts, s.card.font)),
          choice("height", tr("height_t"), tr("height_d"), height_names, index_of(heights, s.card.height))};
}

std::vector<SettingRow> general(const Settings &s, const SettingsState &st) {
  std::vector<SettingRow> rows{heading(tr("hotkeys_t")),
                               valued(Kind::hotkey, "lookup", tr("hk_lookup_t"), tr("hk_lookup_d"), ""),
                               valued(Kind::hotkey, "input", tr("hk_input_t"), tr("hk_input_d"), ""),
                               heading(tr("tab_card")),
                               toggle("selection_icon", tr("icon_t"), tr("icon_d"), s.selection_icon),
                               toggle("pin_moved", tr("pin_moved_t"), tr("pin_moved_d"), s.pin_moved),
                               heading(tr("apps_t")),
                               valued(Kind::apps, "copy_apps", tr("copy_apps_t"), tr("copy_apps_d"), ""),
                               valued(Kind::apps, "ignore_apps", tr("ignore_apps_t"), tr("ignore_apps_d"), ""),
                               heading(tr("system_t")),
                               toggle("open_at_login", tr("login_t"), tr("login_d"), st.login)};
  auto access = tr("access_d", {st.input});
  if (st.trusted) {
    auto r = valued(Kind::status, "access", tr("access_t"), access, tr("allowed"));
    r.on = true;
    rows.push_back(r);
  } else {
    rows.push_back(valued(Kind::button, "access", tr("access_t"), access, tr("allow_btn")));
  }
  std::vector<std::string> langs{tr("ui_lang_system")};
  for (size_t i = 1; i < ui_langs.size(); i++) langs.push_back(ui_langs[i].second);
  rows.push_back(choice("ui_lang", tr("ui_lang"), tr("ui_lang_about"), langs, index_of(ui_langs, s.ui_lang)));
  rows.push_back(heading(tr("updates_t")));
  auto upd = valued(Kind::button, "updates", tr("check_t"), st.checking ? tr("checking") : st.update.empty() ? tr("update_never") : st.update,
                    tr("check_now"));
  upd.enabled = !st.checking;
  rows.push_back(upd);
  if (!st.update_url.empty()) rows.push_back(valued(Kind::link, "release", tr("update_get", {st.update_version}), tr("update_get_d"), st.update_url));
  rows.push_back(heading(tr("ht_about")));
  rows.push_back(valued(Kind::status, "version", "LexiPeek " + st.version, tr("about_d"), ""));
  rows.push_back(valued(Kind::link, "source", tr("source_t"), "github.com/kafeg/lexipeek", "https://github.com/kafeg/lexipeek"));
  rows.push_back(valued(Kind::link, "inkpeek", "InkPeek", tr("inkpeek_d"), "https://github.com/kafeg/inkpeek"));
  rows.push_back(heading(tr("licenses_t")));
  for (auto &[title, about, url] : licenses) rows.push_back(valued(Kind::link, "license", title, tr(about), url));
  return rows;
}

}  // namespace

std::vector<std::string> settings_sections() { return names({"tab_dicts", "tab_card", "tab_words", "sec_general"}); }

std::vector<std::string> settings_tab_keys(SettingsSection section) {
  switch (section) {
    case SettingsSection::languages: return {"packs", "translation", "other-dictionaries"};
    case SettingsSection::card: return {"opening", "article", "appearance"};
    case SettingsSection::notes: return {"words", "phrases", "history"};
    case SettingsSection::general: return {};
  }
  return {};
}

std::vector<std::string> settings_tabs(SettingsSection section) {
  switch (section) {
    case SettingsSection::languages: return names({"lang_packs", "lang_translation", "lang_own"});
    case SettingsSection::card: return names({"card_open", "lang_article", "card_look"});
    case SettingsSection::notes: return names({"list_words", "list_phrases", "list_history"});
    case SettingsSection::general: return {};
  }
  return {};
}

SettingsPage settings_view(SettingsSection section, int tab, const Settings &s, const SettingsState &st) {
  SettingsPage p;
  p.sections = settings_sections();
  p.tabs = settings_tabs(section);
  p.tab = p.tabs.empty() ? 0 : std::clamp(tab, 0, static_cast<int>(p.tabs.size()) - 1);
  switch (section) {
    case SettingsSection::languages:
      if (p.tab == 0) p.pane = "packs";
      else if (p.tab == 1) p.rows = translation(s, st);
      else p.pane = "other";
      break;
    case SettingsSection::card:
      p.rows = p.tab == 0 ? opening(s) : p.tab == 1 ? article(s) : appearance(s);
      break;
    case SettingsSection::notes: p.pane = "notes"; break;
    case SettingsSection::general: p.rows = general(s, st); break;
  }
  return p;
}

void settings_toggle(Settings &s, const std::string &key, bool on) {
  if (key == "mt_english") s.mt_english = on;
  else if (key == "grammar") s.card.grammar = on;
  else if (key == "selection_icon") s.selection_icon = on;
  else if (key == "pin_moved") s.pin_moved = on;
  else if (key == "open_at_login") s.open_at_login = on;
  else if (key.starts_with("section.")) {
    auto k = key.substr(8);
    auto &v = s.card.sections;
    v.erase(std::remove(v.begin(), v.end(), k), v.end());
    if (on) {
      // Kept in the card's order (rel, syn, expr, forms, ety).
      std::vector<std::string> all;
      for (auto &x : sections)
        if (x == k || std::find(v.begin(), v.end(), x) != v.end()) all.push_back(x);
      v = all;
    }
  }
}

void settings_choose(Settings &s, const std::string &key, int i, const SettingsState &st) {
  auto at = [&](const auto &xs) { return xs.at(static_cast<size_t>(std::clamp(i, 0, static_cast<int>(xs.size()) - 1))); };
  if (key == "answer") s.answer = at(answer_codes(s, st));
  else if (key == "method") s.method = method_from(at(methods).first);
  else if (key == "phrase_open") s.card.phrase_open = at(phrase_opens).first;
  else if (key == "full") s.card.full = i == 1;
  else if (key == "width") s.card.width = at(widths);
  else if (key == "text") s.card.text = at(sizes);
  else if (key == "font") s.card.font = at(fonts).first;
  else if (key == "height") s.card.height = at(heights);
  else if (key == "ui_lang") s.ui_lang = at(ui_langs).first;
}

std::string packs_summary(const std::vector<PairRow> &rows) {
  auto n = std::count_if(rows.begin(), rows.end(), [](const PairRow &r) { return r.installed; });
  return n == 0 ? tr("packs_none") : trn("packs_installed", n);
}

std::string pair_detail(const PairRow &r) {
  std::vector<std::string> parts;
  if (!r.level.empty()) parts.push_back(tr("level_" + r.level));
  if (r.installed) {
    parts.push_back("Mozilla");
    for (auto *p : {&r.direct, &r.llm})
      if (*p && (*p)->installed) parts.push_back((*p)->title);
  } else {
    parts.push_back(size_text(r.missing));
  }
  std::string out;
  for (auto &x : parts) out += (out.empty() ? "" : " · ") + x;
  return out;
}

}  // namespace lexipeek
