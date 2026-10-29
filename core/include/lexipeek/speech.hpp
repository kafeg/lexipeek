// Read aloud (spec M9): Qwen3-TTS Base 1.7B through qwentts.cpp in a native speaker's voice per language, one reading
// at a time, each kept as a WAV so the same one is never made twice.
#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "lexipeek/packs.hpp"
#include "lexipeek/settings.hpp"

namespace lexipeek::speech {

// What ▶ reads: a text in its language (text "": nothing).
struct Read {
  std::string text, lang;
  bool operator==(const Read &) const = default;
};

// The model's files: Hugging Face's Serveurperso/Qwen3-TTS-GGUF at a pinned commit, checked by sha256, in <data>/tts/.
struct ModelFile {
  std::string name, url, sha256;
  std::int64_t bytes = 0;
};
const std::vector<ModelFile> &model_files();
std::int64_t model_bytes();
constexpr int model_memory_mb = 3500;  // what it holds while it reads (measured: M9 in the spec)
std::filesystem::path model_dir(const std::filesystem::path &data);
bool model_installed(const std::filesystem::path &data);
// Blocking: the missing files, resumed, checked by sha256; progress over both.
Status download_model(const std::filesystem::path &data, const Progress &progress = {}, const Cancel &cancel = {});
void remove_model(const std::filesystem::path &data);
// Whether this build has the engine (the library is built for macOS and Windows).
bool built();

// The languages with voices in the app (the packs' answer languages); the model's name for one ("de" → "german"),
// "auto" for one it does not name (pl).
const std::vector<std::string> &languages();
bool speaks(const std::string &lang);
std::string model_language(const std::string &lang);
// The most codec frames a reading of text may take: twice the time to say it slowly, 2 s more.
int max_tokens(const std::string &text);

// A voice: its speaker embedding, its clip's codec codes ([16, frames], row-major) and what the clip says.
struct Voice {
  std::string id = "";  // "de-female", an own voice's hash
  std::string lang = "";
  std::string text = "";
  std::string source = "";  // "app", "own"
  std::vector<float> spk = {};
  std::vector<std::int32_t> codes = {};
  int frames = 0;
};
constexpr int codebooks = 16, code_bits = 11;
// .rvq: codes packed code_bits each, LSB first, no header.
std::vector<std::int32_t> unpack_codes(const std::string &bytes);
std::string pack_codes(const std::vector<std::int32_t> &codes);
// The voices in the app; the person's own (from a recording).
std::vector<Voice> app_voices();
std::vector<Voice> own_voices(const std::filesystem::path &data);
// The voice a language is read in: its first own voice that is on, else the app's female or male one; none: id "".
Voice voice_for(const std::vector<Voice> &own, const SpeechSettings &s, const std::string &lang);

// WAV: 16/24/32-bit PCM or 32-bit float, any rate and channels → mono at 24 kHz; written as 16-bit mono.
struct Audio {
  std::vector<float> samples;
  int rate = 24000;
  std::string error;
};
Audio read_wav(const std::string &bytes);
std::string wav_bytes(const std::vector<float> &samples, int rate);

// A reading's file: <data>/readings/<sha256 of the model, voice, language and text>.wav; the newest 2,000 kept.
std::filesystem::path reading_file(const std::filesystem::path &data, const Voice &v, const Read &r);
void prune_readings(const std::filesystem::path &data, std::size_t keep = 2000);

// A reading's state, polled by the shells (through app::CardSession).
enum class Phase { idle, loading, making, ready, failed };
struct Reading {
  int ticket = 0;
  Phase phase = Phase::idle;
  Read read = {};
  std::string wav = "";    // ready: the file to play
  std::string error = "";  // failed: what to say
  bool fix = false;   // failed: Settings → Read aloud fixes it (off, no model)
};

// The engine: the model loaded on the first reading, freed after the idle time; one reading at a time, a newer one
// cancels the one being made.
class Speaker {
 public:
  explicit Speaker(std::filesystem::path data);
  ~Speaker();
  Speaker(const Speaker &) = delete;
  Speaker &operator=(const Speaker &) = delete;

  void configure(const SpeechSettings &s);
  int read(const Read &r);  // its ticket
  void stop();
  Reading state() const;  // the newest reading's
  // Blocking: a voice from a recording (a WAV and what it says) into <data>/voices/; "" or the error.
  std::string add_voice(const std::string &wav, const std::string &text, const std::string &lang);
  std::string remove_voice(const std::string &id);
  void release();
  bool held() const;
  std::chrono::steady_clock::time_point until() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lexipeek::speech
