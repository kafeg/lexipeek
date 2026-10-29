// lookup: the card as the app shows it, in text.
#include <chrono>
#include <format>
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
                         auto ms = [](std::chrono::steady_clock::time_point t) {
                           return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
                         };
                         auto t0 = std::chrono::steady_clock::now();
                         Core core(o);
                         auto open = ms(t0);
                         auto text = a.text();
                         auto t1 = std::chrono::steady_clock::now();
                         auto c = core.lookup({text, Source::cli, o.answer});
                         auto first = ms(t1);
                         if (a.has("--time")) {
                           auto t = std::chrono::steady_clock::now();
                           for (int i = 0; i < 10; i++) core.lookup({text, Source::cli, o.answer});
                           std::cout << std::format("lookup: open {:.0f} ms, first {:.0f} ms, warm {:.1f} ms\n", open, first, ms(t) / 10);
                         }
                         if (c.kind == Card::Kind::empty) return std::cerr << "nothing to look up\n", 1;
                         static const char *kinds[] = {"empty", "word", "phrase", "passage"};
                         std::cout << "text: " << c.text << "\nkind: " << kinds[static_cast<int>(c.kind)] << "\nlang: " << (c.lang.empty() ? "?" : c.lang)
                                   << (c.other.empty() ? "" : " (no pack: " + c.other + ")") << " → " << c.answer << "\n";
                         if (!c.header.empty()) std::cout << "header: " << c.header << "\n";
                         if (!c.base.empty()) std::cout << "base: " << c.base << "\n";
                         if (c.rank) std::cout << "rank: " << c.rank << "\n";
                         for (auto &e : c.entries) {
                           std::cout << "· " << e.line << "   [" << e.dictionary << "]\n";
                           if (a.has("--full")) std::cout << e.article << "\n";
                           auto &r = e.related;
                           if (!r.rel.empty() || !r.syn.empty() || !r.expr.empty() || !r.forms.empty())
                             std::cout << std::format("  related {}, synonyms {}, antonyms {}, expressions {}, forms {}\n", r.rel.size(), r.syn.size(),
                                                      r.ant.size(), r.expr.size(), r.forms.size());
                         }
                         for (auto &g : c.gloss) std::cout << "  " << g.word << (g.base != g.word ? " (" + g.base + ")" : "") << " — " << g.meaning << "\n";
                         for (auto &g : c.separable) std::cout << "  sep: " << g.word << " → " << g.base << " — " << g.meaning << "\n";
                         if (c.more) std::cout << "  … and " << c.more << " more words\n";
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
