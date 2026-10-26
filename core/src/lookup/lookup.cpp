// Core: request → card (as InkPeek's CardActivity, DictionaryPage, LangId.source), then its translation.
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include "dict/library.hpp"
#include "lexipeek/core.hpp"
#include "lexipeek/views.hpp"
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

Card::Kind kind_of(const std::string &s) {
  static const text::Regex ends("[.!?…]\\s+\\p{Lu}|[。！？]\\s*\\S");
  auto words = word_count(s);
  if (words == 1) return Card::Kind::word;
  return words <= 8 && ends.all(s).empty() ? Card::Kind::phrase : Card::Kind::passage;
}

// Java's String.hashCode over UTF-16 units.
std::int32_t java_hash(const std::string &s) {
  std::uint32_t h = 0;
  for (char32_t cp : mt::utf32(s)) {
    if (cp < 0x10000) {
      h = h * 31 + cp;
    } else {
      cp -= 0x10000;
      h = h * 31 + (0xD800 + (cp >> 10));
      h = h * 31 + (0xDC00 + (cp & 0x3FF));
    }
  }
  return static_cast<std::int32_t>(h);
}

// InkPeek's names of the model kinds in its answers' model field.
const char *answer_model(Method m) { return m == Method::fast ? "bergamot" : m == Method::direct ? "opus" : "llm"; }

}  // namespace

std::int64_t answer_version(const Manifest &m, const std::map<std::string, std::string> &installed, const std::string &from,
                            const std::string &to, Method method) {
  auto sha = [&](const std::string &id) {
    auto i = installed.find(id);
    return i == installed.end() ? std::string() : i->second;
  };
  std::string key;
  if (method == Method::fast) {
    for (auto &[a, b] : mt::steps(from, to)) key += (key.empty() ? "" : ", ") + sha("model-" + a + "-" + b);
  } else if (auto c = covering(m, method == Method::direct ? "opus" : "llm", from, to)) {
    key = sha(c->id);
  }
  return java_hash(key);
}

struct Core::Impl {
  CoreOptions o;
  std::unique_ptr<Translator> tr;
  std::unique_ptr<dict::Library> lib;
  std::unique_ptr<Store> store;
  std::mutex mu;  // the library is not thread-safe: one lookup at a time
  std::mutex answer_mu;  // the answer language for Notes, never waiting for a lookup or prepare()
  std::string notes_answer;

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
  impl_->store = std::make_unique<Store>(StoreOptions{o.data, {}, {}});
  impl_->notes_answer = o.answer;
  impl_->o = std::move(o);
}

Core::~Core() = default;

void Core::configure(const Settings &s, const std::optional<Device> &device) {
  std::lock_guard g(impl_->mu);
  impl_->o.answer = s.answer;
  std::lock_guard a(impl_->answer_mu);
  impl_->notes_answer = s.answer;
  impl_->o.method = s.method;
  if (device) impl_->o.device = device;
}

Card Core::lookup(const Request &r) {
  std::lock_guard g(impl_->mu);
  Card c;
  c.source = r.source;
  c.answer = r.answer.empty() ? impl_->o.answer : r.answer;
  auto raw = mt::trim_space(r.text);
  if (raw.empty()) return c;
  c.kind = kind_of(raw);
  c.text = c.kind == Card::Kind::word ? text::clean_query(raw) : text::clean_phrase(raw);
  if (c.text.empty()) return c.kind = Card::Kind::empty, c;
  auto &lib = *impl_->lib;
  try {
    std::tie(c.lang, c.other) = impl_->source(c.text, c.answer);
    // A text in the answer language itself: no pack translates it, the card says so.
    if (c.lang.empty() && c.other == c.answer) c.lang = c.answer, c.other.clear();
    if (c.kind == Card::Kind::word) {
      auto heads = lib.heads(c.text, c.answer, c.lang);
      if (!heads.empty()) {
        c.header = heads.front().header();
        if (text::lower(heads.front().word) != text::lower(c.text)) c.base = heads.front().word;
        c.rank = lib.rank(heads.front().word, heads.front().book);
        c.save_text = heads.front().word, c.save_line = heads.front().line;
        if (c.lang.empty()) c.lang = heads.front().book;
      }
      // One entry per dictionary (InkPeek's DictionaryPage): its first line, its articles, its first headword's sections.
      for (auto &[d, hits] : lib.lookup(c.text, c.answer, c.lang)) {
        Entry e;
        e.dictionary = d->book().empty() ? d->name() : "InkPeek · " + d->book() + " → " + d->answer();
        e.id = d->id();
        e.word = hits.front().word;
        e.line = dict::head_line(hits.front().html).value_or(e.word);
        for (auto &h : hits) {
          bool other = text::lower(h.word) != text::lower(c.text);
          e.article += (other ? "<b>" + text::escape(h.word) + "</b><br>" : "") + h.html;
          if (d->book().empty()) continue;
          Article a{h.word, dict::head_line(h.html).value_or(h.word), {}};
          for (auto &x : dict::senses(h.html)) a.senses.push_back({x.text, x.examples});
          e.articles.push_back(a);
        }
        if (auto x = d->related(e.word)) e.related = {x->rel, x->syn, x->ant, x->expr, x->ipa, x->origin, x->forms};
        c.entries.push_back(e);
      }
    } else if (!c.lang.empty()) {
      // "das Haus": the pack's header of its second word starts so → saved as that word.
      static const text::Regex word_re("\\S+");
      auto words = word_re.all(c.text);
      if (words.size() == 2)
        if (auto heads = lib.heads(words[1].groups[0], c.answer, c.lang);
            !heads.empty() && text::lower(heads.front().header()).starts_with(text::lower(c.text)))
          c.save_text = heads.front().word, c.save_kind = "word", c.save_line = heads.front().line;
      const size_t limit = 60;
      for (auto &gl : lib.gloss(c.text, c.answer, limit, c.lang))
        c.gloss.push_back({gl.word, gl.head ? gl.head->word : gl.word, gl.head ? gl.head->meaning() : "", gl.head ? gl.head->header() : ""});
      c.more = static_cast<int>(dict::gloss_words(c.text) - c.gloss.size());
      for (auto &[f, h] : lib.separable(c.text, c.answer))
        c.separable.push_back({f.form + " … " + f.particle, f.verb, h ? h->meaning() : "", h ? h->header() : ""});
    }
  } catch (const std::exception &e) {
    c.error = e.what();
  }
  // Every card counts as a look (history.seen), not the CLI's.
  if (r.source != Source::cli && c.error.empty()) {
    auto kind = c.kind == Card::Kind::word ? "word" : "phrase";
    impl_->store->seen(c.text, kind, c.answer, c.entries.empty() ? "" : c.entries.front().line, c.base);
  }
  c.looks = impl_->store->looks(c.base.empty() ? c.text : c.base);
  if (c.save_text.empty()) c.save_text = c.text, c.save_kind = c.kind == Card::Kind::word ? "word" : "phrase";
  if (c.save_kind.empty()) c.save_kind = "word";
  c.saved = impl_->store->is_saved(c.save_text) || impl_->store->is_saved(c.text);
  c.answer_name = language_name(c.answer);
  if (!c.lang.empty()) c.lang_name = language_name(c.lang);
  if (!c.other.empty()) c.other_name = language_name(c.other);
  if (c.lang.empty() || c.lang == c.answer) return c;
  auto manifest = Packs(impl_->o.data, "").cached();
  for (auto m : {Method::fast, Method::direct, Method::llm}) {
    MethodState s{m, method_title(m), false, ""};
    // A model this machine cannot run comes first (no use installing it), then a missing one.
    if (m != Method::fast && impl_->o.device)
      if (auto comp = covering(manifest, m == Method::direct ? "opus" : "llm", c.lang, c.answer)) s.reason = cannot(*comp, *impl_->o.device);
    if (s.reason.empty()) s.reason = impl_->tr->why_not(c.lang, c.answer, m);
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
  // Kept as InkPeek's offline answers (section offline-<from>, ver by the models' sha256): an updated model translates again.
  auto section = "offline-" + card.lang;
  Packs packs(impl_->o.data, "");
  auto ver = answer_version(packs.cached(), packs.installed(), card.lang, card.answer, *method);
  if (auto a = impl_->store->answer(card.text, card.answer, section, ver)) return {*a, "", method_title(*method), ""};
  auto t = impl_->tr->translate(card.lang, card.answer, card.text, *method);
  if (t.ok() && card.source != Source::cli) impl_->store->put_answer(card.text, card.answer, section, ver, answer_model(*method), t.text);
  return t;
}

bool Core::toggle_saved(const Card &card, const std::string &translation) {
  auto &st = *impl_->store;
  if (card.save_text.empty()) return false;
  if (st.is_saved(card.save_text) || st.is_saved(card.text)) {
    st.unsave(card.save_text);
    if (card.text != card.save_text) st.unsave(card.text);
    return false;
  }
  auto line = card.save_line.empty() ? translation : card.save_line;
  return st.save(card.save_text, card.save_kind, card.answer, line, card.text).empty();
}

std::vector<Seen> Core::recents(int n) { return impl_->store->recents(n); }

Store &Core::store() { return *impl_->store; }

std::string Core::answer() {
  std::lock_guard g(impl_->answer_mu);
  return impl_->notes_answer;
}

LanguageGuess Core::language(const std::string &text, const std::string &answer) {
  std::lock_guard g(impl_->mu);
  auto [lang, other] = impl_->source(text, answer);
  return {lang, other};
}

SectionPage Core::section(const Card &card, int entry, const std::string &key, int page) {
  std::lock_guard g(impl_->mu);
  const int per = 20;  // InkPeek's WordsList.PAGE_SHORT
  SectionPage p;
  if (entry < 0 || entry >= static_cast<int>(card.entries.size())) return p;
  auto &e = card.entries[entry];
  p.key = key, p.word = e.word, p.entry = entry;
  auto &r = e.related;
  if (key == "forms") {
    for (auto &i : r.ipa) p.text += (p.text.empty() ? "" : ", ") + i;
    for (auto &[f, tags] : r.forms) p.items.push_back({f, f, tags, ""});
    p.total = static_cast<int>(p.items.size()), p.pages = 1;
    return p;
  }
  if (key == "ety") return p.text = r.origin, p.pages = 1, p;
  std::vector<std::string> all = key == "rel" ? r.rel : key == "expr" ? r.expr : r.syn;
  if (key == "syn")
    for (auto &a : r.ant) all.push_back("≠ " + a);
  p.total = static_cast<int>(all.size());
  p.pages = std::max(1, (p.total + per - 1) / per);
  p.page = std::clamp(page, 0, p.pages - 1);
  p.first = p.page * per;
  dict::StarDict *d = nullptr;
  for (auto *x : impl_->lib->all())
    if (x->id() == e.id) d = x;
  // The first meaning of each word from the same dictionary (expressions: none).
  for (int i = p.page * per; i < std::min(p.total, (p.page + 1) * per); i++) {
    auto x = all[i];
    auto w = x.starts_with("≠ ") ? x.substr(std::string("≠ ").size()) : x;
    std::string meaning;
    if (d && key != "expr") {
      try {
        auto hs = d->lookup(w);
        auto h = std::find_if(hs.begin(), hs.end(), [&](const dict::Hit &h) { return h.word == w; });
        if (h == hs.end()) h = std::find_if(hs.begin(), hs.end(), [&](const dict::Hit &h) { return text::lower(h.word) == text::lower(w); });
        if (h != hs.end())
          if (auto l = dict::head_line(h->html)) meaning = dict::Head{h->word, *l, ""}.meaning();
      } catch (const std::exception &) {
      }
    }
    p.items.push_back({x, w, meaning, ""});
  }
  return p;
}

void Core::cancel() { impl_->tr->cancel(); }

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
  impl_->tr->release();
}

Translator &Core::translator() { return *impl_->tr; }

}  // namespace lexipeek
