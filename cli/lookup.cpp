// lookup: the card as the app shows it, in text.
#include <chrono>
#include <iostream>
#include "command.hpp"
#include "lexipeek/core.hpp"

namespace lexipeek::cli {

namespace {

const Register lookup{{"lookup", "[--to ru] [--translate] [--full] [--time] [--ram MB] TEXT...   (the card: header, dictionary, gloss, methods)",
                       {"--translate", "--full", "--time"}, [](const Args &a) {
                         CoreOptions o;
                         o.data = a.data;
                         o.answer = a.opt("--to", "ru");
                         if (a.has("--ram")) o.device = Device{std::stoi(a.opt("--ram")), {"asimddp"}};
                         Core core(o);
                         auto text = a.text();
                         auto c = core.lookup({text, Source::cli, o.answer});
                         if (a.has("--time")) {
                           auto t = std::chrono::steady_clock::now();
                           for (int i = 0; i < 10; i++) core.lookup({text, Source::cli, o.answer});
                           std::cout << "lookup: " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count() / 10 << " ms (warm)\n";
                         }
                         if (c.kind == Card::Kind::empty) return std::cerr << "nothing to look up\n", 1;
                         std::cout << "text: " << c.text << "\nlang: " << (c.lang.empty() ? "?" : c.lang)
                                   << (c.other.empty() ? "" : " (no pack: " + c.other + ")") << " → " << c.answer << "\n";
                         if (!c.header.empty()) std::cout << "header: " << c.header << "\n";
                         if (!c.base.empty()) std::cout << "base: " << c.base << "\n";
                         if (c.rank) std::cout << "rank: " << c.rank << "\n";
                         for (auto &e : c.entries) {
                           std::cout << "· " << e.line << "   [" << e.dictionary << "]\n";
                           if (a.has("--full")) std::cout << e.article << "\n";
                         }
                         for (auto &g : c.gloss) std::cout << "  " << g.word << (g.base != g.word ? " (" + g.base + ")" : "") << " — " << g.meaning << "\n";
                         for (auto &g : c.separable) std::cout << "  sep: " << g.word << " → " << g.base << " — " << g.meaning << "\n";
                         for (auto &m : c.methods)
                           std::cout << (c.method && *c.method == m.method ? "* " : "  ") << m.name << (m.available ? "" : " — " + m.reason) << "\n";
                         if (!c.error.empty()) std::cout << "error: " << c.error << "\n";
                         if (a.has("--translate")) {
                           auto t = core.translate(c);
                           std::cout << "translation: " << (t.ok() ? t.text : "error: " + t.error) << "\n";
                         }
                         return 0;
                       }}};

}  // namespace

}  // namespace lexipeek::cli
