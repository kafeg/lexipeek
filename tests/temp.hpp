// Shared by the tests: a temporary folder away, a wait for a condition.
#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

// On Windows a store still open keeps its files, so a failure is not the test's.
inline void wipe(const std::filesystem::path &p) {
  std::error_code ec;
  std::filesystem::remove_all(p, ec);
}

// Polls done() until it is true or the time is up: a loaded CI runner is slow, so no fixed sleep decides a test.
template <class F>
bool until(F done, int seconds = 30) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (!done()) {
    if (std::chrono::steady_clock::now() > end) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

// A data dir of the test's own over $LEXIPEEK_MODELS: links to its read-only packs only, so the store, settings and the
// index (kept between runs in the temp folder) are the test's — LEXIPEEK_MODELS may name the person's data dir.
inline std::filesystem::path packs_of(const std::filesystem::path &src) {
  namespace fs = std::filesystem;
  static int n = 0;
  auto d = fs::temp_directory_path() / ("lexipeek-packs-" + std::to_string(++n)), index = fs::temp_directory_path() / "lexipeek-packs-index";
  wipe(d);
  fs::create_directories(d);
  fs::create_directories(index);
  for (auto f : {"dicts", "installed.json", "langid.ftz", "llm", "models", "opus", "packs.json", "tts"})
    if (fs::is_directory(src / f)) fs::create_directory_symlink(src / f, d / f);
    else if (fs::exists(src / f)) fs::create_symlink(src / f, d / f);
  fs::create_directory_symlink(index, d / "index");
  return d;
}
