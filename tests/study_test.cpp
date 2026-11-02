// Languages you learn (InkPeek's StudyTest and milestone 21): the list, the settings row, the card, the AI versions.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <fstream>
#include <thread>
#include <nlohmann/json.hpp>
#include "ai/ai.hpp"
#include "lexipeek/app.hpp"
#include "lexipeek/core.hpp"
#include "lexipeek/l10n.hpp"
#include "lexipeek/session.hpp"
#include "lexipeek/speech.hpp"
#include "lexipeek/study.hpp"
#include "lexipeek/views.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;
using V = std::vector<std::string>;

namespace {

void be32(std::string &s, uint32_t v) {
  for (int i = 3; i >= 0; i--) s += static_cast<char>((v >> (i * 8)) & 0xff);
}

// A pack dict-<from>-<to> with its articles (sorted as given) and, when given, its .rel lines.
void pack(const fs::path &data, const std::string &id, const std::vector<std::pair<std::string, std::string>> &words, const std::string &rel = "") {
  auto dir = data / "dicts" / id;
  fs::create_directories(dir);
  std::string idx, dict;
  for (auto &[w, a] : words) {
    idx += w + '\0';
    be32(idx, static_cast<uint32_t>(dict.size()));
    be32(idx, static_cast<uint32_t>(a.size()));
    dict += a;
  }
  std::ofstream(dir / (id + ".idx"), std::ios::binary) << idx;
  std::ofstream(dir / (id + ".dict"), std::ios::binary) << dict;
  std::ofstream(dir / (id + ".ifo")) << "StarDict's dict ifo file\nversion=3.0.0\nbookname=" << id << "\nwordcount=" << words.size()
                                     << "\nidxfilesize=" << idx.size() << "\nsametypesequence=h\n";
  if (!rel.empty()) std::ofstream(dir / (id + ".rel"), std::ios::binary) << rel;
}

std::string head(const std::string &line) { return "<p class=\"head\">" + line + "</p>"; }

// ru → de, ru → en and back; Haus is among the 1,000 most frequent German words.
fs::path study_data(const std::string &name) {
  auto d = fs::temp_directory_path() / ("lexipeek-study-" + name);
  wipe(d);
  pack(d, "dict-ru-de", {{"большой", head("большой — groß, weit")}, {"дом", head("дом (дома, pl. дома) — Haus, Gebäude (alt), Heim; Haus")}});
  pack(d, "dict-ru-en", {{"большой", head("большой — big, large")}, {"дом", head("дом (дома, pl. дома) — house, home")}});
  pack(d, "dict-de-ru", {{"Gebäude", head("das Gebäude (Gebäudes, pl. Gebäude) — здание")}, {"Haus", head("das Haus (Hauses, pl. Häuser) — дом")}},
       "Haus\t\t\t\t\t\t\t\t120\n");
  pack(d, "dict-en-ru", {{"house", head("house (pl. houses) — дом")}});
  return d;
}

CoreOptions options(const fs::path &d, std::vector<std::string> study) {
  CoreOptions o;
  o.data = d;
  if (!study.empty()) o.study["ru"] = study;
  return o;
}

std::vector<std::string> texts(const CardView &v) {
  std::vector<std::string> out;
  for (auto &r : v.rows)
    out.push_back(r.label.empty() ? r.text : r.label + (r.text.empty() ? "" : " " + r.text));
  return out;
}

}  // namespace

TEST_CASE("study: the stored language, one (InkPeek's Study)") {
  CHECK(study_parse({"de"}) == V{"de"});
  CHECK(study_parse({" en ", " de"}) == V{"en"});  // an older list of two keeps its first
  CHECK(study_parse({}).empty());
  CHECK(study_parse({"", "Bad", "1", "de"}) == V{"de"});
  std::vector<LangPair> pairs{{"ru", "de"}, {"ru", "en"}, {"de", "ru"}, {"en", "ru"}, {"ru", "en"}};
  CHECK(study_offered("ru", pairs) == V{"de", "en"});
  CHECK(study_offered("de", pairs) == V{"ru"});
  CHECK(study_offered("fr", pairs).empty());
  CHECK(study_offered("ru", {{"ru", "ru"}}).empty());
  CHECK(study_effective({"en"}, "ru", pairs) == V{"en"});
  CHECK(study_effective({"fr", "de"}, "ru", pairs) == V{"de"});  // no pack ru → fr: not in the card
  CHECK(study_effective({"ru"}, "de", {{"ru", "de"}}).empty());
  CHECK(study_direction("en", {"de"}) == "en → de");
  CHECK(study_words("Haus, Gebäude, Dynastie") == V{"Haus", "Gebäude", "Dynastie"});
  CHECK(study_words("machen, tun") == V{"machen", "tun"});
  CHECK(study_words("schön (bildlich), hübsch; Schön") == V{"schön", "hübsch"});
  CHECK(study_words("Haus [veraltet]; Heim.") == V{"Haus", "Heim"});
  CHECK(study_words("").empty());
  CHECK(study_words("a, b, c", 2).size() == 2);
}

TEST_CASE("study: AI — InkPeek's paragraph and versions; none keeps today's") {
  AiSettings s;
  // The versions with a language you learn: InkPeek's cases (ai_test, tests/inkpeek/tests/versions).
  CHECK(ai::version(ai::Ask::quick, s, {}) == 1031579369);
  CHECK(ai::version(ai::Ask::quick, s, {"German"}) != ai::version(ai::Ask::quick, s, {"English"}));
  CHECK(ai::study_prompt({}).empty());
  CHECK(ai::study_prompt({"German", "English"}).starts_with("\nThe text is in {lang}, the reader's own language, and the reader learns German and English."));
  // The request carries it, {lang} filled in.
  std::vector<std::string> prompts;
  ai::Transport t{nullptr, [&](const ai::CliCall &c) {
                    prompts.push_back(c.prompt);
                    return ai::Ran{0, R"({"result": "{\"translation\": \"x\"}"})", ""};
                  }};
  AiSettings cli;
  cli.provider = "claude-cli";
  std::map<std::string, int> recall;
  ai::Query q{ai::Ask::quick, "", "дом", 1, false, "Russian", "Russian", {"German"}};
  ai::ask(q, cli, recall, t);
  REQUIRE(prompts.size() == 1);
  CHECK(prompts[0].find("The text is in Russian, the reader's own language, and the reader learns German.") != std::string::npos);
  CHECK(prompts[0].find("Explanations stay in Russian.") != std::string::npos);
}

TEST_CASE("study: settings.json, per answer language; the Translation pane's row") {
  auto d = fs::temp_directory_path() / "lexipeek-study-settings";
  wipe(d);
  fs::create_directories(d);
  std::ofstream(d / "settings.json") << R"({"study": {"ru": ["de", "en", "fr"], "de": "x", "en": []}})";
  auto s = load_settings(d);
  CHECK(s.study.size() == 1);
  CHECK(s.study["ru"] == V{"de"});  // one: the first
  SettingsState have;
  have.installed_pairs = {{"ru", "en"}, {"ru", "de"}, {"de", "ru"}};
  s.answer = "ru";
  settings_choose(s, "study", 1, have);  // Off, English, German: by name
  CHECK(s.study["ru"] == V{"en"});
  s.answer = "de";
  settings_choose(s, "study", 1, have);
  CHECK(s.study["de"] == V{"ru"});
  CHECK(s.study["ru"] == V{"en"});  // each answer language its own choice
  settings_choose(s, "study", 0, have);
  CHECK(!s.study.contains("de"));
  settings_choose(s, "study", 1, have);
  CHECK(save_settings(d, s).empty());
  CHECK(load_settings(d).study == s.study);

  set_ui_language("en");
  Settings t;
  t.study["ru"] = {"en"};
  SettingsState st;
  st.pairs = {{"ru", "de"}, {"ru", "en"}, {"ru", "fr"}, {"de", "ru"}};
  st.installed_pairs = {{"ru", "en"}, {"ru", "fr"}};
  auto rows = settings_view(SettingsSection::languages, 1, t, st).rows;
  size_t at = 0;
  while (at < rows.size() && rows[at].key != "answer") at++;
  REQUIRE(at + 1 < rows.size());
  auto &r = rows[at + 1];  // right under the answer language
  CHECK(r.kind == SettingRow::Kind::choice);
  CHECK(r.title == "Text already in Russian");
  CHECK(r.choices == V{"Off", "English", "French"});  // installed packs only, by name
  CHECK(r.selected == 1);
  CHECK(r.about.find("No pack ru → de yet: get it in Packs") != std::string::npos);
  t.study["ru"] = {"fr"};
  CHECK(settings_view(SettingsSection::languages, 1, t, st).rows[at + 1].selected == 2);
  t.answer = "pl";
  CHECK(settings_view(SettingsSection::languages, 1, t, st).rows[at + 1].about == "No packs Polish → other languages yet.");
  wipe(d);
}

TEST_CASE("study: a word in the answer language — the language you learn, its words' own grammar and frequency") {
  set_ui_language("en");
  auto d = study_data("word");
  {
    // None chosen: the card as before.
    Core plain(options(d, {}));
    auto c = plain.lookup({"дом", Source::cli, "ru"});
    CHECK(c.study.empty());
    CHECK(c.lang == "ru");
  }
  Core core(options(d, {"de"}));
  auto c = core.lookup({"дом", Source::input});
  CHECK(c.lang == "ru");
  REQUIRE(c.study.size() == 1);
  CHECK(c.study[0].lang == "de");
  CHECK(c.entries[0].dictionary == "InkPeek · ru → de");
  REQUIRE(c.study[0].words.size() == 3);  // Haus, Gebäude, Heim: notes dropped, each once
  CHECK(c.study[0].words[0].header == "das Haus (Hauses, pl. Häuser)");
  CHECK(c.study[0].words[0].rank == 120);
  CHECK(c.study[0].words[2].word == "Heim");
  CHECK(c.study[0].words[2].header.empty());  // no pack knows it: the word alone
  CHECK(!c.study[0].has_method);  // no models here
  auto s = first_state(c);
  CHECK(s.tab == CardTab::dictionary);
  auto v = card_view(c, s);
  CHECK(v.badge.empty());
  CHECK(v.books.empty());
  CHECK(v.answers.empty());  // passive: no menu
  CHECK(v.subtitle.starts_with("ru → de"));
  CHECK(texts(v) == V{"InkPeek · ru → de дом (дома, pl. дома) — Haus, Gebäude (alt), Heim; Haus", "das Haus (Hauses, pl. Häuser) · among the 1,000 most frequent words",
                      "das Gebäude (Gebäudes, pl. Gebäude)", "Heim"});
  CHECK(v.rows[0].open == "entry");
  CHECK(v.rows[1].link == "Haus");
  CHECK(!v.rows[0].divider);  // a line under the pack's line and between its words (InkPeek's)
  CHECK(v.rows[1].divider);
  CHECK(v.rows[2].divider);
  CHECK(v.pager.empty());
  // Its line: the full entry alone; ← back.
  CardNav nav;
  nav.look("дом", Source::input);
  nav.arrived(c);
  nav.open_entry(0);
  CHECK(card_view(c, nav.state).rows.front().text.find("Haus, Gebäude") != std::string::npos);
  nav.back();
  CHECK(card_view(c, nav.state).rows.front().text.starts_with("дом (дома, pl. дома) — Haus"));
  // A word clicked opens its own card.
  auto step = nav.open("Haus");
  CHECK(step.text == "Haus");
  // Translate: the translation; a missing model says why.
  s.tab = CardTab::translate;
  v = card_view(c, s);
  REQUIRE(v.rows.size() == 1);
  CHECK(v.rows[0].style == CardRow::Style::note);
  // History: its line, under that language.
  auto h = core.store().history();
  REQUIRE(h.size() == 1);
  CHECK(h[0].lang == "de");
  CHECK(h[0].meaning == "дом (дома, pl. дома) — Haus, Gebäude (alt), Heim; Haus");
  // A German word is not in the answer language: as before.
  auto de = core.lookup({"Haus", Source::cli, "ru"});
  CHECK(de.study.empty());
  CHECK(de.lang == "de");
  wipe(d);
}

TEST_CASE("study: a phrase — word by word, the translation as it arrives") {
  set_ui_language("en");
  auto d = study_data("phrase");
  Core core(options(d, {"en"}));
  auto c = core.lookup({"большой дом", Source::cli, "ru"});
  REQUIRE(c.study.size() == 1);
  CHECK(c.study[0].lang == "en");
  REQUIRE(c.study[0].gloss.size() == 2);
  CHECK(c.study[0].gloss[1].meaning == "house, home");
  CHECK(c.gloss.empty());
  // The translation, pretended: on its way, then arrived.
  for (auto &l : c.study) l.has_method = true;
  CardNav nav;
  nav.look(c.text, Source::input);
  nav.arrived(c);
  CHECK(nav.state.tab == CardTab::translate);
  CHECK(nav.state.has_method);
  nav.translating(nav.state.method);
  CHECK(texts(card_view(c, nav.state)) == V{"Translating…"});
  nav.learned(0, nav.state.method, {"big house", "", "", ""});
  auto v = card_view(c, nav.state);
  CHECK(texts(v) == V{"big house"});
  CHECK(v.copy == "big house");
  // The method menu: the offline methods, not AI alone; its title the method in use.
  CHECK(v.menu);
  CHECK(v.methods.size() == 3);
  CHECK(v.menu_title == "Fast · Mozilla");
  nav.translated(nav.state.method, {"big house", "", "", ""});
  CHECK(shown_translation(nav.state) == "big house");
  nav.select(CardTab::dictionary);
  v = card_view(c, nav.state);
  CHECK(v.rows[0].style == CardRow::Style::gloss);
  CHECK(v.rows[1].label == "дом (дома, pl. дома)");
  CHECK(v.rows[1].text == "— house, home");
  wipe(d);
}

TEST_CASE("study: a language picked in the Translation pane is kept (the shell's state has no pairs)") {
  auto d = fs::temp_directory_path() / "lexipeek-study-choose";
  wipe(d);
  fs::create_directories(d);
  std::ofstream(d / "settings.json") << R"({"answer": "ru"})";
  std::ofstream(d / "installed.json") << R"({"dict-ru-en": "a", "dict-ru-de": "b"})";
  CHECK(app::choose(d.string(), "study", 2, {}).empty());  // Off, English, German
  CHECK(load_settings(d).study["ru"] == V{"de"});
  CHECK(app::choose(d.string(), "study", 0, {}).empty());
  CHECK(!load_settings(d).study.contains("ru"));
  wipe(d);
}

TEST_CASE("study: ☆ keeps the note's own reading in its folder; un-saving and removing the note delete only its copies") {
  set_ui_language("en");
  auto d = study_data("readings");
  Settings st;
  st.speech.on = true;
  st.study["ru"] = {"de"};
  save_settings(d, st);
  auto v = speech::voice_for({}, st.speech, "de");
  if (v.id.empty()) return wipe(d);
  speech::Read house{"das Haus", "de"}, building{"das Gebäude", "de"};
  auto made = [&](const speech::Read &r) {
    fs::create_directories(d / "readings");
    std::ofstream(speech::reading_file(d, v, r), std::ios::binary) << speech::wav_bytes(std::vector<float>(100, 0), 24000);
  };
  auto e = app::Engine::make(d.string(), app::device(16384, {}, "arm64"));
  auto ru = app::CardSession::make(e), de = app::CardSession::make(e);
  auto settle = [](app::CardSession &s) {
    until([&] { return s.poll(), !s.busy(); });
    s.poll();
  };
  auto play = [&](app::CardSession &s, int row) {
    s.read(row);
    settle(s);
    auto wav = s.take_play();
    s.played();
    return wav;
  };
  made(house), made(building);
  ru.look("дом", Source::input);
  settle(ru);
  REQUIRE(ru.has_card());
  ru.select(CardTab::dictionary);
  CHECK(ru.view().read == house);
  ru.toggle_saved();
  CHECK(ru.nav().state.saved);
  CHECK(fs::exists(speech::saved_reading(d, "дом", v, house)));
  // A translated word's own ▶ while saved: played, not kept with the note.
  int row = -1;
  for (size_t i = 0; i < ru.view().rows.size(); i++)
    if (ru.view().rows[i].read == building) row = static_cast<int>(i);
  REQUIRE(row >= 0);
  CHECK(play(ru, row) == speech::reading_file(d, v, building).string());
  CHECK(speech::find_saved(d, v, building).empty());
  // de "Haus" reads the same "das Haus": its own copy, from the other note's once the readings let it go.
  speech::prune_readings(d, 0);
  de.look("Haus", Source::input);
  settle(de);
  REQUIRE(de.has_card());
  CHECK(de.view().read == house);
  de.toggle_saved();
  CHECK(fs::exists(speech::saved_reading(d, "Haus", v, house)));
  ru.toggle_saved();
  CHECK(!ru.nav().state.saved);
  CHECK(!fs::exists(speech::saved_dir(d, "дом")));
  CHECK(play(de, -1) == speech::saved_reading(d, "Haus", v, house).string());
  e.remove_note(NotesTab::words, "Haus");
  CHECK(!fs::exists(speech::saved_dir(d, "Haus")));
  // Saved before its reading is made: kept once it is.
  ru.toggle_saved();
  CHECK(!fs::exists(speech::saved_dir(d, "дом")));
  made(house);
  CHECK(!play(ru, -1).empty());
  CHECK(fs::exists(speech::saved_reading(d, "дом", v, house)));
  wipe(d);
}
