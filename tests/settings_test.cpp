// Settings' panes from the core, their changes, and the card's settings honoured by the card.
#include <doctest/doctest.h>
#include "temp.hpp"
#include "lexipeek/l10n.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "lexipeek/app.hpp"
#include "lexipeek/views.hpp"
#include "text/text.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;
using Kind = SettingRow::Kind;

namespace {

const SettingRow *row(const SettingsPage &p, const std::string &key) {
  for (auto &r : p.rows)
    if (r.key == key) return &r;
  return nullptr;
}

SettingsState state() {
  SettingsState st;
  st.trusted = true;
  st.version = "0.1.305";
  st.answers = {"en", "ru"};
  return st;
}

Card word() {
  Card c;
  c.kind = Card::Kind::word;
  c.text = "Häusern", c.lang = "de", c.answer = "ru";
  c.header = "das Haus (Hauses, pl. Häuser)";
  c.base = "Haus";
  Entry e{"InkPeek · de → ru", "дом", "<b>Haus</b> дом", "de-ru", "Haus"};
  e.articles = {{"Haus", "das Haus — дом", {{"дом", {}}}}};
  e.related.rel = {"Krankenhaus"};
  e.related.syn = {"Gebäude"};
  e.related.expr = {"zu Hause"};
  e.related.origin = "germanisch";
  c.entries = {e};
  return c;
}

}  // namespace

TEST_CASE("settings: the card's settings and the app language round trip") {
  auto d = fs::temp_directory_path() / "lexipeek-settings-card";
  wipe(d);
  auto s = load_settings(d);
  CHECK(s.card.phrase_open == "offline");
  CHECK(!s.card.full);
  CHECK(s.card.grammar);
  CHECK(s.card.sections == std::vector<std::string>{"rel", "syn", "expr", "forms"});
  CHECK(s.card.width == 400);
  CHECK(s.card.height == 45);
  s.card = {"dict", true, false, {"rel"}, 460, 16, "serif", 60};
  s.ui_lang = "en";
  REQUIRE(save_settings(d, s).empty());
  auto t = load_settings(d);
  CHECK(t.card.phrase_open == "dict");
  CHECK(t.card.full);
  CHECK(!t.card.grammar);
  CHECK(t.card.sections == std::vector<std::string>{"rel"});
  CHECK(t.card.width == 460);
  CHECK(t.card.text == 16);
  CHECK(t.card.font == "serif");
  CHECK(t.card.height == 60);
  CHECK(t.ui_lang == "en");
  wipe(d);
}

TEST_CASE("settings: sections and tabs as InkPeek's, a pane each") {
  Settings s;
  auto st = state();
  auto p = settings_view(SettingsSection::languages, 0, s, st);
  CHECK(p.sections == std::vector<std::string>{"Languages", "Card", "AI", "General", "Read aloud", "Sync"});
  CHECK(p.tabs == std::vector<std::string>{"Packs", "Translation", "Other dictionaries"});
  CHECK(p.pane == "packs");
  CHECK(settings_view(SettingsSection::languages, 2, s, st).pane == "other");
  CHECK(settings_view(SettingsSection::card, 9, s, st).tab == 2);  // clamped
  CHECK(settings_view(SettingsSection::general, 0, s, st).tabs == std::vector<std::string>{"System", "Hotkeys"});
  // Every row has a title and a one-line description, but headings.
  s.ai.provider = "custom";
  for (auto sec : {SettingsSection::languages, SettingsSection::card, SettingsSection::ai, SettingsSection::general, SettingsSection::sync})
    for (int tab = 0; tab < 3; tab++)
      for (auto &r : settings_view(sec, tab, s, st).rows) {
        CHECK(!r.title.empty());
        if (r.kind != Kind::heading) CHECK_MESSAGE(!r.about.empty(), r.title);
      }
}

TEST_CASE("settings: Sync → Sync — what syncs, the folder, its state, this device, the hint, the backups") {
  Settings s;
  auto st = state();
  st.sync.device = "mac-k3fw2xqa", st.sync.name = "MacBook Air";
  auto p = settings_view(SettingsSection::sync, 0, s, st);
  CHECK(p.tabs == std::vector<std::string>{"Sync", "Anki"});
  std::vector<std::string> keys;
  for (auto &r : p.rows) keys.push_back(r.kind == Kind::heading ? "#" + r.title : r.key);
  CHECK(keys == std::vector<std::string>{"#What syncs", "sync_folder", "sync_device", "#Backups", "backup_export", "backup_import",
                                         "#Keeping it the same everywhere"});
  CHECK(p.rows[0].about.find("no conflicts") != std::string::npos);
  CHECK(row(p, "sync_folder")->value == "Choose…");
  CHECK(row(p, "sync_folder")->about.starts_with("None"));
  CHECK(row(p, "sync_device")->value == "mac-k3fw2xqa");
  CHECK(row(p, "sync_device")->about == "MacBook Air");
  CHECK(p.rows.back().about.find("Syncthing works best") != std::string::npos);
  CHECK(p.rows.back().about.find("NAS or cloud drive") != std::string::npos);
  CHECK(p.rows.back().about.find("Synology") == std::string::npos);
  CHECK(row(p, "backup_export")->value == "Export…");
  CHECK(row(p, "backup_import")->value == "Import…");
  st.backup_said = "Added: 3";
  CHECK(row(settings_view(SettingsSection::sync, 0, s, st), "backup_said")->about == "Added: 3");
  st.sync = {"/Volumes/Sync", true, 1793491200000, 0, "", "mac-k3fw2xqa", "MacBook Air"};
  p = settings_view(SettingsSection::sync, 0, s, st);
  CHECK(row(p, "sync_folder")->value == "Change…");
  CHECK(row(p, "sync_folder")->about == "/Volumes/Sync");
  CHECK(row(p, "sync_state")->value == "Read " + text::local_time(1793491200000));
  CHECK(row(p, "sync_state")->on);
  st.sync.wrote = 1793491100000;
  CHECK(row(settings_view(SettingsSection::sync, 0, s, st), "sync_state")->value ==
        "Read " + text::local_time(1793491200000) + " · written " + text::local_time(1793491100000));
  st.sync.reachable = false;
  CHECK(row(settings_view(SettingsSection::sync, 0, s, st), "sync_state")->value == "Not reachable now: changes wait here");
  CHECK(!row(settings_view(SettingsSection::sync, 0, s, st), "sync_state")->on);
}

TEST_CASE("settings: Translation — answer, method, the English, Set Up while no pair") {
  Settings s;
  auto st = state();
  auto p = settings_view(SettingsSection::languages, 1, s, st);
  REQUIRE(row(p, "answer"));
  CHECK(row(p, "answer")->choices == std::vector<std::string>{"English", "Russian"});
  CHECK(row(p, "answer")->selected == 1);
  CHECK(row(p, "mt_english")->kind == Kind::toggle);
  // The methods as cards (InkPeek's MethodPane): needs, downloaded, why not here.
  st.device = {16384, {"asimddp"}};
  MethodInfo fast, direct, llm;
  direct.method = Method::direct, direct.models = 2, direct.size = 610'000'000, direct.missing = {"opus-a"}, direct.left = 300'000'000;
  direct.bare = {"de→pl"};
  llm.method = Method::llm, llm.cannot = "Needs 6 GB of memory; this machine has 4.0 GB.";
  st.methods = {fast, direct, llm};
  p = settings_view(SettingsSection::languages, 1, s, st);
  std::vector<SettingRow> cards;
  for (auto &r : p.rows)
    if (r.kind == Kind::method) cards.push_back(r);
  REQUIRE(cards.size() == 4);
  CHECK(cards[0].value == "off");  // InkPeek's Off first
  CHECK(cards[0].selected == 4);
  CHECK(cards[2].selected == 2);  // direct
  CHECK(!cards[0].on);
  CHECK(!cards[1].on);
  CHECK(cards[2].on);  // the default: OPUS-MT
  CHECK(cards[1].detail.empty());
  CHECK(cards[2].value == "direct");
  CHECK(cards[2].detail == "Models for your pairs: 2 · 610 MB\nNo model for de→pl: Mozilla translates them.");
  CHECK(cards[2].ids == std::vector<std::string>{"opus-a"});
  CHECK(cards[2].button == "Download 300 MB");
  CHECK(!cards[3].enabled);
  CHECK(cards[3].detail == llm.cannot);
  CHECK(cards[3].ids.empty());
  settings_choose(s, "method", 4, st);
  CHECK(s.mt_off);
  CHECK(!s.method);
  p = settings_view(SettingsSection::languages, 1, s, st);
  CHECK(row(p, "method")->on);  // Off picked
  settings_choose(s, "method", 1, st);
  CHECK(!s.mt_off);
  CHECK(s.method == Method::fast);
  st.downloading = "direct", st.progress = 0.5;
  p = settings_view(SettingsSection::languages, 1, s, st);
  for (auto &r : p.rows)
    if (r.kind == Kind::method) CHECK(r.progress == (r.value == "direct" ? 0.5 : -1));
  st = state();
  CHECK(!row(p, "setup"));  // a pair installed: another one comes from Packs
  st.has_pair = false;
  p = settings_view(SettingsSection::languages, 1, s, st);
  CHECK(p.rows.front().key == "setup");
  CHECK(p.rows.front().value == "Set Up…");
  settings_choose(s, "answer", 0, st);
  CHECK(s.answer == "en");
  settings_choose(s, "method", 2, st);
  CHECK(s.method == Method::direct);
  settings_choose(s, "method", 0, st);
  CHECK(!s.method);
  settings_toggle(s, "mt_english", true);
  CHECK(s.mt_english);
  // An answer the packs do not offer stays first in the list.
  s.answer = "uk";
  p = settings_view(SettingsSection::languages, 1, s, st);
  CHECK(row(p, "answer")->choices.front() == "Ukrainian");
  CHECK(row(p, "answer")->selected == 0);
}

TEST_CASE("settings: Card — opening, article, sections, appearance") {
  Settings s;
  auto st = state();
  auto p = settings_view(SettingsSection::card, 0, s, st);
  CHECK(row(p, "phrase_open")->choices == std::vector<std::string>{"Translation, offline", "Dictionary, word by word"});
  settings_choose(s, "phrase_open", 1, st);
  CHECK(s.card.phrase_open == "dict");
  p = settings_view(SettingsSection::card, 1, s, st);
  CHECK(row(p, "full")->selected == 0);
  CHECK(row(p, "grammar")->on);
  CHECK(row(p, "section.rel")->on);
  CHECK(!row(p, "section.ety")->on);
  settings_toggle(s, "section.ety", true);
  settings_toggle(s, "section.rel", false);
  CHECK(s.card.sections == std::vector<std::string>{"syn", "expr", "forms", "ety"});
  settings_toggle(s, "section.rel", true);
  CHECK(s.card.sections == std::vector<std::string>{"rel", "syn", "expr", "forms", "ety"});
  CHECK(row(p, "order")->kind == Kind::button);  // the order itself: Languages → Other dictionaries
  p = settings_view(SettingsSection::card, 2, s, st);
  CHECK(row(p, "width")->selected == 1);
  CHECK(row(p, "text")->choices[1] == "13 pt");
  CHECK(row(p, "height")->choices[1] == "45 %");
  settings_choose(s, "width", 3, st);
  settings_choose(s, "text", 4, st);
  settings_choose(s, "font", 2, st);
  settings_choose(s, "height", 99, st);
  CHECK(s.card.width == 520);
  CHECK(s.card.text == 16);
  CHECK(s.card.font == "rounded");
  CHECK(s.card.height == 75);
}

TEST_CASE("settings: General — System and Hotkeys tabs; the icon on Card → Opening; About its own rows") {
  Settings s;
  auto st = state();
  auto p = settings_view(SettingsSection::general, 1, s, st);
  CHECK(row(p, "lookup")->kind == Kind::hotkey);
  CHECK(row(p, "input")->kind == Kind::hotkey);
  CHECK(row(p, "ignore_apps")->kind == Kind::apps);
  CHECK(!row(p, "access"));
  p = settings_view(SettingsSection::general, 0, s, st);
  CHECK(row(p, "access")->kind == Kind::status);
  CHECK(row(p, "access")->value == "Allowed");
  CHECK(row(p, "open_at_login"));
  CHECK(!row(p, "keep_model"));  // beside the translation's methods
  CHECK(row(settings_view(SettingsSection::languages, 1, s, st), "keep_model")->title == row(settings_view(SettingsSection::speech, 0, s, st), "speech_keep")->title);
  CHECK(row(p, "ui_lang")->choices.size() == 9);
  CHECK(row(p, "ui_lang")->choices[0] == "Same as System");
  CHECK(row(p, "ui_lang")->choices[2] == "Русский");
  CHECK(!row(p, "updates"));
  st.trusted = false;
  CHECK(row(settings_view(SettingsSection::general, 0, s, st), "access")->kind == Kind::button);
  st.trusted = true;
  p = settings_view(SettingsSection::card, 0, s, st);
  CHECK(row(p, "selection_icon"));
  CHECK(row(p, "pin_moved"));
  settings_toggle(s, "selection_icon", true);
  CHECK(s.keep_model == 5);
  settings_choose(s, "keep_model", 4, {});
  CHECK(s.keep_model == 0);
  settings_toggle(s, "pin_moved", true);
  settings_choose(s, "ui_lang", 1, st);
  CHECK(s.selection_icon);
  CHECK(s.pin_moved);
  CHECK(s.ui_lang == "en");
  settings_choose(s, "ui_lang", 8, st);
  CHECK(s.ui_lang == "pl");
  settings_choose(s, "ui_lang", 0, st);
  CHECK(s.ui_lang.empty());
  // About: version, the update check, links, licenses.
  auto a = about_rows(st);
  auto in = [&](const std::string &key) -> const SettingRow * {
    for (auto &r : a)
      if (r.key == key) return &r;
    return nullptr;
  };
  CHECK(in("version")->title == "LexiPeek 0.1.305");
  CHECK(in("source")->value == "https://github.com/kafeg/lexipeek");
  CHECK(in("updates")->enabled);
  CHECK(in("updates")->about == "Not checked yet.");
  CHECK(!in("release"));
  CHECK(std::count_if(a.begin(), a.end(), [](auto &r) { return r.key == "license"; }) == 0);
  CHECK(about_tabs() == std::vector<std::string>{"About", "Licenses"});
  auto l = about_rows(st, 1);
  CHECK(std::count_if(l.begin(), l.end(), [](auto &r) { return r.key == "license"; }) == 7);
  st.checking = true;
  a = about_rows(st);
  CHECK(!in("updates")->enabled);
  st.checking = false;
  st.update = "LexiPeek 0.2 is out", st.update_version = "0.2", st.update_url = "https://github.com/kafeg/lexipeek/releases/tag/v0.2";
  a = about_rows(st);
  CHECK(in("updates")->about == "LexiPeek 0.2 is out");
  CHECK(in("release")->title == "Download LexiPeek 0.2");
  CHECK(in("release")->value == st.update_url);
}

TEST_CASE("updates: GitHub's latest release against this version") {
  CHECK(newer_version("v0.10", "0.9"));
  CHECK(newer_version("v1.0.1", "1.0"));
  CHECK(!newer_version("v0.1", "0.1"));
  CHECK(!newer_version("0.1.0", "0.1"));
  CHECK(!newer_version("v0.1", "0.2"));
  CHECK(update_check("0.1", 404, "{\"message\":\"Not Found\"}", "The requested URL returned error: 404").text == "No releases yet.");
  CHECK(update_check("0.1", 0, "", "Could not resolve host").text == "Failed: Could not resolve host");
  auto r = update_check("0.1", 200, R"({"tag_name":"v0.2","html_url":"https://github.com/kafeg/lexipeek/releases/tag/v0.2"})", "");
  CHECK(r.text == "LexiPeek 0.2 is out");
  CHECK(r.version == "0.2");
  CHECK(r.url == "https://github.com/kafeg/lexipeek/releases/tag/v0.2");
  r = update_check("0.2", 200, R"({"tag_name":"v0.2"})", "");
  CHECK(r.text == "This is the latest version (0.2).");
  CHECK(r.url.empty());
  CHECK(update_check("0.1", 200, "<html>", "").text == "Failed: GitHub");
}

TEST_CASE("settings: the card honours them — opening, full entry, grammar, sections, look") {
  auto c = word();
  auto s = first_state(c);
  CHECK(!s.full_entry);
  auto v = card_view(c, s);
  CHECK(v.title == "das Haus (Hauses, pl. Häuser)");
  CHECK(v.width == 400);
  CHECK(v.text == 13);
  CHECK(v.height == 45);
  c.prefs = {"offline", true, false, {"syn"}, 360, 16, "serif", 60};
  s = first_state(c);
  CHECK(s.full_entry);
  v = card_view(c, s);
  CHECK(v.title == "Haus");  // no grammar: the headword
  std::vector<std::string> sections;
  for (auto &r : v.rows)
    if (r.style == CardRow::Style::section) sections.push_back(r.open);
  CHECK(sections == std::vector<std::string>{"syn"});
  CHECK(v.width == 360);
  CHECK(v.text == 16);
  CHECK(v.font == "serif");
  CHECK(v.height == 60);
  // A phrase: Translate, or word by word when set so; a passage always Translate.
  Card ph;
  ph.kind = Card::Kind::phrase;
  ph.text = "Ich mache die Tür zu", ph.lang = "de", ph.answer = "ru";
  ph.gloss = {{"Tür", "Tür", "дверь", "die Tür (Tür, мн. Türen)"}};
  CHECK(first_tab(ph) == CardTab::translate);
  ph.prefs.phrase_open = "dict";
  CHECK(first_tab(ph) == CardTab::dictionary);
  auto pv = card_view(ph, first_state(ph));
  CHECK(pv.rows[0].label == "die Tür (Tür, мн. Türen)");
  ph.prefs.grammar = false;
  CHECK(card_view(ph, first_state(ph)).rows[0].label == "Tür");
  ph.kind = Card::Kind::passage;
  CHECK(first_tab(ph) == CardTab::translate);
  CHECK(card_view(ph, first_state(ph)).width == 460);
}

TEST_CASE("settings: the facade writes a row's change to settings.json at once") {
  auto d = fs::temp_directory_path() / "lexipeek-settings-app";
  wipe(d);
  auto st = state();
  CHECK(app::toggle(d.string(), "grammar", false).empty());
  CHECK(app::choose(d.string(), "height", 0, st).empty());
  auto s = load_settings(d);
  CHECK(!s.card.grammar);
  CHECK(s.card.height == 30);
  auto p = app::settings_page(d.string(), SettingsSection::card, 1, st);
  CHECK(!row(p, "grammar")->on);
  wipe(d);
}

TEST_CASE("settings: the packs' summary and a pair's second line") {
  PairRow a{"de", "ru", "German → Russian", "full", true, 0, {}, {PackPart{"opus-x", "opus", "OPUS-MT", 1, true, false, ""}}};
  PairRow b{"fr", "ru", "French → Russian", "basic", false, 84 * 1000 * 1000, {}, {}};
  CHECK(packs_summary({}) == "No language pairs installed");
  CHECK(packs_summary({a, b}) == "1 language pair installed");
  CHECK(packs_summary({a, a}) == "2 language pairs installed");
  CHECK(pair_detail(a) == "Full · Mozilla · OPUS-MT");
  CHECK(pair_detail(b) == "Basic · " + size_text(b.missing));
}

TEST_CASE("settings: offline translation off is kept as \"off\"") {
  auto d = fs::temp_directory_path() / "lexipeek-mt-off";
  wipe(d);
  Settings s;
  s.mt_off = true;
  CHECK(save_settings(d, s).empty());
  CHECK(load_settings(d).mt_off);
  CHECK(!load_settings(d).method);
  wipe(d);
}

TEST_CASE("settings: the hotkeys' default is Win+Shift on Windows, Ctrl+Alt on Linux, a saved one stays") {
  auto d = fs::temp_directory_path() / "lexipeek-settings-hotkeys";
  wipe(d);
  fs::create_directories(d);
  set_ui_platform("windows");
  auto s = load_settings(d);
  CHECK(s.lookup == Hotkey{"D", false, true, false, true});
  CHECK(s.input == Hotkey{"A", false, true, false, true});
  s.lookup = Hotkey{"K", true, false, false, false};
  CHECK(save_settings(d, s).empty());
  CHECK(load_settings(d).lookup == Hotkey{"K", true, false, false, false});
  set_ui_platform("linux");
  wipe(d);
  CHECK(load_settings(d).lookup == Hotkey{"D", true, false, true, false});  // Ctrl+Alt+D
  CHECK(load_settings(d).input == Hotkey{"A", true, false, true, false});
  set_ui_platform("mac");
  wipe(d);
  CHECK(load_settings(d).lookup == Hotkey{"D", true, false, false, false});
}

TEST_CASE("settings: every pane's toggles and choices round trip through settings.json, each index shown back") {
  set_ui_language("en");
  auto d = fs::temp_directory_path() / "lexipeek-settings-rows";
  wipe(d);
  fs::create_directories(d);
  std::ofstream(d / "settings.json") << R"({"ai": {"provider": "anthropic"}, "speech": {"on": true}})";
  auto st = state();
  st.answers = {"en", "ru", "de"};
  st.pairs = {{"ru", "de"}, {"ru", "en"}, {"de", "ru"}, {"en", "ru"}};
  st.installed_pairs = {{"ru", "de"}, {"ru", "en"}};
  auto ds = d.string();
  int toggles = 0, choices = 0;
  for (auto sec : {SettingsSection::languages, SettingsSection::card, SettingsSection::ai, SettingsSection::general, SettingsSection::speech,
                   SettingsSection::sync}) {
    auto tabs = std::max<size_t>(1, settings_tabs(sec).size());
    for (int tab = 0; tab < static_cast<int>(tabs); tab++) {
      // A row of the pane as it is on disk now (a copy: the page is made again each time).
      auto now = [&](const std::string &key) {
        auto p = app::settings_page(ds, sec, tab, st);
        auto *r = row(p, key);
        REQUIRE(r);
        return *r;
      };
      auto rows = app::settings_page(ds, sec, tab, st).rows;
      for (auto &r : rows) {
        CAPTURE(r.key);
        // Open at login shows the system's say (SettingsState::login), not settings.json.
        if (r.kind == Kind::toggle && r.enabled && r.key != "open_at_login") {
          toggles++;
          CHECK(app::toggle(ds, r.key, !r.on).empty());
          CHECK(now(r.key).on == !r.on);
          CHECK(app::toggle(ds, r.key, r.on).empty());
          CHECK(now(r.key).on == r.on);
        } else if (r.kind == Kind::choice && r.enabled && r.key != "ai_preset") {
          for (int i = 0; i < static_cast<int>(r.choices.size()); i++) {
            CAPTURE(i);
            choices++;
            CHECK(app::choose(ds, r.key, i, st).empty());
            CHECK(now(r.key).selected == i);
          }
          CHECK(app::choose(ds, r.key, r.selected, st).empty());
          CHECK(now(r.key).selected == r.selected);
        }
      }
    }
  }
  CHECK(toggles > 20);
  CHECK(choices > 40);
  // A preset fills the server's address in, the provider stays.
  CHECK(app::choose(ds, "ai_provider", 4, st).empty());
  auto custom = app::settings(ds).ai.provider;
  CHECK(app::choose(ds, "ai_preset", 1, st).empty());
  CHECK(!app::settings(ds).ai.url.empty());
  CHECK(app::settings(ds).ai.provider == custom);
  // Every change above was written whole: what is on disk reads back the same.
  auto s = app::settings(ds);
  CHECK(app::save(ds, s).empty());
  CHECK(app::settings(ds).ai.provider == s.ai.provider);
  CHECK(app::settings(ds).speech.on);
  wipe(d);
}
