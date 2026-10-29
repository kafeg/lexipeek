// The card's view model, the facade's pack helpers, answers' versions, configure.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <fstream>
#include <thread>
#include "lexipeek/app.hpp"
#include "lexipeek/core.hpp"
#include "lexipeek/views.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;
using Style = CardRow::Style;

namespace {

Card word() {
  Card c;
  c.kind = Card::Kind::word;
  c.text = "Häusern";
  c.lang = "de", c.answer = "ru", c.answer_name = "Russian";
  c.header = "das Haus (Hauses, pl. Häuser)";
  c.base = "Haus";
  c.entries = {{"InkPeek · de → ru", "das Haus (Hauses, pl. Häuser) — дом", "<b>Haus</b> дом", "de-ru", "Haus"}};
  c.save_text = "Haus";
  c.looks = 3;
  c.methods = {{Method::fast, "Fast · Mozilla", true, ""},
               {Method::direct, "Direct · OPUS-MT", false, "Not installed: opus-x"},
               {Method::llm, "AI model · Hy-MT2", true, ""}};
  c.method = Method::fast;
  return c;
}

Card phrase() {
  auto c = word();
  c.kind = Card::Kind::phrase;
  c.text = "Ich mache die Tür zu";
  c.header = c.base = "";
  c.entries = {};
  c.gloss = {{"Ich", "ich", "я"}, {"Tür", "Tür", "дверь", "die Tür (Tür, мн. Türen)"}, {"die", "", ""}};
  c.separable = {{"mache … zu", "zumachen", "закрывать", "zumachen (macht zu, machte zu, hat zugemacht)"}};
  return c;
}

}  // namespace

TEST_CASE("views: more dictionaries name themselves; another headword keeps its line and the form") {
  auto c = word();
  c.entries.push_back({"Mueller", "Hausen — жить", "", "mueller", "hausen"});
  auto v = card_view(c, first_state(c));
  REQUIRE(v.rows.size() == 2);
  CHECK(v.rows[0].label == "InkPeek · de → ru");
  CHECK(v.rows[0].text == "дом");
  CHECK(v.rows[1].label == "Mueller");
  CHECK(v.rows[1].text == "Häusern: Hausen — жить");
}

TEST_CASE("views: the badge's menu lists the packs' languages when there is a choice") {
  auto c = word();
  CHECK(card_view(c, first_state(c)).books.empty());
  c.books = {"de", "en"}, c.answers = {"ru"};
  auto v = card_view(c, first_state(c));
  REQUIRE(v.books.size() == 2);
  CHECK(v.books[0].code == "de");
  CHECK(v.books[0].on);
  CHECK(!v.books[1].on);
  REQUIRE(v.answers.size() == 1);
  CHECK(v.answers[0].on);
}

TEST_CASE("views: a word opens on Dictionary with its header, the form, frequency and looks") {
  auto c = word();
  auto s = first_state(c);
  CHECK(s.tab == CardTab::dictionary);
  REQUIRE(s.has_method);
  CHECK(s.method == Method::fast);
  auto v = card_view(c, s);
  CHECK(v.style == HeaderStyle::word);
  CHECK(v.title == "das Haus (Hauses, pl. Häuser)");
  CHECK(v.subtitle == "form: Häusern · looked up 3 times");
  CHECK(v.tooltip == "Häusern");
  CHECK(v.badge == "de → ru");
  CHECK(v.tabs);
  REQUIRE(v.rows.size() == 1);
  CHECK(v.rows[0].style == Style::entry);
  CHECK(v.rows[0].label.empty());  // one dictionary: no name
  CHECK(v.rows[0].text == "дом");  // the header and the form are above it
  CHECK(v.rows[0].copy == "дом");
  CHECK(v.rows[0].html.empty());
  CHECK(!v.menu);
  CHECK(v.back.empty());
  s.full_entry = true;
  v = card_view(c, s);
  CHECK(v.rows[0].html == "<b>Haus</b> дом");
  CHECK(v.rows[0].text.empty());
  c.rank = 255;
  CHECK(card_view(c, s).subtitle == "form: Häusern · among the 1,000 most frequent words · looked up 3 times");
  c.looks = 1, c.rank = 4800;
  CHECK(card_view(c, s).subtitle == "form: Häusern · among the 5,000 most frequent words");
  c.header = "";
  c.base = "";
  c.rank = 20001;
  v = card_view(c, s);
  CHECK(v.title == "Häusern");
  CHECK(v.subtitle.empty());
  CHECK(frequency(0).empty());
  CHECK(frequency(20000) == "among the 20,000 most frequent words");
}

TEST_CASE("views: the full entry — senses, examples, other headwords, sections; a section's page; back") {
  auto c = word();
  c.text = "Haus";
  auto &e = c.entries[0];
  e.word = "Haus";
  e.articles = {{"Haus", "das Haus (Hauses) — дом", {{"дом, здание", {"◆ Ich gehe nach Hause — Я иду домой."}}, {"династия", {}}}},
                {"haus", "haus — вариант Haus", {{"вариант Haus", {}}}}};
  e.related.rel = {"Krankenhaus", "zuhause", "a", "b", "c", "d", "e", "f", "g", "h", "i"};
  e.related.syn = {"Gebäude"};
  e.related.ant = {"Hütte"};
  e.related.ipa = {"[haʊ̯s]"};
  e.related.forms = {{"Hauses", "genitive"}};
  e.related.origin = std::string(130, 'x');
  c.prefs.sections.push_back("ety");  // off by default, as InkPeek's
  auto s = first_state(c);
  s.full_entry = true;
  auto v = card_view(c, s);
  REQUIRE(v.rows.size() == 10);
  CHECK(v.rows[0].style == Style::entry);
  CHECK(v.rows[0].text == "дом");  // its header is the card's
  CHECK(v.rows[1].style == Style::sense);
  CHECK(v.rows[1].label == "1.");
  CHECK(v.rows[1].text == "дом, здание");
  CHECK(v.rows[2].style == Style::example);
  CHECK(v.rows[2].text == "◆ Ich gehe nach Hause — Я иду домой.");
  CHECK(v.rows[3].label == "2.");
  CHECK(v.rows[4].style == Style::heading);
  CHECK(v.rows[4].text == "haus — вариант Haus");
  CHECK(v.rows[6].style == Style::section);
  CHECK(v.rows[6].label == "Krankenhaus, zuhause, a, b, c, d, e, f, g, h …");
  CHECK(v.rows[6].text == "Related words · 11 ›");
  CHECK(v.rows[6].open == "rel");
  CHECK(v.rows[7].label == "Gebäude ≠ Hütte");
  CHECK(v.rows[7].text == "Synonyms · 1 · antonyms · 1 ›");
  CHECK(v.rows[8].text == "Forms and pronunciation · [haʊ̯s] ›");
  CHECK(v.rows[9].label == std::string(120, 'x') + " …");
  CHECK(v.rows[9].open == "ety");
  // A section open: its words with their meanings, a pager, "←" to the entry.
  s.section = {"rel", "Haus", 0, 1, 2, 23, 20, {{"Hausarzt", "Hausarzt", "домашний врач"}, {"Haustür", "Haustür", ""}, {"Hausboot", "Hausboot", ""}}, ""};
  v = card_view(c, s);
  REQUIRE(v.rows.size() == 4);
  CHECK(v.rows[0].style == Style::heading);
  CHECK(v.rows[0].text == "Haus · Related words");
  CHECK(v.rows[1].style == Style::gloss);
  CHECK(v.rows[1].label == "Hausarzt");
  CHECK(v.rows[1].text == "— домашний врач");
  CHECK(v.rows[1].link == "Hausarzt");
  CHECK(v.pager == "21–23 of 23");
  CHECK(v.prev);
  CHECK(!v.next);
  CHECK(v.back == "Entry");
  s.section = {};
  s.before = "Ich mache die Tür zu";
  v = card_view(c, s);
  CHECK(v.back == "Ich mache die Tür zu");
  CHECK(v.pager.empty());
}

TEST_CASE("views: a word without entries opens on Translate; Dictionary says so") {
  auto c = word();
  c.entries = {};
  auto s = first_state(c);
  CHECK(s.tab == CardTab::translate);
  s.tab = CardTab::dictionary;
  auto v = card_view(c, s);
  REQUIRE(v.rows.size() == 1);
  CHECK(v.rows[0].style == Style::note);
  CHECK(v.rows[0].text == "No entry in the dictionaries");
}

TEST_CASE("views: Translate — progress, the translation, an error, the methods' menu") {
  auto c = word();
  auto s = first_state(c);
  s.tab = CardTab::translate;
  s.translating = true;
  auto v = card_view(c, s);
  REQUIRE(v.rows.size() == 1);
  CHECK(v.rows[0].style == Style::progress);
  CHECK(v.rows[0].text == "Translating…");
  REQUIRE(v.menu);
  CHECK(v.menu_title == "Fast · Mozilla");
  REQUIRE(v.methods.size() == 3);
  CHECK(v.methods[1].title == "Direct · OPUS-MT — Not installed: opus-x");
  CHECK(!v.methods[1].enabled);
  CHECK(v.methods[2].enabled);
  // A finished one stays while another method runs.
  s.translated = true;
  s.translation.text = "домам";
  v = card_view(c, s);
  CHECK(v.rows[0].style == Style::text);
  CHECK(v.rows[0].text == "домам");
  CHECK(v.rows[0].copy == "домам");
  s.translation.ms = 1500;
  CHECK(card_view(c, s).menu_help == "Translated in 1.5 s");
  s.translation.kept = true;
  CHECK(card_view(c, s).menu_help.starts_with("Already translated"));
  s.translation.error = "Cancelled";
  CHECK(card_view(c, s).rows[0].style == Style::error);
  CHECK(card_view(c, s).rows[0].text == "Cancelled");
  s = {};
  CHECK(card_view(c, s).menu_title == "No method available");
  c.methods[0].available = false;
  c.methods[0].reason = "No Mozilla model de-en (model-de-en)";
  c.method.reset();
  s = first_state(c);
  CHECK(!s.has_method);
  s.tab = CardTab::translate;
  v = card_view(c, s);
  CHECK(v.rows[0].text == "No Mozilla model de-en (model-de-en)");
}

TEST_CASE("views: a phrase opens on Translate, word by word in Dictionary") {
  auto c = phrase();
  auto s = first_state(c);
  CHECK(s.tab == CardTab::translate);
  auto v = card_view(c, s);
  CHECK(v.style == HeaderStyle::phrase);
  CHECK(v.title == "Ich mache die Tür zu");
  CHECK(v.subtitle.empty());
  CHECK(v.tabs);
  s.tab = CardTab::dictionary;
  v = card_view(c, s);
  REQUIRE(v.rows.size() == 4);
  CHECK(v.rows[0].style == Style::gloss);
  CHECK(v.rows[0].label == "mache … zu: zumachen (macht zu, machte zu, hat zugemacht)");
  CHECK(v.rows[0].text == "— закрывать");
  CHECK(v.rows[0].link == "zumachen");
  CHECK(v.rows[1].label == "ich");
  CHECK(v.rows[1].link == "ich");
  CHECK(v.rows[2].label == "die Tür (Tür, мн. Türen)");
  CHECK(v.rows[3].label == "die");
  CHECK(v.rows[3].text.empty());
  CHECK(v.rows[3].link == "die");
  CHECK(!v.menu);
  c.gloss[0].word = "Ich";
  c.gloss[0].base = "ich";
  c.gloss[1] = {"Türen", "Tür", "дверь", "die Tür (Tür, мн. Türen)"};
  c.more = 12;
  v = card_view(c, s);
  CHECK(v.rows[2].label == "Türen: die Tür (Tür, мн. Türen)");
  CHECK(v.rows[2].link == "Tür");
  REQUIRE(v.rows.size() == 5);
  CHECK(v.rows[4].style == Style::note);
  CHECK(v.rows[4].text == "… and 12 more words");
}

TEST_CASE("views: a passage without gloss has only Translate, a wider card") {
  auto c = phrase();
  c.kind = Card::Kind::passage;
  c.gloss = {};
  c.separable = {};
  auto s = first_state(c);
  s.tab = CardTab::dictionary;  // ignored without tabs
  auto v = card_view(c, s);
  CHECK(v.style == HeaderStyle::passage);
  CHECK(v.width == 460);
  CHECK(!v.tabs);
  CHECK(v.tab == CardTab::translate);
  CHECK(v.menu);
}

TEST_CASE("views: same language, no pack, unknown language, nothing to translate with") {
  auto c = phrase();
  c.lang = "ru";
  c.methods = {};
  auto v = card_view(c, first_state(c));
  CHECK(v.badge == "ru");
  CHECK(v.rows[0].text == "Already in the answer language (Russian).");
  CHECK(!v.menu);
  c.lang = "";
  c.other = "fr";
  c.other_name = "French";
  v = card_view(c, first_state(c));
  CHECK(v.badge == "fr → ru");
  CHECK(v.rows[0].text == "No language pack from French into Russian yet.");
  c.other_name = "";
  CHECK(card_view(c, first_state(c)).rows[0].text == "Could not tell the language of this text.");
  c.lang = "de";
  CHECK(card_view(c, first_state(c)).rows[0].text == "Nothing can translate this text here.");
  c.error = "broken index";
  CHECK(card_view(c, first_state(c)).rows[0].text == "broken index");
}

TEST_CASE("views: what a pair needs, what removing it frees, answer languages") {
  auto m = parse_manifest(R"({"format": 1, "components": [
    {"id": "dict-de-ru", "kind": "dict", "from": "de", "to": "ru", "file": {"name": "a", "size": 1, "sha256": "d1"}},
    {"id": "dict-fr-ru", "kind": "dict", "from": "fr", "to": "ru", "file": {"name": "b", "size": 1, "sha256": "d2"}},
    {"id": "dict-de-en", "kind": "dict", "from": "de", "to": "en", "file": {"name": "c", "size": 1, "sha256": "d3"}},
    {"id": "model-de-en", "kind": "model", "from": "de", "to": "en", "file": {"name": "d", "size": 1, "sha256": "m1"}},
    {"id": "model-fr-en", "kind": "model", "from": "fr", "to": "en", "file": {"name": "e", "size": 1, "sha256": "m2"}},
    {"id": "model-en-ru", "kind": "model", "from": "en", "to": "ru", "file": {"name": "f", "size": 1, "sha256": "m3"}},
    {"id": "langid", "kind": "langid", "file": {"name": "g", "size": 1, "sha256": "l"}}]})");
  std::map<std::string, std::string> have{{"dict-de-ru", "d1"}, {"model-de-en", "m1"}, {"model-en-ru", "m3"}, {"langid", "l"},
                                          {"dict-fr-ru", "d2"}, {"model-fr-en", "m2"}};
  CHECK(missing_ids(m, have, {"de", "en"}) == std::vector<std::string>{"dict-de-en"});
  CHECK(missing_ids(m, have, {"de", "ru"}).empty());
  // fr-ru still needs en-ru and langid.
  CHECK(freed_ids(m, have, {"de", "ru"}) == std::vector<std::string>{"dict-de-ru", "model-de-en"});
  CHECK(answer_languages(m) == std::vector<std::string>{"en", "ru"});
}

TEST_CASE("views: the methods for the installed pairs: models, downloaded, uncovered, this machine") {
  auto m = parse_manifest(R"({"format": 1, "components": [
    {"id": "dict-de-ru", "kind": "dict", "from": "de", "to": "ru", "file": {"name": "a", "size": 1, "sha256": "d1"}},
    {"id": "dict-fr-ru", "kind": "dict", "from": "fr", "to": "ru", "file": {"name": "b", "size": 1, "sha256": "d2"}},
    {"id": "model-de-en", "kind": "model", "from": "de", "to": "en", "file": {"name": "d", "size": 10, "sha256": "m1"}},
    {"id": "model-fr-en", "kind": "model", "from": "fr", "to": "en", "file": {"name": "e", "size": 10, "sha256": "m2"}},
    {"id": "model-en-ru", "kind": "model", "from": "en", "to": "ru", "file": {"name": "f", "size": 10, "sha256": "m3"}},
    {"id": "langid", "kind": "langid", "file": {"name": "g", "size": 1, "sha256": "l"}},
    {"id": "opus-a", "kind": "opus", "covers": ["de-ru"], "file": {"name": "h", "size": 100, "sha256": "o"}},
    {"id": "llm-b", "kind": "llm", "covers": ["de-ru", "fr-ru"], "ram_mb": 6000, "cpu": ["asimddp"], "file": {"name": "i", "size": 1000, "sha256": "x"}}]})");
  std::map<std::string, std::string> have{{"dict-de-ru", "d1"}, {"model-de-en", "m1"}, {"model-en-ru", "m3"}, {"langid", "l"},
                                          {"dict-fr-ru", "d2"}, {"model-fr-en", "m2"}, {"opus-a", "o"}};
  auto infos = method_infos(m, have, {16384, {"asimddp"}, "arm64"});
  REQUIRE(infos.size() == 3);
  CHECK(infos[0].models == 3);
  CHECK(infos[0].missing.empty());
  CHECK(infos[1].models == 1);
  CHECK(infos[1].missing.empty());
  CHECK(infos[1].bare == std::vector<std::string>{"fr→ru"});
  CHECK(infos[2].cannot.empty());
  CHECK(infos[2].missing == std::vector<std::string>{"llm-b"});
  CHECK(infos[2].left == 1000);
  CHECK(!method_infos(m, have, {4096, {}, "arm64"})[2].cannot.empty());
  // The first run's download: a method this device cannot run brings Fast's parts instead.
  std::map<std::string, std::string> none;
  auto llm = missing_ids(m, none, {"de", "ru"}, Method::llm, Device{16384, {"asimddp"}, "arm64"});
  CHECK(std::find(llm.begin(), llm.end(), "llm-b") != llm.end());
  auto weak = missing_ids(m, none, {"de", "ru"}, Method::llm, Device{4096, {}, "arm64"});
  CHECK(std::find(weak.begin(), weak.end(), "llm-b") == weak.end());
  CHECK(std::find(weak.begin(), weak.end(), "model-en-ru") != weak.end());
}

TEST_CASE("answers: versioned by the installed models' sha256, as InkPeek") {
  auto m = parse_manifest(R"({"format": 1, "components": [
    {"id": "opus-a", "kind": "opus", "covers": ["de-ru"], "file": {"name": "a", "size": 1, "sha256": "new"}},
    {"id": "llm-b", "kind": "llm", "covers": ["de-ru"], "file": {"name": "b", "size": 1, "sha256": "x"}}]})");
  std::string a = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
  std::string b = "60303ae22b998861bce3b28f33eec1be758a213c86c93c076dbe9f558c11c752";
  std::map<std::string, std::string> have{{"model-de-en", a}, {"model-en-ru", b}, {"opus-a", a}, {"llm-b", "hello"}};
  // Kotlin: steps.joinToString { installed["model-$a-$b"] }.hashCode(), c.sha256.hashCode().
  CHECK(answer_version(m, have, "de", "ru", Method::fast) == 189589710);
  CHECK(answer_version(m, have, "de", "en", Method::fast) == -651348412);
  CHECK(answer_version(m, have, "de", "ru", Method::direct) == -651348412);
  CHECK(answer_version(m, have, "de", "ru", Method::llm) == 99162322);
  CHECK(answer_version(m, {}, "fr", "ru", Method::direct) == 0);
}

TEST_CASE("core: configure changes the answer language and the method without a new core") {
  auto d = fs::temp_directory_path() / "lexipeek-configure";
  wipe(d);
  CoreOptions o;
  o.data = d;
  Core core(o);
  CHECK(core.lookup(Request{.text = "Haus", .source = Source::cli}).answer == "ru");
  Settings s;
  s.answer = "en";
  s.method = Method::direct;
  core.configure(s);
  CHECK(core.lookup(Request{.text = "Haus", .source = Source::cli}).answer == "en");
  CHECK(core.lookup(Request{.text = "Haus", .source = Source::cli, .answer = "de"}).answer == "de");
  wipe(d);
}

TEST_CASE("app: settings by data dir, method ids") {
  auto d = (fs::temp_directory_path() / "lexipeek-app").string();
  wipe(d);
  auto s = app::settings(d);
  CHECK(app::method_id(s) == "direct");  // the default: OPUS-MT
  app::set_method(s, "llm");
  CHECK(app::method_id(s) == "llm");
  CHECK(app::save(d, s).empty());
  CHECK(app::settings(d).method == Method::llm);
  app::set_method(s, "");
  CHECK(!s.method);
  CHECK(app::method_id(Method::direct) == "direct");
  CHECK(app::language("de") == "German");
  CHECK(app::answers(d).empty());
  wipe(d);
}

TEST_CASE("views: the menu's line about the held model") {
  CHECK(!memory_view({}).held);
  CHECK(memory_view({}).line == "No model in memory");
  auto v = memory_view({"direct:opus-de-ru", "OPUS-MT de → ru", 410'000'000, 150});
  CHECK(v.held);
  CHECK(v.line == "In memory: OPUS-MT de → ru · 410 MB");
  CHECK(v.detail == "unloads in 3 min");
  CHECK(v.unload == "Unload Now");
  CHECK(memory_view({"k", "t", 0, 10}).detail == "unloads in less than a minute");
  CHECK(memory_view({"k", "t", 0, -1}).detail == "stays until LexiPeek quits");
}

TEST_CASE("views: offline translation off — Translate says so, or is the AI translation") {
  auto c = phrase();
  c.methods.clear();
  c.method.reset();
  c.mt_off = true;
  auto s = first_state(c);
  CHECK(!s.by_ai);
  CHECK(s.tab == CardTab::dictionary);  // word by word
  s.tab = CardTab::translate;
  CHECK(card_view(c, s).rows[0].text.starts_with("Offline translation is off"));
  c.ai.translate = true;
  CHECK(first_state(c).by_ai);
}

TEST_CASE("nav: a clicked word opens here, ← comes back as it was; a section first") {
  CardNav n;
  auto s = n.look("Ich mache die Tür zu", Source::hotkey);
  CHECK(s.kind == CardStep::Kind::lookup);
  CHECK(s.text == "Ich mache die Tür zu");
  n.arrived(phrase());
  CHECK(n.state.before.empty());
  n.translating(Method::fast);
  n.state.tab = CardTab::dictionary;
  s = n.open("Tür");
  CHECK(s.kind == CardStep::Kind::lookup);
  CHECK(s.text == "Tür");
  auto w = word();
  w.text = "Tür";
  n.arrived(w);
  CHECK(n.state.before == "Ich mache die Tür zu");
  // A section, then ← to the entry, then ← to the phrase as it was (its translation asked again).
  CHECK(n.open_section(0, "rel").kind == CardStep::Kind::section);
  n.section_arrived({"rel", "Tür", 0, 0, 2, 30, 0, {}, ""});
  CHECK(n.turn(1).page == 1);
  CHECK(n.turn(-1).kind == CardStep::Kind::none);
  CHECK(n.back().kind == CardStep::Kind::none);
  CHECK(n.state.section.key.empty());
  s = n.back();
  CHECK(s.kind == CardStep::Kind::restore);
  CHECK(s.translate);
  CHECK(n.card().text == "Ich mache die Tür zu");
  CHECK(n.state.tab == CardTab::dictionary);
  CHECK(n.back().kind == CardStep::Kind::none);
  // A new look-up forgets the trail.
  n.look("Haus", Source::input);
  n.arrived(word());
  CHECK(n.state.before.empty());
}

TEST_CASE("nav: a stale translation is dropped; the badge's re-lookup keeps the card's language") {
  CardNav n;
  n.look("Häusern", Source::input);
  n.arrived(word());
  n.translating(Method::fast);
  CHECK(n.busy());
  n.pick_method(Method::direct);
  n.translated(Method::fast, {"дома", "", "", ""});
  CHECK(!n.state.translated);
  n.translated(Method::direct, {"домам", "", "", ""});
  CHECK(n.state.translation.text == "домам");
  CHECK(!n.busy());
  auto s = n.relookup("", "en");
  CHECK(s.lang == "de");
  CHECK(s.answer == "en");
  CHECK(s.text == "Häusern");
  n.arrived(word());
  n.next_tab(1);
  CHECK(n.state.tab == CardTab::translate);
  n.next_tab(-1);
  CHECK(n.state.tab == CardTab::dictionary);
}

TEST_CASE("core: History off writes nothing of what is opened") {
  auto d = fs::temp_directory_path() / "lexipeek-history-off";
  wipe(d);
  CoreOptions o;
  o.data = d;
  Core core(o);
  Settings s;
  s.card.history = false;
  core.configure(s);
  core.lookup(Request{.text = "Haus", .source = Source::input});
  CHECK(core.store().history().empty());
  s.card.history = true;
  core.configure(s);
  core.lookup(Request{.text = "Haus", .source = Source::input});
  CHECK(core.store().history().size() == 1);
  wipe(d);
}

TEST_CASE("views: a dictionary's line opens only its full entry, ← comes back to all") {
  auto c = word();
  c.entries.push_back({"Mueller", "Haus — дом", "<b>Haus</b> дом, здание", "mueller", "Haus"});
  CardNav n;
  n.look("Häusern", Source::input);
  n.arrived(c);
  auto v = card_view(n.card(), n.state);
  REQUIRE(v.rows.size() == 2);
  CHECK(v.rows[1].open == "entry");
  CHECK(v.rows[1].entry == 1);
  n.open_entry(1);
  v = card_view(n.card(), n.state);
  REQUIRE(v.rows.size() == 1);
  CHECK(v.rows[0].html == "<b>Haus</b> дом, здание");
  CHECK(v.back == "All dictionaries");
  CHECK(v.pager.empty());
  n.back();
  CHECK(card_view(n.card(), n.state).rows.size() == 2);
}

TEST_CASE("views: full entries show one dictionary at a time, ◀ ▶ turn") {
  auto c = word();
  c.entries.push_back({"Mueller", "Haus — дом", "<b>Haus</b> дом, здание", "mueller", "Haus"});
  CardNav n;
  n.look("Häusern", Source::input);
  n.arrived(c);
  n.full_entries();
  auto v = card_view(n.card(), n.state);
  REQUIRE(v.rows.size() == 1);
  CHECK(v.rows[0].html == "<b>Haus</b> дом");
  CHECK(v.pager == "1 / 2");
  CHECK(!v.prev);
  CHECK(v.next);
  CHECK(v.back.empty());
  auto at = n.place();
  n.turn(1);
  v = card_view(n.card(), n.state);
  REQUIRE(v.rows.size() == 1);
  CHECK(v.rows[0].html == "<b>Haus</b> дом, здание");
  CHECK(v.pager == "2 / 2");
  CHECK(v.prev);
  CHECK(!v.next);
  CHECK(n.place() != at);  // the body scrolls to the top
  n.turn(1);
  CHECK(n.state.entry_page == 1);
}

TEST_CASE("core: prepare() goes one dictionary at a time and stops when the library is made again") {
  auto d = fs::temp_directory_path() / "lexipeek-prepare";
  wipe(d);
  CoreOptions o;
  o.data = d;
  Core core(o);
  std::thread t([&] { core.prepare(); });
  core.reload();
  t.join();
  core.prepare();
  CHECK(core.lookup(Request{.text = "Haus", .source = Source::cli}).kind == Card::Kind::word);
  wipe(d);
}

TEST_CASE("help: topics in the app's language, each with its items; a pane to open where there is one") {
  auto h = help_view("⌥D", "⌥A");
  REQUIRE(h.size() == 8);
  CHECK(h[0].title == "Getting started");
  CHECK(h[0].items[1].text.find("⌥D") != std::string::npos);
  CHECK(h[0].go.empty());
  CHECK(h[3].go == "Open Translation");
  CHECK(h[3].section == SettingsSection::languages);
  CHECK(h[3].tab == 1);
  for (auto &t : h)
    for (auto &i : t.items) CHECK(!i.text.empty());
}

TEST_CASE("views: copy takes the translation, a dictionary's line or an AI answer; a failed AI answer offers its settings") {
  auto c = word();
  auto s = first_state(c);
  CHECK(card_view(c, s).copy == "дом");
  c.ai.analysis = true;
  s.tab = CardTab::analysis;
  s.analysis = {false, true, false, false, "", "Key rejected"};
  auto v = card_view(c, s);
  CHECK(v.fix == "Open Settings → AI");
  CHECK(v.copy.empty());
}

TEST_CASE("views: no language pack — the card says so and opens Settings → Packs") {
  auto c = word();
  c.entries.clear(), c.lang.clear(), c.no_packs = true;
  auto s = first_state(c);
  s.tab = CardTab::translate;
  auto v = card_view(c, s);
  REQUIRE(!v.rows.empty());
  CHECK(v.rows[0].text.starts_with("No language pack installed yet"));
  CHECK(v.fix == "Open Settings → Packs");
  CHECK(v.fix_section == SettingsSection::languages);
  c.no_packs = false, c.other = "fr", c.other_name = "French";
  v = card_view(c, s);
  CHECK(v.fix == "Open Settings → Packs");  // a pack from French is not installed
}

TEST_CASE("app: the lookup hotkey's step") {
  CHECK(app::hotkey_step(false, true, "Haus", "") == app::HotkeyStep::show);
  CHECK(app::hotkey_step(true, true, "Baum", "Haus") == app::HotkeyStep::show);  // another selection: its card at once
  CHECK(app::hotkey_step(true, true, "Haus", "Haus") == app::HotkeyStep::close);
  CHECK(app::hotkey_step(true, false, "", "Haus") == app::HotkeyStep::close);
  CHECK(app::hotkey_step(false, false, "", "") == app::HotkeyStep::type);
}

TEST_CASE("app: hotkey labels, lexipeek:// links, menu lines, Notes' file names") {
  Hotkey h{"D", true, false, true, false};
  CHECK(app::hotkey_label(h) == "⌃⌥D");
  set_ui_platform("windows");
  CHECK(app::hotkey_label(h) == "Ctrl+Alt+D");
  set_ui_language("de");
  CHECK(app::hotkey_label(h) == "Strg+Alt+D");
  CHECK(app::hotkey_label(Hotkey{"D", false, true, false, true}) == "Win+Umschalt+D");
  set_ui_language("en");
  set_ui_platform("mac");
  auto l = app::link("lexipeek://lookup?text=Tr%C3%A4umen%20gehen");
  CHECK(l.lookup);
  CHECK(l.text == "Träumen gehen");
  CHECK(app::link("LEXIPEEK://input").input);
  CHECK(!app::link("lexipeek://lookup?text=%20").lookup);
  CHECK(!app::link("lexipeek://other?text=a").lookup);
  CHECK(!app::link("https://lookup?text=a").lookup);
  CHECK(app::menu_line("one\ntwo") == "one two");
  auto cut = app::menu_line(std::string(50, 'x'));
  CHECK(cut.size() == 39 + std::string("…").size());
  CHECK(app::menu_line(std::string(40, 'y')).size() == 40);
  CHECK(app::notes_file(NotesTab::phrases).starts_with("lexipeek-phrases-"));
  CHECK(app::backup_file().ends_with(".db"));
}

TEST_CASE("views: a phrase set to open on AI opens offline when the provider's key is missing") {
  auto c = phrase();
  c.ai.translate = true, c.ai.key = true, c.prefs.phrase_open = "ai";
  CHECK(first_state(c).by_ai);
  c.ai.no_key = true;
  CHECK(!first_state(c).by_ai);
}
