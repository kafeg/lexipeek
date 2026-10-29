// Read aloud (M9): what ▶ reads, a reading's length, voices, WAV, kept readings, the settings, the card's session.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <cmath>
#include <fstream>
#include <thread>
#include "lexipeek/session.hpp"
#include "lexipeek/speech.hpp"
#include "lexipeek/views.hpp"

using namespace lexipeek;
using namespace lexipeek::speech;
namespace fs = std::filesystem;

namespace {

Card german() {
  Card c;
  c.kind = Card::Kind::word;
  c.text = "Häusern";
  c.lang = "de", c.answer = "ru";
  c.header = "das Haus (Hauses, pl. Häuser)";
  c.entries = {{"InkPeek · de → ru", "das Haus — дом", "", "de-ru", "Haus"}};
  c.prefs.read = true;
  return c;
}

// A Russian word shown in German, the language learnt.
Card learnt() {
  Card c;
  c.kind = Card::Kind::word;
  c.text = "дом";
  c.lang = c.answer = "ru";
  c.entries = {{"InkPeek · ru → de", "дом — Haus, Gebäude", "", "ru-de", "дом"}};
  c.study = {{"de", 0, {{"Haus", "Haus", "", "das Haus (Hauses, pl. Häuser)", 120}, {"Gebäude", "Gebäude", ""}}}};
  c.study[0].has_method = true;
  c.prefs.read = true;
  return c;
}

void put(const fs::path &p, const std::string &bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << bytes;
}

fs::path temp(const char *name) {
  auto d = fs::temp_directory_path() / name;
  wipe(d);
  return d;
}

}  // namespace

TEST_CASE("read aloud: a text in another language is read itself; the answer language only with a language you learn") {
  auto c = german();
  auto s = first_state(c);
  CHECK(card_read(c, s, CardTab::dictionary) == Read{"Häusern", "de"});
  CHECK(card_read(c, s, CardTab::translate) == Read{"Häusern", "de"});
  auto v = card_view(c, s);
  CHECK(v.read == Read{"Häusern", "de"});
  CHECK(!v.reading);
  CHECK(v.read_help == "Read aloud (⌘.)");
  c.prefs.read = false;
  CHECK(card_view(c, s).read.text.empty());
  c = german();
  c.lang = "ru";
  CHECK(card_read(c, s, CardTab::translate).text.empty());  // already in the answer language, nothing learnt
  c.lang = "ja";
  CHECK(card_read(c, s, CardTab::translate).text.empty());  // no voice for it
  c.lang = "";
  CHECK(card_read(c, s, CardTab::translate).text.empty());
}

TEST_CASE("read aloud: a language you learn — the translation on Translate, the translated words on Dictionary, each with ▶") {
  auto c = learnt();
  auto s = first_state(c);
  CHECK(card_read(c, s, CardTab::translate).text.empty());  // no translation yet
  Translation t;
  t.text = "das Haus";
  s.learned = {t};
  CHECK(card_read(c, s, CardTab::translate) == Read{"das Haus", "de"});
  s.by_ai = true;
  CHECK(card_read(c, s, CardTab::translate).text.empty());
  s.by_ai = false;
  CHECK(card_read(c, s, CardTab::dictionary) == Read{"Haus", "de"});
  s.tab = CardTab::dictionary;
  s.reading = {"Gebäude", "de"};
  auto v = card_view(c, s);
  std::vector<Read> rows;
  for (auto &r : v.rows)
    if (!r.read.text.empty()) rows.push_back(r.read);
  CHECK(rows == std::vector<Read>{{"Haus", "de"}, {"Gebäude", "de"}});
  CHECK(!v.reading);
  for (auto &r : v.rows)
    if (r.read.text == "Gebäude") CHECK(r.reading);
  s.reading = {"Haus", "de"};
  CHECK(card_view(c, s).reading);
  CHECK(card_view(c, s).read_help == "Stop (⌘.)");
}

TEST_CASE("read aloud: the card says the model is loading, and what failed with the way to Settings → Read aloud") {
  auto c = german();
  auto s = first_state(c);
  s.read_loading = true;
  auto v = card_view(c, s);
  REQUIRE(!v.rows.empty());
  CHECK(v.rows.back().style == CardRow::Style::progress);
  CHECK(v.rows.back().text == "Loading the voice model…");
  s.read_loading = false;
  s.read_error = "The voice model is not downloaded yet: Settings → Read aloud.", s.read_fix = true;
  v = card_view(c, s);
  CHECK(v.rows.back().style == CardRow::Style::error);
  CHECK(v.fix == "Open Settings → Read aloud");
  CHECK(v.fix_section == SettingsSection::speech);
}

TEST_CASE("read aloud: a reading's length by its text, the engine's limit at most") {
  CHECK(max_tokens("Haus") == 33);                         // 12.5 × (2 + 2 × 4 × 0.08)
  CHECK(max_tokens("42") == 48);                           // digits are said as words
  CHECK(max_tokens("Häuser") == max_tokens("Hauser"));     // letters, not bytes
  CHECK(max_tokens(std::string(5000, 'a')) == 2048);
  CHECK(max_tokens("Das Haus am Ende der Straße ist alt.") > max_tokens("Haus"));
}

TEST_CASE("read aloud: the model's languages; codec codes packed 11 bits each as qwentts.cpp's .rvq") {
  CHECK(model_language("de") == "german");
  CHECK(model_language("pt") == "portuguese");
  CHECK(model_language("pl") == "auto");
  CHECK(speaks("pl"));
  CHECK(!speaks("ja"));
  std::vector<std::int32_t> codes;
  for (int i = 0; i < codebooks * 3; i++) codes.push_back((i * 977) % 2048);
  auto packed = pack_codes(codes);
  CHECK(packed.size() == (codes.size() * code_bits + 7) / 8);
  CHECK(unpack_codes(packed) == codes);
  CHECK(model_bytes() == 2'079'448'256LL + 291'150'624LL);
}

TEST_CASE("read aloud: WAV in (PCM, any rate, stereo → mono 24 kHz) and out") {
  std::vector<float> tone;
  for (int i = 0; i < 2400; i++) tone.push_back(0.5f * static_cast<float>(std::sin(i * 0.05)));
  auto back = read_wav(wav_bytes(tone, 24000));
  REQUIRE(back.error.empty());
  REQUIRE(back.samples.size() == tone.size());
  for (size_t i = 0; i < tone.size(); i++) CHECK(std::abs(back.samples[i] - tone[i]) < 1e-4f);
  // 48 kHz stereo, 16-bit: half as many samples at 24 kHz, the channels averaged.
  std::string w = wav_bytes(std::vector<float>(9600, 0.25f), 48000);
  w[22] = 2, w[32] = 4;  // the same bytes as stereo: two channels, half the frames
  auto st = read_wav(w);
  REQUIRE(st.error.empty());
  CHECK(st.samples.size() == 2400);
  CHECK(std::abs(st.samples[1200] - 0.25f) < 0.01f);
  CHECK(!read_wav("not a wav").error.empty());
}

TEST_CASE("read aloud: voices — an own one first for its language, else the app's") {
  auto d = temp("lexipeek-voices");
  put(d / "voices/f00d.json", R"({"lang": "en", "text": "A clip of mine."})");
  put(d / "voices/f00d.spk", std::string(2048 * 4, '\0'));
  put(d / "voices/f00d.rvq", pack_codes(std::vector<std::int32_t>(codebooks * 10, 7)));
  auto own = own_voices(d);
  REQUIRE(own.size() == 1);
  CHECK(own[0].id == "f00d");
  CHECK(own[0].source == "own");
  CHECK(own[0].frames == 10);
  CHECK(own[0].spk.size() == 2048);
  SpeechSettings s;
  CHECK(voice_for(own, s, "en").id == "f00d");
  s.off = {"f00d"};
  if (!app_voices().empty()) {
    CHECK(voice_for(own, s, "de").id == "de-female");
    s.male = true;
    CHECK(voice_for(own, s, "en").id == "en-male");
  }
  CHECK(voice_for(own, s, "ja").id.empty());
  wipe(d);
}

TEST_CASE("read aloud: every language has a female and a male voice in the app, each with its clip's text" * doctest::skip(true)) {
  auto app = app_voices();
  for (auto &l : languages())
    for (auto g : {"-female", "-male"}) {
      auto id = l + g;
      auto v = std::find_if(app.begin(), app.end(), [&](auto &x) { return x.id == id; });
      REQUIRE_MESSAGE(v != app.end(), id);
      CHECK(v->lang == l);
      CHECK(v->spk.size() == 2048);
      CHECK(v->frames > 50);   // 5–12 s at 12.5 frames a second
      CHECK(v->frames < 160);
      CHECK(v->text.size() > 20);
    }
}

TEST_CASE("read aloud: a reading kept is played again without the model; off and no model say what to do") {
  auto d = temp("lexipeek-readings");
  Speaker sp(d);
  auto wait = [&](int t) {
    for (int i = 0; i < 500; i++) {
      auto st = sp.state();
      if (st.ticket == t && st.phase != Phase::making && st.phase != Phase::loading) return st;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return sp.state();
  };
  auto st = wait(sp.read({"Haus", "de"}));
  CHECK(st.phase == Phase::failed);
  CHECK(st.error == "Read aloud is off: turn it on in Settings → Read aloud.");
  CHECK(st.fix);
  SpeechSettings s;
  s.on = true;
  sp.configure(s);
  st = wait(sp.read({"Haus", "ja"}));
  CHECK(st.phase == Phase::failed);
  if (!built()) return wipe(d);
  if (!app_voices().empty()) {
    st = wait(sp.read({"Haus", "de"}));
    CHECK(st.error == "The voice model is not downloaded yet: Settings → Read aloud.");
    auto file = reading_file(d, voice_for({}, s, "de"), {"Haus", "de"});
    put(file, wav_bytes(std::vector<float>(100, 0), 24000));
    st = wait(sp.read({"Haus", "de"}));
    CHECK(st.phase == Phase::ready);
    CHECK(st.wav == file.string());
    CHECK(reading_file(d, voice_for({}, s, "de"), {"Haus", "de"}) != reading_file(d, voice_for({}, s, "de"), {"Häuser", "de"}));
    s.male = true;
    CHECK(reading_file(d, voice_for({}, s, "de"), {"Haus", "de"}) != file);
  }
  sp.stop();
  CHECK(sp.state().phase == Phase::idle);
  wipe(d);
}

TEST_CASE("read aloud: the newest readings kept") {
  auto d = temp("lexipeek-prune");
  for (int i = 0; i < 5; i++) {
    put(d / "readings" / (std::to_string(i) + ".wav"), "x");
    fs::last_write_time(d / "readings" / (std::to_string(i) + ".wav"), fs::file_time_type::clock::now() - std::chrono::hours(10 - i));
  }
  prune_readings(d, 3);
  CHECK(!fs::exists(d / "readings/0.wav"));
  CHECK(!fs::exists(d / "readings/1.wav"));
  CHECK(fs::exists(d / "readings/4.wav"));
  wipe(d);
}

TEST_CASE("read aloud: Settings → Read aloud's rows and their changes, kept in settings.json") {
  Settings s;
  SettingsState st;
  st.speech_built = true;
  auto p = settings_view(SettingsSection::speech, 0, s, st);
  CHECK(p.tabs.empty());
  std::vector<std::string> keys;
  for (auto &r : p.rows) keys.push_back(r.key);
  CHECK(keys == std::vector<std::string>{"speech_on", "speech_model", "speech_gpu", "speech_keep", "speech_voice", "", "speech_add"});
  CHECK(!p.rows[0].on);  // off by default
  CHECK(p.rows[1].button == "Download");
  CHECK(p.rows[1].detail.find("about 3.5 GB of memory") != std::string::npos);
  st.speech_installed = true;
  st.voices = {{"beef", "de", "Wir stellen die Kisten.", "own"}, {"f00d", "en", "Mine.", "own"}};
  p = settings_view(SettingsSection::speech, 0, s, st);
  keys.clear();
  for (auto &r : p.rows) keys.push_back(r.key);
  CHECK(keys == std::vector<std::string>{"speech_on", "speech_model", "speech_remove", "speech_gpu", "speech_keep", "speech_voice", "",
                                         "voice.beef", "voice_remove.beef", "voice.f00d", "voice_remove.f00d", "speech_add"});
  CHECK(p.rows[1].button.empty());
  CHECK(p.rows[7].title == "German · “Wir stellen die Kisten.”");
  st.downloading = "speech", st.progress = 0.5;
  CHECK(settings_view(SettingsSection::speech, 0, s, st).rows[1].progress == 0.5);
  settings_toggle(s, "speech_on", true);
  settings_toggle(s, "speech_gpu", false);
  settings_toggle(s, "voice.f00d", false);
  settings_choose(s, "speech_voice", 1, st);
  settings_choose(s, "speech_keep", 0, st);
  CHECK(s.speech.on);
  CHECK(!s.speech.gpu);
  CHECK(s.speech.male);
  CHECK(s.speech.keep == 1);
  CHECK(s.speech.off == std::vector<std::string>{"f00d"});
  auto d = temp("lexipeek-speech-settings");
  CHECK(save_settings(d, s).empty());
  auto l = load_settings(d);
  CHECK(l.speech.on);
  CHECK(!l.speech.gpu);
  CHECK(l.speech.male);
  CHECK(l.speech.keep == 1);
  CHECK(l.speech.off == s.speech.off);
  CHECK(load_settings(temp("lexipeek-speech-none")).speech.keep == 3);
  wipe(d);
}

TEST_CASE("read aloud: the card's session presses ▶, says why it cannot read, a second press stops") {
  auto d = temp("lexipeek-speech-session");
  Settings st;
  st.speech.on = true;
  save_settings(d, st);
  auto e = app::Engine::make(d.string(), app::device(16384, {}, "arm64"));
  auto s = app::CardSession::make(e);
  s.look("Haus", Source::input);
  for (int i = 0; i < 500 && s.busy(); i++) s.poll(), std::this_thread::sleep_for(std::chrono::milliseconds(2));
  s.poll();
  // No pack: the text's language is unknown, nothing to read.
  CHECK(s.view().read.text.empty());
  s.read();
  CHECK(!s.reading());
  wipe(d);
}
