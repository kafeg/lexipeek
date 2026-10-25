// Core: request → card. M1 fills the dictionary, the gloss and the language by the dictionaries.
#include "lexipeek/core.hpp"
#include "mt/text.hpp"

namespace lexipeek {

namespace {

std::string method_title(Method m) {
  switch (m) {
    case Method::fast: return "Fast · Mozilla";
    case Method::direct: return "Direct · OPUS-MT";
    case Method::llm: return "AI model · Hy-MT2";
  }
  return "";
}

}  // namespace

struct Core::Impl {
  CoreOptions o;
  std::unique_ptr<Translator> tr;
};

Core::Core(CoreOptions o) : impl_(std::make_unique<Impl>()) {
  o.translator.data = o.data;
  impl_->tr = std::make_unique<Translator>(o.translator);
  impl_->o = std::move(o);
}

Core::~Core() = default;

Card Core::lookup(const Request &r) {
  Card c;
  c.text = mt::trim(r.text);
  c.answer = r.answer.empty() ? impl_->o.answer : r.answer;
  if (c.text.empty()) return c;
  c.kind = c.text.find_first_of(" \t\n") == std::string::npos ? Card::Kind::word : Card::Kind::phrase;
  auto langs = impl_->tr->langid(c.text, 3);
  if (!langs.empty() && langs[0].p >= 0.5f) c.lang = langs[0].code;
  if (c.lang.empty()) return c;
  for (auto m : {Method::fast, Method::direct, Method::llm}) {
    MethodState s{m, method_title(m), false, impl_->tr->why_not(c.lang, c.answer, m)};
    s.available = s.reason.empty();
    c.methods.push_back(s);
  }
  for (auto &s : c.methods)
    if (s.available && (!c.method || (impl_->o.method && s.method == *impl_->o.method))) c.method = s.method;
  return c;
}

Translation Core::translate(const Card &card, std::optional<Method> m) {
  auto method = m ? m : card.method;
  if (!method || card.lang.empty()) return {"", "", "", "No method can translate this text here"};
  return impl_->tr->translate(card.lang, card.answer, card.text, *method);
}

void Core::release() { impl_->tr->release(); }

Translator &Core::translator() { return *impl_->tr; }

}  // namespace lexipeek
