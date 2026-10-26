// Tests on real models from InkPeek's packs: run when LEXIPEEK_MODELS names a data dir that has them, else skipped.
#include <doctest/doctest.h>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>
#include "lexipeek/mt.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

namespace {

fs::path models() {
  const char *v = std::getenv("LEXIPEEK_MODELS");  // NOLINT(concurrency-mt-unsafe)
  return v && *v ? fs::path(v) : fs::path();
}

bool has(const fs::path &p) {
  if (models().empty() || !fs::exists(models() / p)) {
    MESSAGE("skipped: no " << p.string() << " in LEXIPEEK_MODELS");
    return false;
  }
  return true;
}

bool cyrillic(const std::string &s) { return s.find("\xD0") != std::string::npos || s.find("\xD1") != std::string::npos; }

double ms(const std::function<void()> &f) {
  auto t = std::chrono::steady_clock::now();
  f();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

}  // namespace

TEST_CASE("langid") {
  if (!has("langid.ftz")) return;
  Translator tr({models()});
  auto l = tr.langid("Das Haus am Ende der Straße ist alt.");
  REQUIRE(!l.empty());
  CHECK(l[0].code == "de");
  CHECK(l[0].p > 0.5f);
  CHECK(tr.langid("Дом в конце улицы старый.")[0].code == "ru");
}

TEST_CASE("fast: Mozilla de → en") {
  if (!has("models/deen/set")) return;
  Translator tr({models()});
  auto r = tr.translate("de", "en", "Das Haus ist groß.", Method::fast);
  INFO(r.error);
  REQUIRE(r.ok());
  CHECK(r.text.find("house") != std::string::npos);
  CHECK(tr.held() == "fast:de-en");
  tr.release();
  CHECK(tr.held().empty());
}

TEST_CASE("fast: Mozilla de → ru through English") {
  if (!has("models/deen/set") || !has("models/enru/set")) return;
  Translator tr({models()});
  auto r = tr.translate("de", "ru", "Das Haus ist groß.", Method::fast);
  INFO(r.error);
  REQUIRE(r.ok());
  CHECK(r.english.find("house") != std::string::npos);
  CHECK(cyrillic(r.text));
}

TEST_CASE("direct: OPUS-MT int8 de → ru, the target tag per sentence") {
  if (!has("opus/opus-deu_eng_fra_por_spa-sla")) return;
  Translator tr({models()});
  auto r = tr.translate("de", "ru", "Das Haus ist groß. Der Garten ist klein.", Method::direct);
  INFO(r.error);
  REQUIRE(r.ok());
  CHECK(r.by == "OPUS-MT");
  CHECK(cyrillic(r.text));
  CHECK(r.text.find(">>") == std::string::npos);
}

TEST_CASE("llm: Hy-MT2 de → ru") {
  if (!has("llm/llm-hy-mt2-1.8b-q4.gguf")) return;
  Translator tr({models()});
  auto r = tr.translate("de", "ru", "Das Haus ist groß.", Method::llm);
  INFO(r.error);
  REQUIRE(r.ok());
  CHECK(cyrillic(r.text));
  CHECK(r.by == "Hy-MT2 1.8B");
}

TEST_CASE("idle unload and the reload budget") {
  using namespace std::chrono_literals;
  // Budgets: about 7× the M0 bench on Apple Silicon (docs/spec.md → Models in memory), room for slower machines.
  struct Case {
    const char *needs;
    Method m;
    double budget_ms;
  };
  for (auto c : {Case{"models/deen/set", Method::fast, 700}, Case{"opus/opus-deu_eng_fra_por_spa-sla", Method::direct, 1000},
                 Case{"llm/llm-hy-mt2-1.8b-q4.gguf", Method::llm, 1000}}) {
    if (!has(c.needs)) continue;
    CAPTURE(method_name(c.m));
    TranslatorOptions o{models()};
    o.idle_bergamot = o.idle_llm = 100ms;
    Translator tr(o);
    auto to = c.m == Method::fast ? "en" : "ru";
    REQUIRE(tr.load("de", to, c.m).empty());
    for (int i = 0; i < 100 && !tr.held().empty(); i++) std::this_thread::sleep_for(10ms);
    CHECK(tr.held().empty());
    CHECK(ms([&] { tr.load("de", to, c.m); }) < c.budget_ms);
  }
}

TEST_CASE("llm: a newer request cancels a running translation") {
  if (!has("llm/llm-hy-mt2-1.8b-q4.gguf")) return;
  Translator tr({models()});
  REQUIRE(tr.load("de", "ru", Method::llm).empty());
  std::string text;
  for (int i = 0; i < 6; i++) text += "Am nächsten Morgen stand er früh auf und ging zum Bahnhof. Der Zug war voll. ";
  std::thread canceller([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    tr.cancel();
  });
  auto t0 = std::chrono::steady_clock::now();
  auto r = tr.translate("de", "ru", text, Method::llm);
  canceller.join();
  CHECK(r.error == "Cancelled");
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
  CHECK(tr.translate("de", "ru", "Das Haus ist alt.", Method::llm).ok());  // the next one runs
}
