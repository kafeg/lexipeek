// translate, langid, bench.
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include "command.hpp"
#include "lexipeek/mt.hpp"
#include "memory.hpp"

namespace lexipeek::cli {

namespace {

const char *SENTENCE = "Das Haus am Ende der Straße ist alt, aber sehr schön.";
const char *PARAGRAPH =
    "Am nächsten Morgen stand er früh auf und ging zum Bahnhof. Der Zug war voll, und er musste die ganze Fahrt "
    "stehen. In der Stadt angekommen, suchte er das kleine Café, von dem ihm seine Schwester erzählt hatte.";

TranslatorOptions options(const Args &a) {
  TranslatorOptions o;
  o.data = a.data;
  if (a.has("--idle-ms")) o.idle_bergamot = o.idle_llm = std::chrono::milliseconds(std::stoll(a.opt("--idle-ms")));
  return o;
}

double ms_since(std::chrono::steady_clock::time_point t) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

int bench_one(Translator &tr, const std::string &from, const std::string &to, Method m, const std::string &text) {
  using clock = std::chrono::steady_clock;
  auto t = clock::now();
  if (auto e = tr.load(from, to, m); !e.empty()) {
    std::cout << std::format("| {} | {} |\n", method_name(m), e);
    return 1;
  }
  double load = ms_since(t);
  auto loaded = memory().now_mb;
  t = clock::now();
  auto first = tr.translate(from, to, SENTENCE, m);
  double first_ms = ms_since(t);
  t = clock::now();
  auto next = tr.translate(from, to, text, m);
  double next_ms = ms_since(t);
  auto working = memory().now_mb;
  t = clock::now();
  tr.release();
  double unload = ms_since(t);
  auto after = memory().now_mb;
  t = clock::now();
  tr.load(from, to, m);
  double reload = ms_since(t);
  t = clock::now();
  tr.translate(from, to, SENTENCE, m);
  double again = ms_since(t);
  if (!first.ok() || !next.ok()) {
    std::cout << std::format("| {} | {} |\n", method_name(m), first.ok() ? next.error : first.error);
    return 1;
  }
  std::cout << std::format("| {} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.0f} | {:.0f} |\n",
                           method_name(m), load, first_ms, next_ms, unload, reload, again, loaded, working, after,
                           memory().peak_mb);
  std::cerr << method_name(m) << ": " << first.text << "\n" << method_name(m) << ": " << next.text << "\n";
  return 0;
}

const Register translate{{"translate", "--from de --to ru [--method fast|direct|llm] [--english] [TEXT...]   (no TEXT: stdin)",
                          {"--english"}, [](const Args &a) {
                            auto m = method_from(a.opt("--method", "fast"));
                            if (!m) return std::cerr << "unknown method\n", 2;
                            Translator tr(options(a));
                            auto r = tr.translate(a.opt("--from", "de"), a.opt("--to", "ru"), a.text(), *m);
                            if (!r.ok()) return std::cerr << "error: " << r.error << "\n", 1;
                            if (a.has("--english") && !r.english.empty()) std::cout << r.english << "\n";
                            std::cout << r.text << "\n";
                            return 0;
                          }}};

const Register langid{{"langid", "[-k 3] [TEXT...]", {}, [](const Args &a) {
                         Translator tr(options(a));
                         auto ls = tr.langid(a.text(), std::stoi(a.opt("-k", "3")));
                         if (ls.empty()) return std::cerr << "error: no language (is langid.ftz in " << a.data.string() << "?)\n", 1;
                         for (auto &l : ls) std::cout << std::format("{} {:.3f}\n", l.code, l.p);
                         return 0;
                       }}};

const Register bench{{"bench", "[--from de --to ru] [--method M] [--text TEXT]   (no --method: each method in its own process)", {},
                      [](const Args &a) {
                        auto from = a.opt("--from", "de"), to = a.opt("--to", "ru");
                        if (!a.has("--method")) {
                          std::cout << "| method | load ms | first ms | next ms | unload ms | reload ms | first after reload ms | MB loaded | MB working | MB unloaded | peak RSS MB |\n"
                                       "|---|---|---|---|---|---|---|---|---|---|---|\n" << std::flush;
                          int failed = 0;
                          for (auto m : {Method::fast, Method::direct, Method::llm}) {
                            auto cmd = std::format("\"{}\" --data \"{}\" bench --from {} --to {} --method {}", a.exe, a.data.string(), from, to, method_name(m));
#if defined(_WIN32)
                            cmd = "\"" + cmd + "\"";
#endif
                            failed += std::system(cmd.c_str()) != 0;
                          }
                          return failed ? 1 : 0;
                        }
                        auto m = method_from(a.opt("--method"));
                        if (!m) return std::cerr << "unknown method\n", 2;
                        Translator tr(options(a));
                        return bench_one(tr, from, to, *m, a.opt("--text", PARAGRAPH));
                      }}};

}  // namespace

}  // namespace lexipeek::cli
