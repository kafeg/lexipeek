// Core: request → card (as InkPeek's CardActivity, DictionaryPage, LangId.source), then its translation.
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include "dict/library.hpp"
#include "lexipeek/core.hpp"
#include "mt/text.hpp"
#include "text/text.hpp"

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

size_t word_count(const std::string &s) {
  static const text::Regex words("\\S+");
  return words.all(s).size();
}

}  // namespace

struct Core::Impl {
  CoreOptions o;
  std::unique_ptr<Translator> tr;
  std::unique_ptr<dict::Library> lib;
  std::mutex mu;  // the library is not thread-safe: one lookup at a time

  // The text's language among the packs' book languages into answer (InkPeek's LangId.source): (lang, other).
  std::pair<std::string, std::string> source(const std::string &txt, const std::string &answer) {
    std::vector<std::string> books;
    for (auto *d : lib->all())
      if (d->answer() == answer && !d->book().empty() && std::find(books.begin(), books.end(), d->book()) == books.end())
        books.push_back(d->book());
    if (books.empty()) return {"", ""};
    auto guess = tr->langid(txt, 5);
    std::optional<Language> top;
    if (!guess.empty()) top = guess.front();
    // When fastText is unsure (a word or two): how many words each book language's dictionary knows, forms included.
    static const text::Regex word("\\p{L}[\\p{L}'’-]*");
    std::vector<std::string> words;
    for (auto &m : word.all(txt))
      if (std::find(words.begin(), words.end(), m.groups[0]) == words.end() && words.size() < 40) words.push_back(m.groups[0]);
    std::map<std::string, size_t> known;
    for (auto &b : books)
      for (auto &w : words) known[b] += lib->knows(b, answer, w);
    std::optional<std::pair<std::string, size_t>> best;
    for (auto &b : books)
      if (!best || known[b] > best->second) best = std::make_pair(b, known[b]);
    auto in = [&](const std::string &x) { return std::find(books.begin(), books.end(), x) != books.end(); };
    if (top && top->p >= 0.5f && in(top->code)) return {top->code, ""};
    // fastText is sure it is a language without a pack: said so, unless a book dictionary knows nearly every word.
    if (top && top->p >= 0.5f && (!best || static_cast<double>(best->second) < static_cast<double>(words.size()) * 0.8)) return {"", top->code};
    if (best && best->second > 0) return {best->first, ""};
    if (top && in(top->code)) return {top->code, ""};
    return {"", top ? top->code : ""};
  }
};

Core::Core(CoreOptions o) : impl_(std::make_unique<Impl>()) {
  o.translator.data = o.data;
  impl_->tr = std::make_unique<Translator>(o.translator);
  impl_->lib = std::make_unique<dict::Library>(o.data / "dicts", o.data / "index");
  impl_->o = std::move(o);
}

Core::~Core() = default;

Card Core::lookup(const Request &r) {
  std::lock_guard g(impl_->mu);
  Card c;
  c.answer = r.answer.empty() ? impl_->o.answer : r.answer;
  auto raw = mt::trim_space(r.text);
  if (raw.empty()) return c;
  c.kind = word_count(text::clean_query(raw)) == 1 ? Card::Kind::word : Card::Kind::phrase;
  c.text = c.kind == Card::Kind::word ? text::clean_query(raw) : text::clean_phrase(raw);
  if (c.text.empty()) return c.kind = Card::Kind::empty, c;
  auto &lib = *impl_->lib;
  try {
    std::tie(c.lang, c.other) = impl_->source(c.text, c.answer);
    if (c.kind == Card::Kind::word) {
      auto heads = lib.heads(c.text, c.answer);
      if (!heads.empty()) {
        c.header = heads.front().header();
        if (text::lower(heads.front().word) != text::lower(c.text)) c.base = heads.front().word;
        c.rank = lib.rank(heads.front().word, heads.front().book);
        if (c.lang.empty()) c.lang = heads.front().book;
      }
      for (auto &[d, hits] : lib.lookup(c.text, c.answer)) {
        auto label = d->book().empty() ? d->name() : "InkPeek · " + d->book() + " → " + d->answer();
        for (auto &h : hits) c.entries.push_back({label, dict::head_line(h.html).value_or(h.word), h.html});
      }
    } else {
      for (auto &gl : lib.gloss(c.text, c.answer))
        c.gloss.push_back({gl.word, gl.head ? gl.head->word : gl.word, gl.head ? gl.head->meaning() : ""});
      for (auto &[f, h] : lib.separable(c.text, c.answer))
        c.separable.push_back({f.form + " … " + f.particle, f.verb, h ? h->meaning() : ""});
    }
  } catch (const std::exception &e) {
    c.error = e.what();
  }
  if (c.lang.empty() || c.lang == c.answer) return c;
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
  if (card.lang == card.answer && !card.lang.empty()) return {card.text, "", "", ""};
  if (!method || card.lang.empty()) return {"", "", "", "No method can translate this text here"};
  return impl_->tr->translate(card.lang, card.answer, card.text, *method);
}

LanguageGuess Core::language(const std::string &text, const std::string &answer) {
  std::lock_guard g(impl_->mu);
  auto [lang, other] = impl_->source(text, answer);
  return {lang, other};
}

void Core::release() { impl_->tr->release(); }

void Core::prepare() {
  std::lock_guard g(impl_->mu);
  for (auto *d : impl_->lib->all()) {
    try {
      d->prepare();
    } catch (const std::exception &) {
    }
  }
  impl_->tr->langid("Haus", 1);
}

void Core::reload() {
  std::lock_guard g(impl_->mu);
  impl_->lib = std::make_unique<dict::Library>(impl_->o.data / "dicts", impl_->o.data / "index");
}

Translator &Core::translator() { return *impl_->tr; }

}  // namespace lexipeek
