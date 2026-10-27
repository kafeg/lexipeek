// ai: one AI request about a text with the data dir's AI settings (the key from LEXIPEEK_AI_KEY), or the model list.
#include <cstdlib>
#include <iostream>
#include "command.hpp"
#include "lexipeek/app.hpp"

namespace lexipeek::cli {

namespace {

const Register ai_cmd{{"ai", "quick|analysis|context TEXT... | models   (Settings → AI of DIR; the key from LEXIPEEK_AI_KEY)", {}, [](const Args &a) {
                         if (a.rest.empty()) return std::cerr << "ai: quick, analysis, context or models\n", 2;
                         auto *k = std::getenv("LEXIPEEK_AI_KEY");
                         std::string key = k ? k : "";
                         auto data = a.data.string();
                         auto &what = a.rest[0];
                         if (what == "models") {
                           auto m = app::ai_models(data, key);
                           if (!m.error.empty()) return std::cerr << m.error << "\n", 1;
                           for (auto &t : app::model_titles(m.list)) std::cout << t << "\n";
                           return 0;
                         }
                         auto ask = what == "quick" ? ai::Ask::quick : what == "analysis" ? ai::Ask::analysis : what == "context" ? ai::Ask::context
                                                                                                                              : static_cast<ai::Ask>(-1);
                         if (static_cast<int>(ask) < 0 || a.rest.size() < 2) return std::cerr << "ai: quick|analysis|context TEXT...\n", 2;
                         std::string text;
                         for (size_t i = 1; i < a.rest.size(); i++) text += (i > 1 ? " " : "") + a.rest[i];
                         auto engine = app::Engine::make(data, {});
                         auto card = engine.lookup(text, Source::cli);
                         auto st = engine.ask(card, ask, key);
                         if (!st.ok) return std::cerr << st.error << "\n", 1;
                         std::cout << ai::render(ask, st.json, card.kind == Card::Kind::word) << "\n";
                         return 0;
                       }}};

}  // namespace

}  // namespace lexipeek::cli
