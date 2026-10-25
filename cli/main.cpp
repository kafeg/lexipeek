// lexipeek-cli: the core from a terminal (translate, langid, bench).
#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include "lexipeek/mt.hpp"
#include "lexipeek/packs.hpp"
#include "memory.hpp"

using namespace lexipeek;

namespace {

constexpr const char *USAGE = R"(Usage: lexipeek-cli [--data DIR] [--idle-ms N] <command> [options]
  translate --from de --to ru [--method fast|direct|llm] [--english] [TEXT...]   (no TEXT: stdin)
  langid [-k 3] [TEXT...]
  bench [--from de --to ru] [--method M] [--text TEXT]   (no --method: each method in its own process)
DIR: the data dir with the packs (default: the platform's, or LEXIPEEK_DATA).
)";

const char *SENTENCE = "Das Haus am Ende der Straße ist alt, aber sehr schön.";
const char *PARAGRAPH =
    "Am nächsten Morgen stand er früh auf und ging zum Bahnhof. Der Zug war voll, und er musste die ganze Fahrt "
    "stehen. In der Stadt angekommen, suchte er das kleine Café, von dem ihm seine Schwester erzählt hatte.";

struct Args {
  std::string command;
  std::map<std::string, std::string> opts;
  std::vector<std::string> rest;
  std::string opt(const std::string &k, const std::string &def = "") const {
    auto i = opts.find(k);
    return i == opts.end() ? def : i->second;
  }
};

Args parse(int argc, char **argv) {
  Args a;
  for (int i = 1; i < argc; i++) {
    std::string s = argv[i];
    if (s == "--english") a.opts[s] = "1";
    else if (s.starts_with("-") && s.size() > 1 && i + 1 < argc) a.opts[s] = argv[++i];
    else if (a.command.empty()) a.command = s;
    else a.rest.push_back(s);
  }
  return a;
}

std::string text_of(const Args &a) {
  std::string t;
  for (auto &r : a.rest) t += (t.empty() ? "" : " ") + r;
  if (t.empty()) t.assign(std::istreambuf_iterator<char>(std::cin), {});
  return t;
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
  auto loaded = cli::memory().now_mb;
  t = clock::now();
  auto first = tr.translate(from, to, SENTENCE, m);
  double first_ms = ms_since(t);
  t = clock::now();
  auto next = tr.translate(from, to, text, m);
  double next_ms = ms_since(t);
  auto working = cli::memory().now_mb;
  t = clock::now();
  tr.release();
  double unload = ms_since(t);
  auto after = cli::memory().now_mb;
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
                           cli::memory().peak_mb);
  std::cerr << method_name(m) << ": " << first.text << "\n" << method_name(m) << ": " << next.text << "\n";
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  auto a = parse(argc, argv);
  TranslatorOptions o;
  o.data = a.opts.contains("--data") ? std::filesystem::path(a.opt("--data")) : default_data_dir();
  if (a.opts.contains("--idle-ms")) o.idle_bergamot = o.idle_llm = std::chrono::milliseconds(std::stoll(a.opt("--idle-ms")));
  auto from = a.opt("--from", "de"), to = a.opt("--to", "ru");

  if (a.command == "translate") {
    auto m = method_from(a.opt("--method", "fast"));
    if (!m) return std::cerr << USAGE, 2;
    Translator tr(o);
    auto r = tr.translate(from, to, text_of(a), *m);
    if (!r.ok()) return std::cerr << "error: " << r.error << "\n", 1;
    if (a.opts.contains("--english") && !r.english.empty()) std::cout << r.english << "\n";
    std::cout << r.text << "\n";
    return 0;
  }
  if (a.command == "langid") {
    Translator tr(o);
    auto ls = tr.langid(text_of(a), std::stoi(a.opt("-k", "3")));
    if (ls.empty()) return std::cerr << "error: no language (is langid.ftz in " << o.data.string() << "?)\n", 1;
    for (auto &l : ls) std::cout << std::format("{} {:.3f}\n", l.code, l.p);
    return 0;
  }
  if (a.command == "bench") {
    auto text = a.opt("--text", PARAGRAPH);
    if (!a.opts.contains("--method")) {
      std::cout << "| method | load ms | first ms | next ms | unload ms | reload ms | first after reload ms | MB loaded | MB working | MB unloaded | peak RSS MB |\n"
                   "|---|---|---|---|---|---|---|---|---|---|---|\n" << std::flush;
      int failed = 0;
      for (auto m : {Method::fast, Method::direct, Method::llm}) {
        std::string cmd = std::format("\"{}\" --data \"{}\" bench --from {} --to {} --method {}", argv[0], o.data.string(), from, to, method_name(m));
#if defined(_WIN32)
        cmd = "\"" + cmd + "\"";
#endif
        failed += std::system(cmd.c_str()) != 0;
      }
      return failed ? 1 : 0;
    }
    auto m = method_from(a.opt("--method"));
    if (!m) return std::cerr << USAGE, 2;
    Translator tr(o);
    return bench_one(tr, from, to, *m, text);
  }
  std::cerr << USAGE;
  return a.command.empty() || a.command == "help" ? 0 : 2;
}
