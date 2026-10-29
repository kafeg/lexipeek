#include "dict/library.hpp"
#include <algorithm>
#include <map>
#include <set>
#include "text/text.hpp"

namespace lexipeek::dict {

namespace fs = std::filesystem;

std::string Head::header() const {
  auto i = line.find(" — ");
  static const text::Regex edge("^\\s+|\\s+$");
  return edge.replace(i == std::string::npos ? line : line.substr(0, i), "");
}

std::string Head::meaning() const {
  auto i = line.find(" — ");
  static const text::Regex edge("^\\s+|\\s+$");
  return i == std::string::npos ? "" : edge.replace(line.substr(i + std::string(" — ").size()), "");
}

std::optional<std::string> head_line(const std::string &html) {
  static const text::Regex head("<p class=\"head\">(.*?)</p>");
  static const text::Regex edge("^\\s+|\\s+$");
  text::Regex::Match m;
  if (!head.find(html, m)) return std::nullopt;
  return edge.replace(text::strip_tags(m.groups[1]), "");
}

std::vector<Sense> senses(const std::string &html) {
  static const text::Regex li("<li[^>]*>(.*?)</li>", false, true), br("<br\\s*/?>"), small("^\\s*<small>(.*)</small>\\s*$", false, true),
      space("\\s+"), edge("^\\s+|\\s+$");
  auto plain = [&](const std::string &h) { return edge.replace(space.replace(text::strip_tags(h), " "), ""); };
  std::vector<Sense> out;
  for (auto &m : li.all(html)) {
    Sense s;
    // The meaning, then its examples, each after a <br> in <small>.
    auto parts = br.replace(m.groups[1], "\n");
    size_t at = 0;
    while (at <= parts.size()) {
      auto nl = parts.find('\n', at);
      auto part = parts.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
      text::Regex::Match e;
      if (small.find(part, e)) {
        if (auto x = plain(e.groups[1]); !x.empty()) s.examples.push_back(x);
      } else if (auto x = plain(part); !x.empty()) {
        s.text += (s.text.empty() ? "" : " ") + x;
      }
      if (nl == std::string::npos) break;
      at = nl + 1;
    }
    out.push_back(s);
  }
  return out;
}

size_t gloss_words(const std::string &txt) {
  static const text::Regex word("\\p{L}[\\p{L}'’-]*");
  std::set<std::string> seen;
  for (auto &m : word.all(txt)) seen.insert(text::lower(m.groups[0]));
  return seen.size();
}

std::vector<SeparableFind> find_separable(const std::string &text, const std::function<std::vector<std::string>(const std::string &)> &bases,
                                          const std::function<std::string(const std::string &)> &particle) {
  static const text::Regex token("\\p{L}+|[^\\p{L}\\p{N}\\s]"), letters("^\\p{L}+$");
  static const std::set<std::string> conj{"und", "oder", "aber", "sondern", "denn"};
  std::vector<std::vector<std::string>> clauses(1);
  for (auto &m : token.all(text)) {
    if (letters.matches(m.groups[0]))
      clauses.back().push_back(m.groups[0]);
    else if (!clauses.back().empty())
      clauses.emplace_back();
  }
  std::vector<SeparableFind> out;
  auto add = [&](SeparableFind f) {
    if (std::none_of(out.begin(), out.end(), [&](const SeparableFind &x) { return x.verb == f.verb; })) out.push_back(f);
  };
  for (auto &c : clauses) {
    for (size_t j = 1; j < c.size(); j++) {
      auto &p = c[j];
      if (!text::is_lower(p) || !(j == c.size() - 1 || conj.contains(c[j + 1]))) continue;
      for (size_t i = j; i-- > 0;) {
        if (i > 0 && !text::is_lower(c[i])) continue;
        std::vector<std::string> verbs;
        for (auto &b : bases(text::lower(c[i])))
          if (particle(p + b) == p) verbs.push_back(p + b);
        if (verbs.empty()) continue;
        for (auto &v : verbs) add({c[i], p, v});
        break;
      }
    }
  }
  return out;
}

Library::Library(fs::path packs, fs::path work, Own own) : packs_(std::move(packs)), work_(std::move(work)), own_(std::move(own)) {}

Library::~Library() = default;

void Library::configure(Own own) { own_ = std::move(own); }

std::vector<StarDict *> Library::found() {
  std::vector<std::pair<fs::path, bool>> files;
  std::set<fs::path> seen;
  auto add = [&](const fs::path &root, bool pack) {
    for (auto &f : scan(root)) {
      std::error_code ec;
      auto c = fs::weakly_canonical(f, ec);
      if (!ec && seen.insert(c).second) files.emplace_back(c, pack);
    }
  };
  add(packs_, true);
  for (auto &r : own_.roots) add(r, false);
  std::string stamp;
  for (auto &[f, pack] : files) stamp += (pack ? "p " : "o ") + f.string() + "\n";
  if (stamp != stamp_) {
    std::vector<std::unique_ptr<StarDict>> next;
    for (auto &[f, pack] : files) {
      auto same = std::find_if(dicts_.begin(), dicts_.end(), [&](auto &d) { return d && d->ifo() == f && d->pack() == pack; });
      if (same != dicts_.end()) next.push_back(std::move(*same));
      else {
        // One unreadable .ifo (being rewritten by another app) is skipped, not the whole library.
        try {
          next.push_back(std::make_unique<StarDict>(f, work_, pack));
        } catch (const std::exception &) {
        }
      }
    }
    dicts_ = std::move(next);
    stamp_ = stamp;
  }
  std::vector<StarDict *> out;
  for (auto &d : dicts_) out.push_back(d.get());
  auto rank = [&](StarDict *d) {
    auto i = std::find(own_.order.begin(), own_.order.end(), d->id());
    return i == own_.order.end() ? own_.order.size() : static_cast<size_t>(i - own_.order.begin());
  };
  std::stable_sort(out.begin(), out.end(), [&](StarDict *a, StarDict *b) {
    if (rank(a) != rank(b)) return rank(a) < rank(b);
    if (a->pack() != b->pack()) return a->pack();
    return a->name() < b->name();
  });
  return out;
}

std::vector<StarDict *> Library::all() {
  auto out = found();
  std::erase_if(out, [&](StarDict *d) { return !d->pack() && std::find(own_.off.begin(), own_.off.end(), d->id()) != own_.off.end(); });
  return out;
}

std::vector<std::pair<StarDict *, std::vector<Hit>>> Library::lookup(const std::string &query, const std::string &answer,
                                                                     const std::string &book) {
  return lookup(query, answer, all(), book);
}

std::vector<std::pair<StarDict *, std::vector<Hit>>> Library::lookup(const std::string &query, const std::string &answer,
                                                                     const std::vector<StarDict *> &among, const std::string &book) {
  auto q = text::clean_query(query);
  auto lq = text::lower(q);
  std::vector<std::pair<StarDict *, std::vector<Hit>>> hits;
  for (auto *d : among) {
    try {
      hits.emplace_back(d, d->lookup(q));
    } catch (const std::exception &) {
      hits.emplace_back(d, std::vector<Hit>{});
    }
  }
  // A dictionary that knows the form (Häuser) names the base form (Haus): looked up in the others too.
  std::vector<std::string> bases;
  for (auto &[d, hs] : hits)
    for (auto &h : hs)
      if (text::lower(h.word) != lq && std::find(bases.begin(), bases.end(), h.word) == bases.end() && bases.size() < 3) bases.push_back(h.word);
  std::vector<std::pair<StarDict *, std::vector<Hit>>> out;
  for (auto &[d, hs] : hits) {
    auto own = hs;
    if (own.empty())
      for (auto &b : bases)
        try {
          for (auto &h : d->lookup(b))
            if (h.word == b) own.push_back(h);
        } catch (const std::exception &) {
        }
    if (!own.empty()) out.emplace_back(d, own);
  }
  // As InkPeek's Library.lookup over the ticked pairs: the packs from the text's language into answer, the own ones too.
  auto pack = [](StarDict *d) { return d->pack(); };
  std::vector<std::pair<StarDict *, std::vector<Hit>>> into, from;
  for (auto &x : out) {
    if (pack(x.first) && x.first->answer() != answer) continue;
    into.push_back(x);
    if (!pack(x.first) || x.first->book() == book) from.push_back(x);
  }
  if (!book.empty() && std::any_of(from.begin(), from.end(), [&](auto &x) { return pack(x.first); })) return from;
  return into.empty() ? out : into;
}

std::vector<std::pair<StarDict *, std::vector<Hit>>> Library::pack_lookup(const std::string &query, const std::string &book,
                                                                          const std::string &answer) {
  auto among = all();
  std::erase_if(among, [&](StarDict *d) { return !d->pack() || d->book() != book || d->answer() != answer; });
  return lookup(query, answer, among, book);
}

std::vector<Head> Library::heads(const std::string &query, const std::string &answer, const std::string &book) {
  std::vector<Head> out;
  std::set<std::string> books;
  auto found = lookup(query, answer, book);
  std::stable_partition(found.begin(), found.end(), [&](auto &x) { return x.first->book() == book; });
  for (auto &[d, hs] : found) {
    if (!d->pack() || d->book().empty() || !books.insert(d->book()).second) continue;
    for (auto &h : hs)
      if (auto l = head_line(h.html)) {
        out.push_back({h.word, *l, d->book()});
        break;
      }
  }
  return out;
}

std::vector<GlossLine> Library::gloss(const std::string &txt, const std::string &answer, size_t limit, const std::string &book) {
  static const text::Regex word("\\p{L}[\\p{L}'’-]*"), tail("[\\s]+$");
  auto dicts = all();
  auto matches = word.all(txt);
  std::vector<std::string> words;
  std::set<std::string> seen, starts;
  for (auto &m : matches) {
    if (seen.insert(text::lower(m.groups[0])).second && words.size() < limit) words.push_back(m.groups[0]);
    auto before = tail.replace(txt.substr(0, m.begin), "");
    // A sentence's first word is capitalised by spelling, not by meaning: "Er" is "er".
    if (before.empty() || std::string(".!?:").find(before.back()) != std::string::npos || before.ends_with("…")) starts.insert(m.groups[0]);
  }
  std::map<std::string, std::vector<std::pair<StarDict *, std::vector<Hit>>>> found;
  std::map<std::string, int> known;
  for (auto &w : words) {
    std::vector<std::pair<StarDict *, std::vector<Hit>>> r;
    auto lw = text::lower(w);
    if (starts.contains(w) && w != lw)
      for (auto &x : lookup(lw, answer, dicts, book))
        if (x.second.front().word == lw) r.push_back(x);
    if (r.empty()) r = lookup(w, answer, dicts, book);
    for (auto &x : r) known[x.first->id()]++;
    found[w] = r;
  }
  int top = 0;
  for (auto &[id, n] : known) top = std::max(top, n);
  std::vector<GlossLine> out;
  for (auto &w : words) {
    std::vector<std::pair<StarDict *, Head>> cand;
    for (auto &[d, hs] : found[w]) {
      if (known[d->id()] * 2 < top) continue;
      // An own dictionary gives its first meaning.
      if (auto h = head(hs.front())) {
        h->book = d->book();
        if (!h->meaning().empty()) cand.emplace_back(d, *h);
      }
    }
    std::stable_sort(cand.begin(), cand.end(), [&](auto &a, auto &b) {
      bool aa = a.first->answer() != answer, ba = b.first->answer() != answer;
      if (aa != ba) return !aa;
      return known[a.first->id()] > known[b.first->id()];
    });
    out.push_back({w, cand.empty() ? std::nullopt : std::optional<Head>(cand.front().second)});
  }
  return out;
}

std::vector<std::pair<SeparableFind, std::optional<Head>>> Library::separable(const std::string &txt, const std::string &answer) {
  auto dicts = all();
  std::erase_if(dicts, [](StarDict *d) { return !d->pack(); });
  auto finds = find_separable(
      txt,
      [&](const std::string &f) {
        std::vector<std::string> out;
        for (auto *d : dicts)
          for (auto &h : d->lookup(f))
            if (std::find(out.begin(), out.end(), h.word) == out.end()) out.push_back(h.word);
        return out;
      },
      [&](const std::string &v) -> std::string {
        for (auto *d : dicts)
          if (auto r = d->related(v); r && !r->sep.empty()) return r->sep;
        return "";
      });
  std::vector<std::pair<SeparableFind, std::optional<Head>>> out;
  for (auto &f : finds) {
    auto hs = heads(f.verb, answer);
    out.emplace_back(f, hs.empty() ? std::nullopt : std::optional<Head>(hs.front()));
  }
  return out;
}

int Library::rank(const std::string &word, const std::string &book) {
  for (auto *d : all())
    if (d->book() == book)
      if (auto r = d->related(word); r && r->rank > 0) return r->rank;
  return 0;
}

bool Library::knows(const std::string &book, const std::string &answer, const std::string &word) {
  for (auto *d : all())
    if (d->book() == book && (answer.empty() || d->answer() == answer)) {
      try {
        return !d->lookup(word).empty();
      } catch (const std::exception &) {
        return false;
      }
    }
  return false;
}

}  // namespace lexipeek::dict
