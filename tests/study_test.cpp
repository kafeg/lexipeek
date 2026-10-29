// Languages you learn (InkPeek's StudyTest and milestone 21): the list, the settings row, the card, the AI versions.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include "ai/ai.hpp"
#include "lexipeek/app.hpp"
#include "lexipeek/core.hpp"
#include "lexipeek/l10n.hpp"
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
  for (auto &r : v.rows) out.push_back(r.label.empty() ? r.text : r.label + (r.text.empty() ? "" : " " + r.text));
  return out;
}

}  // namespace

TEST_CASE("study: the stored list, two at most, in order (InkPeek's Study)") {
  CHECK(study_parse({"de", "en"}) == V{"de", "en"});
  CHECK(study_parse({" en ", " de"}) == V{"en", "de"});
  CHECK(study_parse({}).empty());
  CHECK(study_parse({"de", "de", "en", "fr"}) == V{"de", "en"});  // two at most, each once
  CHECK(study_parse({"de", "", "Bad", "1"}) == V{"de"});
  auto l = study_toggle({}, "en", true);
  l = study_toggle(l, "de", true);
  CHECK(l == V{"en", "de"});  // the order of ticking
  CHECK(study_toggle(l, "fr", true) == V{"en", "de"});
  CHECK(study_toggle(l, "en", false) == V{"de"});
  CHECK(study_toggle(study_toggle(l, "en", false), "fr", true) == V{"de", "fr"});
  CHECK(study_toggle(l, "de", true) == V{"en", "de"});
  std::vector<LangPair> pairs{{"ru", "de"}, {"ru", "en"}, {"de", "ru"}, {"en", "ru"}, {"ru", "en"}};
  CHECK(study_offered("ru", pairs) == V{"de", "en"});
  CHECK(study_offered("de", pairs) == V{"ru"});
  CHECK(study_offered("fr", pairs).empty());
  CHECK(study_offered("ru", {{"ru", "ru"}}).empty());
  CHECK(study_effective({"en", "de"}, "ru", pairs) == V{"en", "de"});
  CHECK(study_effective({"fr", "de"}, "ru", pairs) == V{"de"});  // no pack ru → fr: not in the card
  CHECK(study_effective({"ru"}, "de", {{"ru", "de"}}).empty());
  CHECK(study_direction("ru", {"de", "en"}) == "ru → de, en");
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
  // InkPeek: Ask.version(study) = (prompt() + Ai.study(study)).hashCode(), worked out with Java's String.hashCode from
  // InkPeek's Ai.STUDY and its versions without it (ai_test).
  CHECK(ai::version(ai::Ask::quick, s, {}) == 1031579369);
  CHECK(ai::version(ai::Ask::quick, s, {"German", "English"}) == 1835370981);
  CHECK(ai::version(ai::Ask::quick, s, {"English", "German"}) == -1007015789);
  CHECK(ai::version(ai::Ask::analysis, s, {"German"}) == 158405933);
  CHECK(ai::version(ai::Ask::context, s, {"German"}) == 978701152);
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
  CHECK(s.study["ru"] == V{"de", "en"});
  settings_toggle(s, "study.de", false);
  CHECK(s.study["ru"] == V{"en"});
  s.answer = "de";
  settings_toggle(s, "study.ru", true);
  CHECK(s.study["de"] == V{"ru"});
  CHECK(s.study["ru"] == V{"en"});  // each answer language its own list
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
  CHECK(r.kind == SettingRow::Kind::checks);
  CHECK(r.title == "Text already in Russian");
  REQUIRE(r.checks.size() == 3);
  CHECK(r.checks[0].title == "English");  // by name
  CHECK(r.checks[0].on);
  CHECK(r.checks[1].title == "French");
  CHECK(r.checks[1].enabled);
  CHECK(r.checks[2].title == "German");
  CHECK(!r.checks[2].enabled);
  CHECK(r.checks[2].note == "No pack ru → de yet: get it in Packs");
  t.study["ru"] = {"en", "fr"};
  st.installed_pairs.push_back({"ru", "de"});
  r = settings_view(SettingsSection::languages, 1, t, st).rows[at + 1];
  CHECK(!r.checks[2].enabled);
  CHECK(r.checks[2].note == "Two at most: untick one first");
  t.answer = "pl";
  CHECK(settings_view(SettingsSection::languages, 1, t, st).rows[at + 1].about == "No packs Polish → other languages yet.");
  wipe(d);
}

TEST_CASE("study: a word in the answer language — a block per language, the words' own grammar and frequency") {
  set_ui_language("en");
  auto d = study_data("word");
  {
    // None ticked: the card as before.
    Core plain(options(d, {}));
    auto c = plain.lookup({"дом", Source::cli, "ru"});
    CHECK(c.study.empty());
    CHECK(c.lang == "ru");
  }
  Core core(options(d, {"de", "en", }));
  auto c = core.lookup({"дом", Source::input});
  CHECK(c.lang == "ru");
  REQUIRE(c.study.size() == 2);
  CHECK(c.study[0].lang == "de");
  CHECK(c.study[1].lang == "en");
  REQUIRE(c.entries.size() == 2);
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
  CHECK(v.subtitle.starts_with("ru → de, en"));
  CHECK(texts(v) == V{"German", "дом (дома, pl. дома) — Haus, Gebäude (alt), Heim; Haus", "das Haus (Hauses, pl. Häuser) · among the 1,000 most frequent words",
                      "das Gebäude (Gebäudes, pl. Gebäude)", "Heim", "English", "дом (дома, pl. дома) — house, home",
                      "house (pl. houses)", "home"});
  CHECK(v.rows[1].open == "entry");
  CHECK(v.rows[2].link == "Haus");
  CHECK(v.rows[6].entry == 1);
  // Its line: the full entry alone; ← back to the blocks.
  CardNav nav;
  nav.look("дом", Source::input);
  nav.arrived(c);
  nav.open_entry(1);
  CHECK(card_view(c, nav.state).rows.front().text.find("house, home") != std::string::npos);
  nav.back();
  CHECK(card_view(c, nav.state).rows.front().text == "German");
  // A word clicked opens its own card.
  auto step = nav.open("Haus");
  CHECK(step.text == "Haus");
  // Translate: each language under its name; a missing model says why.
  s.tab = CardTab::translate;
  v = card_view(c, s);
  REQUIRE(v.rows.size() == 4);
  CHECK(v.rows[0].text == "German");
  CHECK(v.rows[1].style == CardRow::Style::note);
  // History: the first language's line, under that language.
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

TEST_CASE("study: a phrase — word by word per language, the translations as they arrive") {
  set_ui_language("en");
  auto d = study_data("phrase");
  Core core(options(d, {"en", "de"}));
  auto c = core.lookup({"большой дом", Source::cli, "ru"});
  REQUIRE(c.study.size() == 2);
  CHECK(c.study[0].lang == "en");
  REQUIRE(c.study[0].gloss.size() == 2);
  CHECK(c.study[0].gloss[1].meaning == "house, home");
  CHECK(c.gloss.empty());
  // The translations, pretended: one arrived, the other on its way.
  for (auto &l : c.study) l.has_method = true;
  CardNav nav;
  nav.look(c.text, Source::input);
  nav.arrived(c);
  CHECK(nav.state.tab == CardTab::translate);
  CHECK(nav.state.has_method);
  nav.translating(nav.state.method);
  nav.learned(0, nav.state.method, {"big house", "", "", ""});
  auto v = card_view(c, nav.state);
  CHECK(texts(v) == V{"English", "big house", "German", "Translating…"});
  CHECK(v.copy == "big house");
  nav.learned(1, nav.state.method, {"", "", "", "failed"});
  nav.translated(nav.state.method, {"big house", "", "", ""});
  CHECK(texts(card_view(c, nav.state)) == V{"English", "big house", "German", "failed"});
  CHECK(shown_translation(nav.state) == "big house");
  nav.select(CardTab::dictionary);
  v = card_view(c, nav.state);
  CHECK(v.rows[0].text == "English");
  CHECK(v.rows[1].style == CardRow::Style::gloss);
  CHECK(v.rows[2].label == "дом (дома, pl. дома)");
  CHECK(v.rows[2].text == "— house, home");
  wipe(d);
}
