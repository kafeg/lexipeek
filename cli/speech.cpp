// say, voices, tts: Read aloud (M9) from the command line — readings, voices, the model, timings.
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include "command.hpp"
#include "lexipeek/speech.hpp"
#include "memory.hpp"

namespace lexipeek::cli {

namespace {

namespace fs = std::filesystem;
using namespace speech;

SpeechSettings settings(const Args &a) {
  SpeechSettings s;
  s.on = true;
  s.male = a.opt("--voice") == "male";
  s.gpu = !a.has("--cpu");
  return s;
}

double ms_since(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

// One reading to its end: its state, how long it took, how long it loaded.
Reading said(Speaker &sp, const Read &r, double &ms, double &load_ms) {
  auto t = std::chrono::steady_clock::now();
  load_ms = 0;
  sp.read(r);
  for (;;) {
    auto st = sp.state();
    if (st.phase == Phase::loading) load_ms = ms_since(t);
    if (st.phase == Phase::ready || st.phase == Phase::failed || st.phase == Phase::idle) return ms = ms_since(t), st;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

double seconds(const std::string &wav) {
  std::ifstream f(wav, std::ios::binary);
  std::string b{std::istreambuf_iterator<char>(f), {}};
  return static_cast<double>(read_wav(b).samples.size()) / 24000.0;
}

const Register say{{"say", "[--lang de] [--voice female|male] [--cpu] [--out FILE] TEXT...   (a reading: its WAV and timings)",
                    {"--cpu"}, [](const Args &a) {
                      Speaker sp(a.data);
                      sp.configure(settings(a));
                      double ms, load;
                      auto st = said(sp, {a.text(), a.opt("--lang", "de")}, ms, load);
                      if (st.phase != Phase::ready) return std::cerr << "error: " << st.error << "\n", 1;
                      if (a.has("--out")) fs::copy_file(st.wav, a.opt("--out"), fs::copy_options::overwrite_existing);
                      std::cout << std::format("{}\t{:.0f} ms\t{:.2f} s of audio\t{} MB peak\n", st.wav, ms, seconds(st.wav), memory().peak_mb);
                      return 0;
                    }}};

// Each word and sentence twice: first made, then kept. One per line from stdin.
const Register say_bench{{"say-bench", "[--lang de] [--voice female|male] [--cpu] (lines from stdin: load, each reading, its audio)",
                          {"--cpu"}, [](const Args &a) {
                            Speaker sp(a.data);
                            sp.configure(settings(a));
                            std::string line;
                            std::cout << "| text | made ms | audio s | kept ms |\n|---|---|---|---|\n";
                            std::istringstream in(a.text());
                            while (std::getline(in, line)) {
                              if (line.empty()) continue;
                              double ms, load, again, l2;
                              auto st = said(sp, {line, a.opt("--lang", "de")}, ms, load);
                              if (st.phase != Phase::ready) return std::cerr << "error: " << st.error << "\n", 1;
                              said(sp, {line, a.opt("--lang", "de")}, again, l2);
                              if (load > 0) std::cout << std::format("| (load) | {:.0f} | | |\n", load);
                              std::cout << std::format("| {} | {:.0f} | {:.2f} | {:.0f} |\t{}\n", line, ms - load, seconds(st.wav), again, st.wav);
                            }
                            std::cout << std::format("peak {} MB\n", memory().peak_mb);
                            return 0;
                          }}};

const Register voices{{"voices", "[add WAV --lang de TEXT... | remove ID]   (the voices: the app's and own)", {},
                       [](const Args &a) {
                         if (!a.rest.empty() && a.rest[0] == "add" && a.rest.size() >= 3) {
                           Speaker sp(a.data);
                           sp.configure(settings(a));
                           std::string said;
                           for (size_t i = 2; i < a.rest.size(); i++) said += (said.empty() ? "" : " ") + a.rest[i];
                           auto e = sp.add_voice(a.rest[1], said, a.opt("--lang", "de"));
                           if (!e.empty()) return std::cerr << "error: " << e << "\n", 1;
                         } else if (!a.rest.empty() && a.rest[0] == "remove" && a.rest.size() == 2) {
                           Speaker(a.data).remove_voice(a.rest[1]);
                         }
                         for (auto &v : own_voices(a.data)) std::cout << std::format("{}\t{}\t{}\t{} frames\t{}\n", v.id, v.lang, v.source, v.frames, v.text);
                         for (auto &v : app_voices()) std::cout << std::format("{}\t{}\t{}\t{} frames\t{}\n", v.id, v.lang, v.source, v.frames, v.text);
                         return 0;
                       }}};

const Register tts{{"tts", "status | download | remove   (the voice model: Qwen3-TTS Base 1.7B)", {}, [](const Args &a) {
                      auto cmd = a.rest.empty() ? "status" : a.rest[0];
                      if (cmd == "download") {
                        auto st = download_model(a.data, [](std::int64_t d, std::int64_t t) { std::cerr << std::format("\r{} / {} MB", d >> 20, t >> 20); });
                        std::cerr << "\n";
                        if (!st.ok()) return std::cerr << "error: " << st.error << "\n", 1;
                      } else if (cmd == "remove") {
                        remove_model(a.data);
                      }
                      std::cout << (model_installed(a.data) ? "installed" : "not installed") << " (" << (built() ? "built" : "not built")
                                << ") " << (model_bytes() >> 20) << " MB in " << model_dir(a.data).string() << "\n";
                      return 0;
                    }}};

}  // namespace

}  // namespace lexipeek::cli
