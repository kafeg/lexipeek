// The first-run window's view model and Mozilla's English in between (setting, card row, store).
#include <doctest/doctest.h>
#include "temp.hpp"
#include <cstdlib>
#include "lexipeek/app.hpp"
#include "lexipeek/core.hpp"
#include "lexipeek/views.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

namespace {

Manifest manifest() {
  return parse_manifest(R"({"format": 1, "components": [
    {"id": "dict-de-ru", "kind": "dict", "from": "de", "to": "ru", "stats": {"coverage": 92}, "file": {"name": "a", "size": 30000000, "sha256": "d1"}},
    {"id": "dict-fr-ru", "kind": "dict", "from": "fr", "to": "ru", "stats": {"coverage": 70}, "file": {"name": "b", "size": 20000000, "sha256": "d2"}},
    {"id": "dict-it-ru", "kind": "dict", "from": "it", "to": "ru", "stats": {"coverage": 40}, "file": {"name": "h", "size": 1000000, "sha256": "d4"}},
    {"id": "dict-de-en", "kind": "dict", "from": "de", "to": "en", "stats": {"coverage": 92}, "file": {"name": "c", "size": 10000000, "sha256": "d3"}},
    {"id": "model-de-en", "kind": "model", "from": "de", "to": "en", "file": {"name": "d", "size": 17000000, "sha256": "m1"}},
    {"id": "model-fr-en", "kind": "model", "from": "fr", "to": "en", "file": {"name": "e", "size": 17000000, "sha256": "m2"}},
    {"id": "model-it-en", "kind": "model", "from": "it", "to": "en", "file": {"name": "i", "size": 17000000, "sha256": "m4"}},
    {"id": "model-en-ru", "kind": "model", "from": "en", "to": "ru", "file": {"name": "f", "size": 17000000, "sha256": "m3"}},
    {"id": "langid", "kind": "langid", "file": {"name": "g", "size": 1000000, "sha256": "l"}}]})");
}

fs::path models() {
  const char *v = std::getenv("LEXIPEEK_MODELS");  // NOLINT(concurrency-mt-unsafe)
  return v && *v ? fs::path(v) : fs::path();
}

Card german(const std::string &text) {
  Card c;
  c.kind = Card::Kind::passage;
  c.source = Source::hotkey;
  c.text = text;
  c.lang = "de", c.answer = "ru";
  c.method = Method::fast;
  return c;
}

}  // namespace

TEST_CASE("first run: sizes, at launch, the default answer language") {
  CHECK(size_text(84'400'000) == "84 MB");
  CHECK(size_text(10) == "1 MB");
  CHECK(size_text(1'230'000'000) == "1.2 GB");
  CHECK(first_run_at_launch(false, true, true));
  CHECK(first_run_at_launch(true, false, false));
  CHECK(!first_run_at_launch(true, false, true));
  CHECK(!first_run_at_launch(true, true, false));
  auto m = manifest();
  CHECK(first_answer(m, "ru") == "ru");
  CHECK(first_answer(m, "ja") == "en");
  CHECK(!has_pair(m, {}));
  CHECK(has_pair(m, {{"dict-de-en", "d3"}, {"model-de-en", "m1"}, {"langid", "l"}}));
}

TEST_CASE("first run on Windows: no Accessibility step, the pair is 1 of 2") {
  auto m = manifest();
  FirstRunState s;
  s.trusted = true;
  set_ui_platform("windows");
  auto v = first_run_view(m, {}, s);
  CHECK(v.step == FirstStep::pair);
  CHECK(v.number == 1);
  CHECK(v.steps == 2);
  v = first_run_view(m, {{"dict-de-en", "d3"}, {"model-de-en", "m1"}, {"langid", "l"}}, s);
  CHECK(v.step == FirstStep::done);
  CHECK(v.number == 2);
  set_ui_platform("mac");
}

TEST_CASE("first run: Accessibility, then the pair, its download, the hotkey") {
  auto m = manifest();
  FirstRunState s;
  s.system = "ru";
  auto v = first_run_view(m, {}, s);
  CHECK(v.step == FirstStep::access);
  CHECK(v.number == 1);
  CHECK(v.button == "Open System Settings");
  CHECK(v.secondary == "Later");
  CHECK(v.text.find("⌥D") != std::string::npos);

  // Granted: the pair, into the system's language, books by level.
  s.trusted = true;
  v = first_run_view(m, {}, s);
  CHECK(v.step == FirstStep::pair);
  CHECK(v.number == 2);
  CHECK(v.answer == "ru");
  REQUIRE(v.answers.size() == 2);
  CHECK(v.answers[0].title == "English");
  REQUIRE(v.books.size() == 3);
  CHECK(v.books[0].title == "German");
  CHECK(v.books[0].detail == "Full · 65 MB");
  CHECK(v.books[1].code == "fr");
  CHECK(v.books[2].detail == "Experimental · 36 MB");
  CHECK(v.book == "de");
  CHECK(v.button == "Download 65 MB");
  s.book = "fr";
  CHECK(first_run_view(m, {}, s).button == "Download 55 MB");

  // Another answer language: its books; a book it has not is replaced by its first.
  s.answer = "en";
  v = first_run_view(m, {}, s);
  CHECK(v.answer == "en");
  REQUIRE(v.books.size() == 1);
  CHECK(v.book == "de");

  // An unknown system language: English.
  FirstRunState t;
  t.trusted = true;
  t.system = "ja";
  CHECK(first_run_view(m, {}, t).answer == "en");

  // The download, its error, then the pair is there.
  s.answer = "ru", s.book = "de";
  s.downloading = true, s.done = 13'000'000, s.total = 65'000'000;
  v = first_run_view(m, {}, s);
  CHECK(v.step == FirstStep::download);
  CHECK(v.title == "Downloading German → Russian");
  CHECK(v.progress);
  CHECK(v.fraction == doctest::Approx(0.2));
  CHECK(v.progress_text == "13 MB of 65 MB");
  CHECK(v.button == "Cancel");
  s.downloading = false, s.error = "sha256 mismatch";
  v = first_run_view(m, {}, s);
  CHECK(v.error == "sha256 mismatch");
  CHECK(v.button == "Try Again");
  CHECK(v.secondary == "Back");
  s.error.clear();
  v = first_run_view(m, {{"dict-de-ru", "d1"}, {"model-de-en", "m1"}, {"model-en-ru", "m3"}, {"langid", "l"}}, s);
  CHECK(v.step == FirstStep::done);
  CHECK(v.number == 3);
  CHECK(v.text == "Select a word or a sentence in any app and press ⌥D. LexiPeek lives in the menu bar.");
  CHECK(v.button == "Close");

  // Later on Accessibility: the pair anyway; done says what works without it.
  FirstRunState later;
  later.access_later = true;
  later.hotkey = "⌃⌥L";
  CHECK(first_run_view(m, {}, later).step == FirstStep::pair);
  v = first_run_view(m, {{"dict-de-ru", "d1"}, {"model-de-en", "m1"}, {"model-en-ru", "m3"}, {"langid", "l"}}, later);
  CHECK(v.text.find("⌃⌥L") != std::string::npos);
  CHECK(v.note.find("⌥A") != std::string::npos);
}

TEST_CASE("first run: no packs list yet") {
  FirstRunState s;
  s.trusted = true;
  s.fetching = true;
  auto v = first_run_view({}, {}, s);
  CHECK(v.step == FirstStep::list);
  CHECK(v.progress);
  CHECK(v.button.empty());  // no "Try again" while it is fetched
  CHECK(v.text == "Getting the list of language packs…");
  s.fetching = false;
  s.list_error = "Could not resolve host";
  v = first_run_view({}, {}, s);
  CHECK(v.error == "Could not resolve host");
  CHECK(v.button == "Try Again");
  CHECK(v.button_enabled);
}

TEST_CASE("app: the first run on an empty data dir") {
  auto d = (fs::temp_directory_path() / "lexipeek-firstrun").string();
  wipe(d);
  CHECK(!app::has_pair(d));
  CHECK(app::first_run_at_launch(d, true));
  FirstRunState s;
  s.trusted = true;
  CHECK(app::first_run(d, s).step == FirstStep::list);
  auto st = app::settings(d);
  CHECK(!st.first_run);
  CHECK(!st.mt_english);
  st.first_run = st.mt_english = true;
  CHECK(app::save(d, st).empty());
  CHECK(app::settings(d).first_run);
  CHECK(app::settings(d).mt_english);
  wipe(d);
}

TEST_CASE("views: the English in between, under the translation") {
  Card c = german("Das Haus ist alt.");
  c.methods = {{Method::fast, "Fast · Mozilla", true, ""}};
  auto s = first_state(c);
  s.translated = true;
  s.translation = {"Дом старый.", "The house is old.", "Mozilla", ""};
  auto v = card_view(c, s);
  REQUIRE(v.rows.size() == 2);
  CHECK(v.rows[0].style == CardRow::Style::text);
  CHECK(v.rows[1].style == CardRow::Style::english);
  CHECK(v.rows[1].text == "The house is old.");
  s.translation.english.clear();
  CHECK(card_view(c, s).rows.size() == 1);
}

TEST_CASE("core: the English in between is kept as the text's English, shown when asked") {
  auto d = fs::temp_directory_path() / "lexipeek-english";
  wipe(d);
  {
    CoreOptions o;
    o.data = d;
    Core core(o);
    auto c = german("Das Haus ist alt.");
    auto ver = answer_version({}, {}, "de", "ru", Method::fast);
    auto en = answer_version({}, {}, "de", "en", Method::fast);
    core.store().seen(c.text, "phrase", "ru", "", "");  // answers are kept for texts in the history, as a card's
    core.store().put_answer(c.text, "ru", "offline-de", ver, "bergamot", "Дом старый.");
    core.store().put_answer(c.text, "en", "offline-de", en, "bergamot", "The house is old.");
    auto t = core.translate(c);
    CHECK(t.text == "Дом старый.");
    CHECK(t.english.empty());
    Settings s;
    s.mt_english = true;
    core.configure(s);
    t = core.translate(c);
    CHECK(t.english == "The house is old.");
    // Not through English: no English.
    c.answer = "en";
    core.store().put_answer(c.text, "en", "offline-de", en, "bergamot", "The house is old.");
    CHECK(core.translate(c).english.empty());
    // Direct: no English either.
    c.answer = "ru";
    CHECK(core.translate(c, Method::direct).english.empty());
  }
  wipe(d);
}

TEST_CASE("core with models: a fast translation keeps its English; the step alone when kept before") {
  auto src = models();
  if (src.empty() || !fs::exists(src / "models/deen/set") || !fs::exists(src / "models/enru/set")) {
    MESSAGE("skipped: no models/deen, models/enru in LEXIPEEK_MODELS");
    return;
  }
  auto d = fs::temp_directory_path() / "lexipeek-english-models";
  wipe(d);
  fs::create_directories(d);
  for (auto f : {"models", "packs.json", "installed.json"})
    if (fs::exists(src / f)) fs::create_symlink(src / f, d / f);
  {
    CoreOptions o;
    o.data = d;
    Core core(o);
    auto c = german("Das Haus ist groß.");
    core.store().seen(c.text, "phrase", "ru", "", "");
    auto t = core.translate(c);
    REQUIRE(t.ok());
    CHECK(t.english.empty());
    Packs p(d, "");
    auto en = answer_version(p.cached(), p.installed(), "de", "en", Method::fast);
    auto kept = core.store().answer(c.text, "en", "offline-de", en);
    REQUIRE(kept);
    CHECK(kept->find("house") != std::string::npos);
    Settings s;
    s.mt_english = true;
    core.configure(s);
    CHECK(core.translate(c).english == *kept);
    // Translated before the setting: the English step only.
    auto c2 = german("Der Baum ist grün.");
    auto ver = answer_version(p.cached(), p.installed(), "de", "ru", Method::fast);
    core.store().seen(c2.text, "phrase", "ru", "", "");
    core.store().put_answer(c2.text, "ru", "offline-de", ver, "bergamot", "Дерево зелёное.");
    t = core.translate(c2);
    CHECK(t.text == "Дерево зелёное.");
    CHECK(t.english.find("tree") != std::string::npos);
    CHECK(core.store().answer(c2.text, "en", "offline-de", en) == t.english);
  }
  wipe(d);
}
