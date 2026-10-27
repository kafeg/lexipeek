// Offline translation and language id: the three methods of InkPeek, one heavy model in memory.
#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lexipeek {

enum class Method { fast, direct, llm };  // Mozilla through English, OPUS-MT, Hy-MT2
std::string_view method_name(Method m);
std::optional<Method> method_from(std::string_view s);

struct Translation {
  std::string text;
  std::string english;  // the step in between (fast, neither side English)
  std::string by;       // "Mozilla", "OPUS-MT" or the model's name
  std::string error;
  std::int64_t ms = 0;  // how long it took
  bool kept = false;    // an answer kept from before (no model ran)
  bool ok() const { return error.empty(); }
};

struct Language {
  std::string code;
  float p = 0;
};

struct TranslatorOptions {
  std::filesystem::path data;
  std::chrono::milliseconds idle_bergamot{std::chrono::minutes(5)};
  std::chrono::milliseconds idle_llm{std::chrono::minutes(2)};
  int threads = 0;  // llama.cpp CPU threads; 0: half the cores, at most 4
};

class Translator {
 public:
  explicit Translator(TranslatorOptions o);
  ~Translator();
  Translator(const Translator &) = delete;
  Translator &operator=(const Translator &) = delete;

  // Blocking.
  Translation translate(const std::string &from, const std::string &to, const std::string &text, Method m);
  // Why the method cannot translate from → to here ("" = it can): a missing model, a missing pack.
  std::string why_not(const std::string &from, const std::string &to, Method m);
  // Loads the method's model for from → to without translating; "" or the error.
  std::string load(const std::string &from, const std::string &to, Method m);
  std::vector<Language> langid(const std::string &text, int k = 3);
  // Stops a running AI translation (it returns the error "Cancelled"); the next translate starts afresh.
  void cancel();
  // Frees the held model (idle, memory pressure).
  void release();
  // What is held: "" none, else the key ("fast:de-ru", "direct:<id>", "llm:<id>").
  std::string held() const;
  // How long a model stays after a use from now on (0: until release()).
  void set_idle(std::chrono::milliseconds idle);
  // The held model for the menu: its name ("OPUS-MT de → ru"), its files' size, seconds left (-1: kept).
  struct Memory {
    std::string key, title;
    std::int64_t bytes = 0;
    long long seconds = 0;
  };
  Memory memory() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lexipeek
