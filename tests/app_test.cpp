// The facade's own writes and the engine's file work: the app's language, one setting each, a pack removed under a
// running engine, the held model and Read aloud's lines without a model.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <fstream>
#include "lexipeek/app.hpp"
#include "lexipeek/l10n.hpp"
#include "lexipeek/speech.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

namespace {

void be32(std::string &s, uint32_t v) {
  for (int i = 3; i >= 0; i--) s += static_cast<char>((v >> (i * 8)) & 0xff);
}

// <data>/dicts/dict-de-ru with Haus, marked in installed.json as the pack manager does.
fs::path with_pack(const std::string &name) {
  auto d = fs::temp_directory_path() / name;
  wipe(d);
  auto dir = d / "dicts" / "dict-de-ru";
  fs::create_directories(dir);
  std::string a = "<p class=\"head\">das Haus (Hauses, pl. Häuser) — дом</p>", idx = std::string("Haus") + '\0';
  be32(idx, 0);
  be32(idx, static_cast<uint32_t>(a.size()));
  std::ofstream(dir / "dict-de-ru.idx", std::ios::binary) << idx;
  std::ofstream(dir / "dict-de-ru.dict", std::ios::binary) << a;
  std::ofstream(dir / "dict-de-ru.ifo") << "StarDict's dict ifo file\nversion=3.0.0\nbookname=de-ru\nwordcount=1\nidxfilesize=" << idx.size()
                                         << "\nsametypesequence=h\n";
  std::ofstream(d / "installed.json") << R"({"dict-de-ru": "00"})";
  return d;
}

}  // namespace

TEST_CASE("app: the app's language — the setting, else the system's when it is a UI language, else English; a new install answers in it") {
  auto d = fs::temp_directory_path() / "lexipeek-app-language";
  wipe(d);
  fs::create_directories(d);
  auto ds = d.string();
  CHECK(app::set_language(ds, "de") == "de");
  CHECK(ui_language() == "de");
  CHECK(app::settings(ds).answer == "de");  // no settings.json yet: the new install's answer
  CHECK(app::set_language(ds, "ja") == "en");  // not a UI language: English
  CHECK(app::settings(ds).answer == "de");      // settings.json there now: its answer stays
  CHECK(app::set_language(ds, "fr") == "fr");
  auto s = app::settings(ds);
  s.ui_lang = "pl";
  CHECK(app::save(ds, s).empty());
  CHECK(app::set_language(ds, "fr") == "pl");  // the setting wins
  CHECK(app::text("cancel") == tr("cancel"));
  set_ui_language("en");
  wipe(d);
}

TEST_CASE("app: one setting each — the method picked in a card, the first run done, the first pair's answer") {
  auto d = fs::temp_directory_path() / "lexipeek-app-one";
  wipe(d);
  fs::create_directories(d);
  auto ds = d.string();
  std::ofstream(d / "settings.json") << R"({"method": "off", "some_newer_field": 7})";
  CHECK(app::method_id(app::settings(ds)) == "off");
  CHECK(app::pick_method(ds, Method::fast).empty());
  CHECK(app::method_id(app::settings(ds)) == "fast");  // picked: no longer off
  CHECK(!app::settings(ds).first_run);
  CHECK(app::finish_first_run(ds).empty());
  CHECK(app::settings(ds).first_run);
  CHECK(app::set_answer(ds, "en").empty());
  CHECK(app::settings(ds).answer == "en");
  std::ifstream f(d / "settings.json");
  std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  CHECK(all.find("some_newer_field") != std::string::npos);  // a field this build does not know is kept
  auto s = app::settings(ds);
  app::set_method(s, "off");
  CHECK(s.mt_off);
  app::set_method(s, "direct");
  CHECK(!s.mt_off);
  CHECK(app::method_id(s) == "direct");
  wipe(d);
}

TEST_CASE("app: Settings → Packs' Remove under a running engine — files and installed.json gone, the next card without it") {
  set_ui_language("en");
  auto d = with_pack("lexipeek-app-remove");
  auto ds = d.string();
  {
    auto e = app::Engine::make(ds, app::device(16384, {"asimddp"}, "arm64"));
    auto c = e.lookup("Haus", Source::hotkey);
    REQUIRE(c.entries.size() == 1);
    CHECK(app::has_pair(ds) == false);  // a dictionary alone is no pair to the first run
    CHECK(e.remove(ds, {"dict-de-ru"}).ok());
    CHECK(!fs::exists(d / "dicts" / "dict-de-ru"));
    std::ifstream f(d / "installed.json");
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(all.find("dict-de-ru") == std::string::npos);
    e.reload();
    CHECK(e.lookup("Haus", Source::hotkey).entries.empty());
    // Not installed: nothing to remove, no error.
    CHECK(e.remove(ds, {"dict-de-ru"}).ok());
    CHECK(app::remove(ds, {"dict-de-ru"}).ok());
  }
  wipe(d);
}

TEST_CASE("app: the held model's menu line, Read aloud's without a model, a voice from a recording that cannot be made") {
  set_ui_language("en");
  auto d = fs::temp_directory_path() / "lexipeek-app-memory";
  wipe(d);
  fs::create_directories(d);
  auto ds = d.string();
  {
    auto e = app::Engine::make(ds, app::device(16384, {"asimddp"}, "arm64"));
    CHECK(!e.memory().held);
    e.release();
    CHECK(!e.memory().held);
    CHECK(!e.speech_memory().held);
    e.release_speech();
    e.remove_speech();  // nothing downloaded: nothing to do
    CHECK(!speech::model_installed(d));
    auto wav = d / "voice.wav";
    std::ofstream(wav, std::ios::binary) << speech::wav_bytes(std::vector<float>(24000, 0), 24000);
    CHECK(!e.add_voice(wav.string(), "Guten Tag", "de").empty());  // no voice model: an error, nothing kept
    CHECK(speech::own_voices(d).empty());
    e.remove_voice("none");
    CHECK(!app::speech_languages().empty());
  }
  wipe(d);
}
