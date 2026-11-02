// Tests on real models from InkPeek's packs: run when LEXIPEEK_MODELS names a data dir that has them, else skipped.
#include <doctest/doctest.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>
#include "temp.hpp"
#include "lexipeek/mt.hpp"
#include "lexipeek/session.hpp"

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

TEST_CASE("direct: a long sentence and a lower-case one keep the target language to the end") {
  if (!has("opus/opus-deu_eng_fra_por_spa-sla")) return;
  Translator tr({models()});
  // Past Marian's 128 tokens its own cut lost the tag: the rest came out in another language.
  auto r = tr.translate("de", "ru",
                        "gestern habe ich das Programm geöffnet und versucht, einen langen Text zu übersetzen, zuerst funktionierte alles "
                        "gut, aber dann wurde die Karte langsam, und ich verstand nicht, warum das so ist, deshalb beschloss ich, die "
                        "Einstellungen zu prüfen, wo es mehrere Reiter gibt, und jeder ist für seinen Teil des Programms zuständig, dann "
                        "schloss ich das Fenster, öffnete es wieder, wählte eine andere Sprache, stellte die alte wieder ein, startete den "
                        "Rechner neu, wartete eine Minute, öffnete das Programm wieder und versuchte noch einmal, denselben langen Text zu "
                        "übersetzen, aber das Ergebnis war genauso seltsam wie vorher, und dann schrieb ich dem Entwickler einen Brief mit "
                        "einer genauen Beschreibung von allem, was an diesem langen und ermüdenden Tag passiert war. das war alles.",
                        Method::direct);
  INFO(r.text);
  REQUIRE(r.ok());
  CHECK(std::none_of(r.text.begin(), r.text.end(), [](char ch) { return std::isalpha(static_cast<unsigned char>(ch)); }));
  CHECK(!r.text.starts_with(","));
}

TEST_CASE("fast and direct: a passage's lines, none starting with the \", \" a model may put first") {
  const char *text = "Erste Zeile.\nZweite Zeile, etwas länger als die erste.\n\nEin neuer Absatz nach einer Leerzeile.";
  for (auto [m, needs] : {std::pair{Method::fast, "models/enru/set"}, {Method::direct, "opus/opus-deu_eng_fra_por_spa-sla"}}) {
    if (!has(needs) || (m == Method::fast && !has("models/deen/set"))) continue;
    Translator tr({models()});
    auto r = tr.translate("de", "ru", text, m);
    INFO(r.text);
    REQUIRE(r.ok());
    CHECK(std::count(r.text.begin(), r.text.end(), '\n') == 3);
    for (size_t at = 0; at != std::string::npos; at = r.text.find('\n', at + 1)) {
      auto line = r.text.substr(at == 0 ? 0 : at + 1, 2);
      CHECK(!line.starts_with(","));
      CHECK(!line.starts_with(";"));
    }
  }
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
    CHECK(until([&] { return tr.held().empty(); }));
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

TEST_CASE("card session: the last method picked wins, a closed card's translation stops, ← keeps the card's own") {
  if (!has("models/deen/set") || !has("opus/opus-deu_eng_fra_por_spa-sla") || !has("llm/llm-hy-mt2-1.8b-q4.gguf")) return;
  auto e = app::Engine::make(packs_of(models()).string(), app::device(16384, {"asimddp"}, "arm64"));
  auto s = app::CardSession::make(e);
  auto settle = [&](int ms) {
    for (int i = 0; i < ms / 10 && s.busy(); i++) s.poll(), std::this_thread::sleep_for(std::chrono::milliseconds(10));
    s.poll();
  };
  s.look("Der Hund schläft im Garten. Die Kinder spielen draußen, bis es dunkel wird.", Source::input, "de", "ru");
  settle(30000);
  REQUIRE(s.has_card());
  s.use(Method::llm);
  s.use(Method::fast);
  settle(30000);
  auto st = s.nav().state;
  CHECK(st.method == Method::fast);
  CHECK(st.translated);
  CHECK(!st.translating);
  CHECK(cyrillic(st.translation.text));
  // A long text on the AI model, the card closed meanwhile: nothing waits, the next card translates.
  std::string text;
  for (int i = 0; i < 8; i++) text += "Am nächsten Morgen stand er früh auf und ging zum Bahnhof. Der Zug war voll. ";
  s.look(text, Source::input, "de", "ru");
  settle(30000);
  s.use(Method::llm);
  s.poll();
  CHECK(s.busy());
  auto t0 = std::chrono::steady_clock::now();
  s.cancel();
  settle(5000);
  CHECK(!s.busy());
  CHECK(!s.nav().state.translating);
  s.look("Das Haus ist alt.", Source::input, "de", "ru");
  s.use(Method::fast);
  settle(30000);
  CHECK(s.nav().state.translated);
  CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
}
