// Sentence boundaries by Unicode's UAX #29 rules (as ICU's, without abbreviation suppressions: InkPeek's BreakIterator).
#include <algorithm>
#include <utf8proc.h>
#include "mt/text.hpp"

namespace lexipeek::mt {

namespace {

enum class SB { Other, CR, LF, Extend, Sep, Format, Sp, Lower, Upper, OLetter, Numeric, ATerm, SContinue, STerm, Close };

struct Range {
  char32_t lo, hi;
  SB p;
};

constexpr Range TABLE[] = {
#include "mt/sentence_break.inc"
};

SB prop(char32_t c) {
  auto it = std::upper_bound(std::begin(TABLE), std::end(TABLE), c, [](char32_t x, const Range &r) { return x < r.lo; });
  if (it == std::begin(TABLE)) return SB::Other;
  --it;
  return c <= it->hi ? it->p : SB::Other;
}

bool para(SB p) { return p == SB::Sep || p == SB::CR || p == SB::LF; }
bool saterm(SB p) { return p == SB::ATerm || p == SB::STerm; }

// Kotlin's Char.isWhitespace, which String.trim() uses.
bool space(char32_t c) {
  if (c == '\t' || c == '\n' || c == 0x0B || c == '\f' || c == '\r' || (c >= 0x1C && c <= 0x1F)) return true;
  auto cat = utf8proc_category(static_cast<utf8proc_int32_t>(c));
  return cat == UTF8PROC_CATEGORY_ZS || cat == UTF8PROC_CATEGORY_ZL || cat == UTF8PROC_CATEGORY_ZP;
}

}  // namespace

std::u32string utf32(const std::string &s) {
  std::u32string out;
  auto p = reinterpret_cast<const utf8proc_uint8_t *>(s.data());
  auto left = static_cast<utf8proc_ssize_t>(s.size());
  while (left > 0) {
    utf8proc_int32_t cp;
    auto k = utf8proc_iterate(p, left, &cp);
    if (k <= 0) { k = 1; cp = 0xFFFD; }
    out.push_back(static_cast<char32_t>(cp));
    p += k;
    left -= k;
  }
  return out;
}

std::string utf8(const std::u32string &s) {
  std::string out;
  for (auto c : s) {
    utf8proc_uint8_t buf[4];
    auto n = utf8proc_encode_char(static_cast<utf8proc_int32_t>(c), buf);
    out.append(reinterpret_cast<char *>(buf), static_cast<size_t>(n));
  }
  return out;
}

std::vector<size_t> sentence_breaks(const std::u32string &t) {
  std::vector<size_t> out{0};
  if (t.empty()) return out;
  // SB5: Extend and Format belong to the character before them (not after a paragraph separator).
  std::vector<size_t> x;
  for (size_t i = 0; i < t.size(); i++) {
    auto p = prop(t[i]);
    if ((p == SB::Extend || p == SB::Format) && !x.empty() && !para(prop(t[x.back()]))) continue;
    x.push_back(i);
  }
  auto P = [&](size_t j) { return prop(t[x[j]]); };
  for (size_t j = 1; j < x.size(); j++) {
    auto prev = P(j - 1), cur = P(j);
    bool brk;
    if (prev == SB::CR && cur == SB::LF) brk = false;  // SB3
    else if (para(prev)) brk = true;                    // SB4
    else if (prev == SB::ATerm && cur == SB::Numeric) brk = false;  // SB6
    else if (prev == SB::ATerm && j >= 2 && (P(j - 2) == SB::Upper || P(j - 2) == SB::Lower) && cur == SB::Upper) brk = false;  // SB7
    else {
      // SATerm Close* Sp* before the boundary?
      size_t k = j;
      bool sp = false;
      while (k > 0 && P(k - 1) == SB::Sp) --k, sp = true;
      while (k > 0 && P(k - 1) == SB::Close) --k;
      if (k == 0 || !saterm(P(k - 1))) {
        brk = false;  // SB998
      } else {
        auto term = P(k - 1);
        bool lower = false;
        if (term == SB::ATerm) {  // SB8: … × ( ¬(OLetter | Upper | Lower | ParaSep | SATerm) )* Lower
          for (size_t a = j; a < x.size(); a++) {
            auto q = P(a);
            if (q == SB::Lower) { lower = true; break; }
            if (q == SB::OLetter || q == SB::Upper || para(q) || saterm(q)) break;
          }
        }
        if (lower) brk = false;
        else if (cur == SB::SContinue || saterm(cur)) brk = false;                         // SB8a
        else if (!sp && (cur == SB::Close || cur == SB::Sp || para(cur))) brk = false;  // SB9
        else if (cur == SB::Sp || para(cur)) brk = false;                                // SB10
        else brk = true;                                                                  // SB11
      }
    }
    if (brk) out.push_back(x[j]);
  }
  out.push_back(t.size());
  return out;
}

std::string trim_space(const std::string &s) {
  auto u = utf32(s);
  size_t b = 0, e = u.size();
  while (b < e && space(u[b])) b++;
  while (e > b && space(u[e - 1])) e--;
  return utf8(u.substr(b, e - b));
}

std::vector<std::string> sentences(const std::string &text) {
  auto u = utf32(text);
  auto b = sentence_breaks(u);
  std::vector<std::string> out;
  for (size_t i = 1; i < b.size(); i++) out.push_back(trim_space(utf8(u.substr(b[i - 1], b[i] - b[i - 1]))));
  return out;
}

}  // namespace lexipeek::mt
