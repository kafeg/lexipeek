// Settings' panes (InkPeek's MainActivity sections and *Settings.kt, its ⚙ page as General): rows and their changes.
#include <algorithm>
#include <array>
#include "lexipeek/views.hpp"

namespace lexipeek {

namespace {

using Kind = SettingRow::Kind;

const std::vector<std::pair<std::string, std::string>> phrase_opens{{"offline", "Translation, offline"}, {"dict", "Dictionary, word by word"}};
const std::vector<std::pair<std::string, std::string>> methods{
    {"", "The first available"}, {"fast", "Fast · Mozilla"}, {"direct", "Direct · OPUS-MT"}, {"llm", "AI model · Hy-MT2"}};
const std::vector<int> widths{360, 400, 460, 520};
const std::vector<std::string> width_names{"Narrow", "Medium", "Wide", "Widest"};
const std::vector<int> sizes{12, 13, 14, 15, 16, 18};
const std::vector<std::pair<std::string, std::string>> fonts{{"", "As the system"}, {"serif", "Serif"}, {"rounded", "Rounded"}, {"mono", "Monospaced"}};
const std::vector<int> heights{30, 45, 60, 75};
const std::vector<std::pair<std::string, std::string>> ui_langs{{"", "As the system"}, {"en", "English"}};
// InkPeek's pack sections (CardSettings.buildArticle): key, title, about.
const std::vector<std::array<std::string, 3>> sections{
    {"rel", "Related words", "Same root: prefixed verbs (anmachen, zumachen), phrasal verbs (make up), derived words."},
    {"syn", "Synonyms", "Synonyms and antonyms, the frequent ones first."},
    {"expr", "Expressions and proverbs", "Set phrases and idioms with the word, from the dictionary."},
    {"forms", "Forms and pronunciation", "The table of forms and the IPA."},
    {"ety", "Origin", "Where the word comes from (etymology)."}};

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

template <class K>
std::vector<std::string> names(const std::vector<std::pair<K, std::string>> &xs) {
  std::vector<std::string> out;
  for (auto &x : xs) out.push_back(x.second);
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
    rows.push_back(valued(Kind::button, "setup", "No language pair installed yet", "Picks the first pair and downloads it, as on the first launch.", "Set Up…"));
  std::vector<std::string> langs;
  auto codes = answer_codes(s, st);
  for (auto &c : codes) langs.push_back(language_name(c));
  rows.push_back(choice("answer", "Translate into", "The answer language of cards, translations and Notes.", langs, index_of(codes, s.answer)));
  rows.push_back(choice("method", "Offline translation method",
                        "The card can switch it for one text; a method this Mac cannot run is dimmed there, with the reason.", names(methods),
                        index_of(methods, s.method ? std::string(method_name(*s.method)) : std::string())));
  rows.push_back(toggle("mt_english", "Show the English",
                        "Fast · Mozilla through English: that English, smaller, under the translation. Nothing is translated again.", s.mt_english));
  if (st.has_pair)
    rows.push_back(valued(Kind::button, "setup", "First run", "Accessibility, another pair and its download, as on the first launch.", "Open…"));
  return rows;
}

std::vector<SettingRow> opening(const Settings &s) {
  return {choice("phrase_open", "Phrase opens on", "A word with entries always opens on Dictionary, a passage on Translate.", names(phrase_opens),
                 index_of(phrase_opens, s.card.phrase_open))};
}

std::vector<SettingRow> article(const Settings &s) {
  std::vector<SettingRow> rows{choice("full", "Dictionary view", "Short lines, ⏎ for the full entry; or full entries with their sections.",
                                      {"Short lines", "Full entries"}, s.card.full ? 1 : 0)};
  rows.push_back(heading("From InkPeek language packs"));
  rows.push_back(toggle("grammar", "Grammar", "The card's header: article, plural, verb forms (das Haus, pl. Häuser).", s.card.grammar));
  for (auto &[key, title, about] : sections) {
    bool on = std::find(s.card.sections.begin(), s.card.sections.end(), key) != s.card.sections.end();
    rows.push_back(toggle("section." + key, title, about, on));
  }
  rows.push_back(heading("Dictionaries"));
  rows.push_back(valued(Kind::button, "order", "Dictionary order", "Packs and other dictionaries in one list: Languages → Other dictionaries.", "Open…"));
  return rows;
}

std::vector<SettingRow> appearance(const Settings &s) {
  std::vector<std::string> size_names, height_names;
  for (auto n : sizes) size_names.push_back(std::to_string(n) + " pt");
  for (auto n : heights) height_names.push_back(std::to_string(n) + " %");
  return {choice("width", "Width", "The card's width; a passage's is a little wider.", width_names, index_of(widths, s.card.width)),
          choice("text", "Text size", "The card's text; the header is a little larger.", size_names, index_of(sizes, s.card.text)),
          choice("font", "Font", "The card's text and its articles.", names(fonts), index_of(fonts, s.card.font)),
          choice("height", "Height limit", "The card grows with its text up to this share of the screen; the rest scrolls.", height_names,
                 index_of(heights, s.card.height))};
}

std::vector<SettingRow> general(const Settings &s, const SettingsState &st) {
  std::vector<SettingRow> rows{heading("Hotkeys"),
                               valued(Kind::hotkey, "lookup", "Translate selection", "The selected text of the app in front, in a card at the cursor.", ""),
                               valued(Kind::hotkey, "input", "Type text", "A card with an input field.", ""),
                               heading("Card"),
                               toggle("selection_icon", "Icon at the selection",
                                      "A small icon next to a new selection opens the card. Read by Accessibility; the clipboard is not touched.",
                                      s.selection_icon),
                               toggle("pin_moved", "Keep a moved card", "A card dragged elsewhere stays open, as pinned.", s.pin_moved),
                               heading("Apps"),
                               valued(Kind::apps, "copy_apps", "Read the selection with ⌘C in",
                                      "For apps that hide their selection from Accessibility; the clipboard is put back after.", ""),
                               valued(Kind::apps, "ignore_apps", "The hotkey and the icon do nothing in", "Apps with their own use for these keys.", ""),
                               heading("System"),
                               toggle("open_at_login", "Open at login", "LexiPeek starts with the Mac, in the menu bar.", st.login)};
  auto access = "Reads the selected text when you press the hotkey; nothing leaves this Mac. Without it, ⌥A, the Service and lexipeek:// links still work.";
  if (st.trusted) {
    auto r = valued(Kind::status, "access", "Accessibility", access, "Allowed");
    r.on = true;
    rows.push_back(r);
  } else {
    rows.push_back(valued(Kind::button, "access", "Accessibility", access, "Allow…"));
  }
  rows.push_back(choice("ui_lang", "App language", "Menus and settings. Other languages come with the app's translations.", names(ui_langs),
                        index_of(ui_langs, s.ui_lang)));
  rows.push_back(heading("Updates"));
  auto upd = valued(Kind::button, "updates", "Check for updates", "No releases are published yet.", "Check Now");
  upd.enabled = false;
  rows.push_back(upd);
  rows.push_back(heading("About"));
  rows.push_back(valued(Kind::status, "version", "LexiPeek " + st.version, "A pop-up dictionary and offline translator. MIT license.", ""));
  rows.push_back(valued(Kind::link, "source", "Source code", "github.com/kafeg/lexipeek", "https://github.com/kafeg/lexipeek"));
  rows.push_back(valued(Kind::link, "inkpeek", "InkPeek", "The sibling for e-ink readers; LexiPeek uses its language packs.", "https://github.com/kafeg/inkpeek"));
  return rows;
}

std::string capital(std::string s) {
  if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
  return s;
}

}  // namespace

std::vector<std::string> settings_sections() { return {"Languages", "Card", "Notes", "General"}; }

std::vector<std::string> settings_tabs(SettingsSection section) {
  switch (section) {
    case SettingsSection::languages: return {"Packs", "Translation", "Other dictionaries"};
    case SettingsSection::card: return {"Opening", "Article", "Appearance"};
    case SettingsSection::notes: return {"Words", "Phrases", "History"};
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
        if (x[0] == k || std::find(v.begin(), v.end(), x[0]) != v.end()) all.push_back(x[0]);
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
  return n == 0 ? "No language pairs installed" : n == 1 ? "1 language pair installed" : std::to_string(n) + " language pairs installed";
}

std::string pair_detail(const PairRow &r) {
  std::vector<std::string> parts;
  if (!r.level.empty()) parts.push_back(capital(r.level));
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
