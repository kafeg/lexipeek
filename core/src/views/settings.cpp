// Settings' panes (InkPeek's MainActivity sections and *Settings.kt, its ⚙ page as General): rows and their changes.
#include <algorithm>
#include <array>
#include "lexipeek/ai.hpp"
#include "lexipeek/l10n.hpp"
#include "lexipeek/views.hpp"

namespace lexipeek {

namespace {

using Kind = SettingRow::Kind;

// Choices: the value and its text's key.
const std::vector<std::pair<std::string, std::string>> phrase_opens{{"offline", "open_offline"}, {"ai", "open_ai"},       {"dict", "open_dict"},
                                                                    {"analysis", "a_analysis"},  {"context", "a_context"}};
// The "method" choice's indexes: none (the first available), the methods, off.
const std::vector<std::string> methods{"", "fast", "direct", "llm", "off"};
const std::vector<int> widths{360, 400, 460, 520};
const std::vector<std::string> width_names{"width_narrow", "width_medium", "width_wide", "width_widest"};
const std::vector<int> sizes{12, 13, 14, 15, 16, 18};
const std::vector<std::pair<std::string, std::string>> fonts{{"", "ui_lang_system"}, {"serif", "font_serif"}, {"rounded", "font_rounded"}, {"mono", "font_mono"}};
const std::vector<int> heights{30, 45, 60, 75};
const std::vector<int> keeps_min{1, 5, 15, 60, 0};
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
  rows.push_back(toggle("mt_english", tr("mt_english_t"), tr("mt_english_about"), s.mt_english));
  if (st.has_pair) rows.push_back(valued(Kind::button, "setup", tr("firstrun_t"), tr("firstrun_d"), tr("open_btn")));
  // The methods as InkPeek's cards: what each does, what it needs for the installed pairs, why it cannot run here.
  auto gb = [](int mb) {
    char b[16];
    std::snprintf(b, sizeof b, "%.1f", mb / 1024.0);
    return std::string(b);
  };
  rows.push_back(heading(tr("mt_t"), st.ram_mb > 0 ? tr("mt_this", {gb(st.ram_mb), tr(st.dotprod ? "mt_yes" : "mt_no")}) : ""));
  auto picked = s.method.value_or(Method::fast);
  if (!st.methods.empty()) {
    SettingRow off{Kind::method, "method", tr("mt_off_t"), tr("mt_off_d")};
    off.value = "off", off.on = s.mt_off;
    rows.push_back(off);
  }
  for (auto &m : st.methods) {
    auto id = std::string(method_name(m.method));
    SettingRow r{Kind::method, "method", method_title(m.method), tr("mt_" + id + "_d")};
    r.value = id, r.on = !s.mt_off && m.method == picked, r.enabled = m.cannot.empty();
    std::vector<std::string> lines;
    if (!m.cannot.empty()) lines.push_back(m.cannot);
    else if (m.method != Method::fast && m.models > 0)
      lines.push_back(tr("mt_need", {digits(m.models), size_text(m.size)}) + (m.missing.empty() ? " · " + tr("mt_have") : ""));
    if (m.cannot.empty() && !m.bare.empty()) {
      std::string b;
      for (auto &x : m.bare) b += (b.empty() ? "" : ", ") + x;
      lines.push_back(tr("mt_none_for", {b}));
    }
    for (auto &l : lines) r.detail += (r.detail.empty() ? "" : "\n") + l;
    if (m.cannot.empty() && !m.missing.empty()) {
      r.ids = m.missing;
      r.button = tr("download_size", {size_text(m.left)});
    }
    if (st.downloading == id) r.progress = st.progress;
    rows.push_back(r);
  }
  if (!st.download_error.empty()) rows.push_back(valued(Kind::status, "download_error", tr("failed", {st.download_error}), "", ""));
  return rows;
}

// What a phrase can open on with these settings (InkPeek's Opening.choices): AI ones only when AI and the module are on.
std::vector<std::pair<std::string, std::string>> opens(const Settings &s) {
  bool on = ai::provider(s.ai.provider) != nullptr;
  std::vector<std::pair<std::string, std::string>> out;
  for (auto &o : phrase_opens) {
    if (o.first == "ai" && !(on && s.ai.translate && ai::module_on(ai::Ask::quick, s.ai))) continue;
    if (o.first == "analysis" && !(on && ai::module_on(ai::Ask::analysis, s.ai))) continue;
    if (o.first == "context" && !(on && ai::module_on(ai::Ask::context, s.ai))) continue;
    out.push_back(o);
  }
  return out;
}

std::vector<SettingRow> opening(const Settings &s) {
  auto o = opens(s);
  std::vector<std::string> titles;
  for (auto &x : o) titles.push_back(tr(x.second) + (x.first == "analysis" || x.first == "context" ? " · " + tr("tab_ai") : ""));
  return {choice("phrase_open", tr("phrase_open"), tr("phrase_open_about"), titles, index_of(o, s.card.phrase_open)),
          toggle("selection_icon", tr("icon_t"), tr("icon_d"), s.selection_icon),
          toggle("pin_moved", tr("pin_moved_t"), tr("pin_moved_d"), s.pin_moved)};
}

std::string mark(bool edited, const std::string &about) { return edited ? tr("prompt_edited") + " · " + about : about; }

bool edited(const Settings &s, const std::string &id) { return s.ai.prompts.contains(id); }

// AI → Connection (InkPeek's AiSettings.buildMain): provider, its server or program, key, model, answers, style, a test.
std::vector<SettingRow> connection(const Settings &s, const SettingsState &st) {
  auto &ps = ai::providers();
  std::vector<std::string> titles{tr("ai_none")};
  int at = 0;
  for (size_t i = 0; i < ps.size(); i++) {
    titles.push_back(ps[i].id == "custom" ? tr("ai_custom") : ps[i].title);
    if (ps[i].id == s.ai.provider) at = static_cast<int>(i) + 1;
  }
  std::vector<SettingRow> rows{choice("ai_provider", tr("ai_title"), tr("ai_title_d"), titles, at)};
  auto *p = ai::provider(s.ai.provider);
  if (!p) return rows;
  if (p->id == "custom") {
    std::vector<std::string> pre{"—"};
    for (auto &x : ai::presets()) pre.push_back(x.name);
    rows.push_back(choice("ai_preset", tr("preset_pick"), tr("server_d"), pre, 0));
    auto url = valued(Kind::text, "ai_url", tr("server_t"), tr("server_url"), s.ai.url);
    url.placeholder = ai::presets().front().url;
    rows.push_back(url);
  }
  if (p->cli) {
    auto i = s.ai.commands.find(p->id);
    auto cmd = valued(Kind::text, "ai_command", tr("ai_cli_t"), tr("ai_cli_d"), i == s.ai.commands.end() ? "" : i->second);
    cmd.placeholder = p->id == "codex-cli" ? "codex" : "claude";
    rows.push_back(cmd);
  } else {
    auto key = valued(Kind::secret, "ai_key", tr("key_t"), st.ai_key ? tr("key_saved") : p->key ? tr("key_missing") : tr("key_optional"), "");
    key.on = st.ai_key;
    rows.push_back(key);
  }
  auto i = s.ai.models.find(p->id);
  auto m = valued(Kind::model, "ai_model", tr("model_t"), tr(p->cli ? "model_cli_d" : "model_d"), i == s.ai.models.end() ? "" : i->second);
  m.placeholder = p->model;
  m.on = p->list;
  rows.push_back(m);
  rows.push_back(heading(tr("ai_answers_t")));
  rows.push_back(valued(Kind::button, "answer_lang", tr("ans_row", {language_name(s.answer)}), tr("ans_lang_d"), tr("open_btn")));
  auto style = valued(Kind::button, "edit", tr("prompt_system_t"), mark(edited(s, "system"), tr("prompt_system_d")), tr("edit_btn"));
  style.edit = "system";
  rows.push_back(style);
  auto test = valued(Kind::button, "ai_test", tr("ai_test_t"), st.ai_testing ? tr("ai_testing") : st.ai_test.empty() ? tr("ai_test_d") : st.ai_test,
                     tr("ai_test_btn"));
  test.enabled = !st.ai_testing;
  rows.push_back(test);
  return rows;
}

// AI → Modules (InkPeek's refreshModules): AI translation, then each module's parts with ✎, then asking ahead.
std::vector<SettingRow> modules(const Settings &s) {
  std::vector<SettingRow> rows{heading(tr("prompt_quick_t"))};
  bool quick = false;
  for (auto &k : ai::keys(ai::Ask::quick)) quick |= edited(s, "quick." + k.name);
  auto tr_row = toggle("ai_translate", tr("ai_tr_t"), mark(quick, tr("ai_tr_d")), s.ai.translate);
  tr_row.edit = "quick";
  rows.push_back(tr_row);
  for (auto a : {ai::Ask::analysis, ai::Ask::context}) {
    auto sec = ai::section(a);
    rows.push_back(heading(tr("a_" + sec), tr("a_" + sec + "_d")));
    auto on = ai::active(a, s.ai);
    for (auto &k : ai::keys(a)) {
      auto id = sec + "." + k.name;
      bool picked = std::any_of(on.begin(), on.end(), [&](auto &x) { return x.name == k.name; });
      auto r = toggle("ai_key." + id, tr("ak_" + k.name), mark(edited(s, id), tr("ak_" + k.name + "_d")), picked);
      r.edit = id;
      rows.push_back(r);
    }
  }
  rows.push_back(heading(tr("ai_requests_t")));
  rows.push_back(toggle("ai_prefetch", tr("prefetch_t"), tr("prefetch_d"), s.ai.prefetch));
  auto reset = valued(Kind::button, "ai_reset", tr("reset_prompts_t"), tr("reset_prompts_d"), tr("prompt_reset"));
  reset.enabled = !s.ai.prompts.empty();
  rows.push_back(reset);
  return rows;
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

// General (InkPeek's ⚙ page in desktop form): Hotkeys (the keys, per-app rules) · System (Accessibility, login, the
// app's language, the model in memory).
std::vector<SettingRow> general(const Settings &s, const SettingsState &st, int tab) {
  if (tab == 0)
    return {heading(tr("hotkeys_t")),
            valued(Kind::hotkey, "lookup", tr("hk_lookup_t"), tr("hk_lookup_d"), ""),
            valued(Kind::hotkey, "input", tr("hk_input_t"), tr("hk_input_d"), ""),
            heading(tr("apps_t")),
            valued(Kind::apps, "copy_apps", tr("copy_apps_t"), tr("copy_apps_d"), ""),
            valued(Kind::apps, "ignore_apps", tr("ignore_apps_t"), tr("ignore_apps_d"), "")};
  std::vector<SettingRow> rows;
  auto access = tr("access_d", {st.input});
  if (st.trusted) {
    auto r = valued(Kind::status, "access", tr("access_t"), access, tr("allowed"));
    r.on = true;
    rows.push_back(r);
  } else {
    rows.push_back(valued(Kind::button, "access", tr("access_t"), access, tr("allow_btn")));
  }
  rows.push_back(toggle("open_at_login", tr("login_t"), tr("login_d"), st.login));
  std::vector<std::string> langs{tr("ui_lang_system")};
  for (size_t i = 1; i < ui_langs.size(); i++) langs.push_back(ui_langs[i].second);
  rows.push_back(choice("ui_lang", tr("ui_lang"), tr("ui_lang_about"), langs, index_of(ui_langs, s.ui_lang)));
  std::vector<std::string> keeps;
  for (auto m : keeps_min) keeps.push_back(m == 0 ? tr("keep_forever") : m == 60 ? tr("keep_hour") : tr("keep_min", {digits(m)}));
  rows.push_back(choice("keep_model", tr("keep_t"), tr("keep_d"), keeps, index_of(keeps_min, s.keep_model)));
  return rows;
}

}  // namespace

std::vector<std::string> settings_sections() { return names({"tab_dicts", "tab_card", "tab_ai", "sec_general"}); }

std::vector<std::string> settings_tab_keys(SettingsSection section) {
  switch (section) {
    case SettingsSection::languages: return {"packs", "translation", "other-dictionaries"};
    case SettingsSection::card: return {"opening", "article", "appearance"};
    case SettingsSection::ai: return {"connection", "modules"};
    case SettingsSection::general: return {"keys", "system"};
  }
  return {};
}

std::vector<std::string> settings_tabs(SettingsSection section) {
  switch (section) {
    case SettingsSection::languages: return names({"lang_packs", "lang_translation", "lang_own"});
    case SettingsSection::card: return names({"card_open", "lang_article", "card_look"});
    case SettingsSection::ai: return names({"ai_connect", "ai_modules"});
    case SettingsSection::general: return names({"hotkeys_t", "system_t"});
  }
  return {};
}

std::vector<SettingRow> about_rows(const SettingsState &st) {
  std::vector<SettingRow> rows{valued(Kind::status, "version", "LexiPeek " + st.version, tr("about_d"), "")};
  auto upd = valued(Kind::button, "updates", tr("check_t"), st.checking ? tr("checking") : st.update.empty() ? tr("update_never") : st.update,
                    tr("check_now"));
  upd.enabled = !st.checking;
  rows.push_back(upd);
  if (!st.update_url.empty()) rows.push_back(valued(Kind::link, "release", tr("update_get", {st.update_version}), tr("update_get_d"), st.update_url));
  rows.push_back(valued(Kind::link, "source", tr("source_t"), "github.com/kafeg/lexipeek", "https://github.com/kafeg/lexipeek"));
  rows.push_back(valued(Kind::link, "inkpeek", "InkPeek", tr("inkpeek_d"), "https://github.com/kafeg/inkpeek"));
  rows.push_back(heading(tr("licenses_t")));
  for (auto &[title, about, url] : licenses) rows.push_back(valued(Kind::link, "license", title, tr(about), url));
  return rows;
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
    case SettingsSection::ai: p.rows = p.tab == 0 ? connection(s, st) : modules(s); break;
    case SettingsSection::general: p.rows = general(s, st, p.tab); break;
  }
  return p;
}

void settings_toggle(Settings &s, const std::string &key, bool on) {
  if (key == "mt_english") s.mt_english = on;
  else if (key == "grammar") s.card.grammar = on;
  else if (key == "selection_icon") s.selection_icon = on;
  else if (key == "pin_moved") s.pin_moved = on;
  else if (key == "open_at_login") s.open_at_login = on;
  else if (key == "ai_translate") s.ai.translate = on;
  else if (key == "ai_prefetch") s.ai.prefetch = on;
  else if (key.starts_with("ai_key.")) {
    // An answer key off or on: kept only when it differs from its default (an extra one is off).
    auto id = key.substr(7);
    auto dot = id.find('.');
    for (auto a : {ai::Ask::quick, ai::Ask::analysis, ai::Ask::context})
      for (auto &k : ai::keys(a))
        if (dot != std::string::npos && ai::section(a) == id.substr(0, dot) && k.name == id.substr(dot + 1)) {
          if (on == !k.extra) s.ai.picked.erase(id);
          else s.ai.picked[id] = on;
        }
  }
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
  // Behind a first "Off" / "Fill in from a preset…".
  auto at1 = [&](const auto &xs) { return xs.at(static_cast<size_t>(std::clamp(i - 1, 0, static_cast<int>(xs.size()) - 1))); };
  if (key == "answer") s.answer = at(answer_codes(s, st));
  else if (key == "method") {
    s.mt_off = at(methods) == "off";
    s.method = method_from(at(methods));
  }
  else if (key == "phrase_open") s.card.phrase_open = at(opens(s)).first;
  else if (key == "ai_provider") s.ai.provider = i <= 0 ? "" : at1(ai::providers()).id;
  else if (key == "ai_preset" && i > 0) s.ai.url = at1(ai::presets()).url;
  else if (key == "full") s.card.full = i == 1;
  else if (key == "width") s.card.width = at(widths);
  else if (key == "text") s.card.text = at(sizes);
  else if (key == "font") s.card.font = at(fonts).first;
  else if (key == "height") s.card.height = at(heights);
  else if (key == "ui_lang") s.ui_lang = at(ui_langs).first;
  else if (key == "keep_model") s.keep_model = at(keeps_min);
}

void settings_text(Settings &s, const std::string &key, const std::string &value) {
  auto v = value;
  while (!v.empty() && (v.back() == ' ' || v.back() == '\n')) v.pop_back();
  while (!v.empty() && v.front() == ' ') v.erase(0, 1);
  auto set = [&](std::map<std::string, std::string> &m) {
    if (v.empty()) m.erase(s.ai.provider);
    else m[s.ai.provider] = v;
  };
  if (key == "ai_url") s.ai.url = v;
  else if (key == "ai_command") set(s.ai.commands);
  else if (key == "ai_model") set(s.ai.models);
}

PromptEditor prompt_editor(const Settings &s, const std::string &id) {
  PromptEditor e;
  e.cancel = tr("cancel"), e.reset = tr("prompt_reset"), e.save = tr("save");
  auto field = [&](const std::string &fid, const std::string &label, const std::string &original) {
    auto i = s.ai.prompts.find(fid);
    e.fields.push_back({fid, label, i == s.ai.prompts.end() ? original : i->second, original});
  };
  if (id == "system") {
    e.title = tr("prompt_system_t"), e.note = tr("prompt_system_note");
    field("system", "", ai::system_prompt());
    return e;
  }
  e.note = tr("prompt_keys");
  if (id == "quick") {
    e.title = tr("prompt_quick_t") + " · " + tr("tab_ai");
    for (auto &k : ai::keys(ai::Ask::quick)) field("quick." + k.name, k.name, k.about);
    return e;
  }
  for (auto a : {ai::Ask::analysis, ai::Ask::context})
    for (auto &k : ai::keys(a))
      if (ai::section(a) + "." + k.name == id) {
        e.title = tr("ak_" + k.name) + " · " + tr("tab_ai");
        field(id, "", k.about);
      }
  return e;
}

void settings_prompts(Settings &s, const std::vector<std::string> &ids, const std::vector<std::string> &texts) {
  for (size_t i = 0; i < ids.size() && i < texts.size(); i++) {
    std::string original = ids[i] == "system" ? ai::system_prompt() : "";
    for (auto a : {ai::Ask::quick, ai::Ask::analysis, ai::Ask::context})
      for (auto &k : ai::keys(a))
        if (ai::section(a) + "." + k.name == ids[i]) original = k.about;
    auto t = texts[i];
    while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
    while (!t.empty() && (t.front() == ' ' || t.front() == '\n')) t.erase(0, 1);
    if (t.empty() || t == original) s.ai.prompts.erase(ids[i]);
    else s.ai.prompts[ids[i]] = t;
  }
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
