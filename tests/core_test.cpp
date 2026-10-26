// Core tests that need no models.
#include <doctest/doctest.h>
#include <fstream>
#include <thread>
#include "lexipeek/core.hpp"
#include "lexipeek/mt.hpp"
#include "lexipeek/packs.hpp"
#include "lexipeek/settings.hpp"
#include "lexipeek/views.hpp"
#include "mt/model_host.hpp"
#include "mt/text.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

TEST_CASE("steps go through English when neither side is English") {
  using V = std::vector<std::pair<std::string, std::string>>;
  CHECK(mt::steps("de", "ru") == V{{"de", "en"}, {"en", "ru"}});
  CHECK(mt::steps("de", "en") == V{{"de", "en"}});
  CHECK(mt::steps("en", "ru") == V{{"en", "ru"}});
  CHECK(mt::steps("de", "de").empty());
}

TEST_CASE("bergamot config picks the files and the gemm precision") {
  auto d = fs::temp_directory_path() / "lexipeek-config-test";
  fs::remove_all(d);
  fs::create_directories(d);
  CHECK(mt::bergamot_config(d).empty());
  for (auto n : {"model.deen.intgemm.alphas.bin", "lex.50.50.deen.s2t.bin", "vocab.deen.spm", "set"}) std::ofstream(d / n) << "x";
  auto c = mt::bergamot_config(d);
  CHECK(c.find("gemm-precision: int8shiftAlphaAll") != std::string::npos);
  CHECK(c.find("shortlist: [") != std::string::npos);
  CHECK(c.find("vocab.deen.spm, " + (d / "vocab.deen.spm").string()) != std::string::npos);
  fs::remove_all(d);
  fs::create_directories(d);
  for (auto n : {"model.bin", "srcvocab.spm", "trgvocab.spm"}) std::ofstream(d / n) << "x";
  c = mt::bergamot_config(d);
  CHECK(c.find("gemm-precision: int8shiftAll\n") != std::string::npos);
  CHECK(c.find("shortlist") == std::string::npos);
  CHECK(c.find("trgvocab.spm]") != std::string::npos);
  fs::remove_all(d);
}

TEST_CASE("sentences and chunks") {
  auto s = mt::sentences("Er kam spät. Sie wartete!  Warum?");
  REQUIRE(s.size() == 3);
  CHECK(s[0] == "Er kam spät.");
  CHECK(s[2] == "Warum?");
  CHECK(mt::chunks("kurz").size() == 1);
  auto c = mt::chunks("Eins zwei. Drei vier. Fünf sechs.", 22);
  REQUIRE(c.size() == 2);
  CHECK(c[0] == "Eins zwei. Drei vier.");
}

TEST_CASE("utf16 length counts as Kotlin") {
  CHECK(mt::utf16_length("Haus") == 4);
  CHECK(mt::utf16_length("Straße") == 6);
  CHECK(mt::utf16_length("дом") == 3);
  CHECK(mt::utf16_length("😀") == 2);
}

TEST_CASE("prompt template") {
  CHECK(mt::fill("{from}→{to}: {text}", "German", "Russian", "a {to} b") == "German→Russian: a {to} b");
}

TEST_CASE("packs.json") {
  auto m = parse_manifest(R"({"format": 1, "generated": "x", "components": [
    {"id": "opus-a", "kind": "opus", "from": "", "to": "", "covers": ["de-ru", "de-uk"], "tags": {"ru": ">>rus<<"},
     "ram_mb": 3500, "file": {"name": "opus-a.tar.zst", "size": 5, "sha256": "ab"}, "future": [1]},
    {"id": "llm-b", "kind": "llm", "covers": ["de-ru"], "template": "{text}", "names": {"de": "German"}, "cpu": ["asimddp"],
     "file": {"name": "b.gguf", "size": 7, "sha256": "cd"}},
    {"id": "dict-de-ru", "kind": "dict", "from": "de", "to": "ru", "stats": {"coverage": 91.5},
     "file": {"name": "d.zip", "size": 1, "sha256": "ef"}}]})");
  REQUIRE(m.error.empty());
  REQUIRE(m.components.size() == 3);
  CHECK(m.components[0].tags.at("ru") == ">>rus<<");
  CHECK(m.components[0].ram_mb == 3500);
  CHECK(m.components[1].names.at("de") == "German");
  CHECK(m.components[2].coverage == doctest::Approx(91.5));
  CHECK(covering(m, "opus", "de", "uk")->id == "opus-a");
  CHECK(covering(m, "llm", "de", "ru")->id == "llm-b");
  CHECK(!covering(m, "opus", "ru", "de"));
  CHECK(!parse_manifest(R"({"format": 2, "components": []})").error.empty());
  CHECK(!parse_manifest("{").error.empty());
}

TEST_CASE("translator reports a missing model") {
  TranslatorOptions o;
  o.data = fs::temp_directory_path() / "lexipeek-empty";
  Translator tr(o);
  auto r = tr.translate("de", "ru", "Haus", Method::fast);
  CHECK(!r.ok());
  CHECK(r.error.find("model-de-en") != std::string::npos);
  CHECK(tr.translate("de", "de", "Haus", Method::fast).text == "Haus");
  CHECK(!tr.translate("de", "ru", "Haus", Method::direct).ok());
  CHECK(tr.langid("Haus").empty());
}

namespace {
struct Fake : mt::Held {
  int *alive;
  explicit Fake(int *a) : alive(a) { ++*alive; }
  ~Fake() override { --*alive; }
};
}  // namespace

TEST_CASE("model host: one model, idle unload") {
  using namespace std::chrono_literals;
  mt::ModelHost host;
  int alive = 0, loads = 0;
  auto load = [&] { return ++loads, std::make_unique<Fake>(&alive); };
  host.with("a", 200ms, load, [](mt::Held &) {});
  host.with("a", 200ms, load, [](mt::Held &) {});
  CHECK(loads == 1);
  host.with("b", 200ms, load, [](mt::Held &) {});
  CHECK(alive == 1);  // a freed before b
  CHECK(host.held() == "b");
  host.with("b", 50ms, load, [](mt::Held &) { std::this_thread::sleep_for(100ms); });  // not freed while in use
  CHECK(host.held() == "b");
  for (int i = 0; i < 100 && !host.held().empty(); i++) std::this_thread::sleep_for(10ms);
  CHECK(host.held().empty());
  CHECK(alive == 0);
  CHECK_THROWS(host.with("c", 50ms, [&]() -> std::unique_ptr<mt::Held> { throw std::runtime_error("x"); }, [](mt::Held &) {}));
  CHECK(host.held().empty());
  host.with("d", 1h, load, [](mt::Held &) {});
  host.release();
  CHECK(alive == 0);
}

TEST_CASE("core: an empty data dir gives a card without methods") {
  CoreOptions o;
  o.data = fs::temp_directory_path() / "lexipeek-empty";
  Core core(o);
  CHECK(core.lookup(Request{.text = "  "}).kind == Card::Kind::empty);
  auto c = core.lookup(Request{.text = "Das Haus"});
  CHECK(c.kind == Card::Kind::phrase);
  CHECK(c.text == "Das Haus");
  CHECK(c.answer == "ru");
  CHECK(!c.method);
  CHECK(!core.translate(c).ok());
}

TEST_CASE("settings: defaults, round trip, unknown fields kept") {
  auto d = fs::temp_directory_path() / "lexipeek-settings-test";
  fs::remove_all(d);
  auto s = load_settings(d);
  CHECK(s.answer == "ru");
  CHECK(s.lookup.key == "D");
  CHECK(s.input.key == "A");
  fs::create_directories(d);
  std::ofstream(d / "settings.json") << R"({"future": 1, "answer": "en"})";
  s = load_settings(d);
  CHECK(s.answer == "en");
  s.method = Method::direct;
  s.copy_apps = {"com.example.app"};
  s.selection_icon = true;
  CHECK(save_settings(d, s).empty());
  auto t = load_settings(d);
  CHECK(t.method == Method::direct);
  CHECK(t.copy_apps.size() == 1);
  CHECK(t.selection_icon);
  CHECK(!t.pin_moved);
  std::ifstream f(d / "settings.json");
  std::string all((std::istreambuf_iterator<char>(f)), {});
  CHECK(all.find("future") != std::string::npos);
  std::ofstream(d / "settings.json") << "{broken";
  CHECK(load_settings(d).answer == "ru");
  fs::remove_all(d);
}

TEST_CASE("core: a card is a look in the history, not the CLI's") {
  auto d = fs::temp_directory_path() / "lexipeek-core-store";
  fs::remove_all(d);
  {
    CoreOptions o;
    o.data = d;
    Core core(o);
    core.lookup(Request{.text = "Haus", .source = Source::hotkey});
    core.lookup(Request{.text = "Haus", .source = Source::hotkey});
    core.lookup(Request{.text = "Baum", .source = Source::cli});
    auto r = core.recents(5);
    REQUIRE(r.size() == 1);
    CHECK(r[0].text == "Haus");
    CHECK(r[0].count == 2);
    CHECK(core.lookup(Request{.text = "Haus", .source = Source::cli}).looks == 2);
  }
  fs::remove_all(d);
}


TEST_CASE("views: the packs list") {
  auto m = parse_manifest(R"({"format": 1, "components": [
    {"id": "dict-de-ru", "kind": "dict", "from": "de", "to": "ru", "stats": {"coverage": 90}, "file": {"name": "a", "size": 10, "sha256": "d1"}},
    {"id": "dict-it-ru", "kind": "dict", "from": "it", "to": "ru", "stats": {"coverage": 50}, "file": {"name": "b", "size": 10, "sha256": "d2"}},
    {"id": "model-de-en", "kind": "model", "from": "de", "to": "en", "file": {"name": "c", "size": 20, "sha256": "m1"}},
    {"id": "model-en-ru", "kind": "model", "from": "en", "to": "ru", "file": {"name": "d", "size": 30, "sha256": "m2"}},
    {"id": "langid", "kind": "langid", "file": {"name": "e", "size": 1, "sha256": "l"}},
    {"id": "llm-x", "kind": "llm", "name": "Hy-MT2 1.8B", "covers": ["de-ru"], "ram_mb": 5000, "file": {"name": "f", "size": 100, "sha256": "x"}}]})");
  auto rows = pack_rows(m, {{"dict-de-ru", "old"}, {"model-de-en", "m1"}}, Device{4096, {}});
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].title == "German → Russian");
  CHECK(rows[0].level == "full");
  CHECK(!rows[0].installed);
  CHECK(rows[0].missing == 31);  // en-ru and langid
  CHECK(rows[0].base[0].update);
  REQUIRE(rows[0].llm);
  CHECK(rows[0].llm->cannot.find("memory") != std::string::npos);
  CHECK(!rows[0].direct);
  CHECK(rows[1].level == "experimental");
}
