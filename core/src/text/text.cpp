#define PCRE2_CODE_UNIT_WIDTH 8
#include "text/text.hpp"
#include <pcre2.h>
#include <stdexcept>
#include <utf8proc.h>

namespace lexipeek::text {

struct Regex::Impl {
  pcre2_code *code = nullptr;
};

Regex::Regex(std::string_view pattern, bool ignore_case, bool dotall) : impl_(std::make_unique<Impl>()) {
  int err = 0;
  PCRE2_SIZE at = 0;
  uint32_t flags = PCRE2_UTF | (ignore_case ? PCRE2_CASELESS : 0) | (dotall ? PCRE2_DOTALL : 0);
  impl_->code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(), flags, &err, &at, nullptr);
  if (!impl_->code) throw std::runtime_error("Bad regex: " + std::string(pattern));
  pcre2_jit_compile(impl_->code, PCRE2_JIT_COMPLETE);
}

Regex::~Regex() { pcre2_code_free(impl_->code); }

bool Regex::find(std::string_view s, Match &m, size_t from) const {
  auto *md = pcre2_match_data_create_from_pattern(impl_->code, nullptr);
  int n = pcre2_match(impl_->code, reinterpret_cast<PCRE2_SPTR>(s.data()), s.size(), from, 0, md, nullptr);
  bool ok = n > 0;
  if (ok) {
    auto *ov = pcre2_get_ovector_pointer(md);
    m.begin = ov[0];
    m.end = ov[1];
    m.groups.clear();
    for (int i = 0; i < n; i++)
      m.groups.emplace_back(ov[2 * i] == PCRE2_UNSET ? std::string() : std::string(s.substr(ov[2 * i], ov[2 * i + 1] - ov[2 * i])));
  }
  pcre2_match_data_free(md);
  return ok;
}

std::vector<Regex::Match> Regex::all(std::string_view s) const {
  std::vector<Match> out;
  Match m;
  for (size_t from = 0; from <= s.size() && find(s, m, from);) {
    out.push_back(m);
    if (m.end == m.begin) {
      // An empty match: step over one UTF-8 character.
      from = m.end + 1;
      while (from < s.size() && (static_cast<unsigned char>(s[from]) & 0xC0) == 0x80) from++;
    } else {
      from = m.end;
    }
  }
  return out;
}

bool Regex::matches(std::string_view s) const {
  Match m;
  return find(s, m) && m.begin == 0 && m.end == s.size();
}

std::string Regex::replace(std::string_view s, const std::function<std::string(const Match &)> &with) const {
  std::string out;
  size_t last = 0;
  for (auto &m : all(s)) {
    out.append(s.substr(last, m.begin - last));
    out += with(m);
    last = m.end;
  }
  out.append(s.substr(last));
  return out;
}

std::string Regex::replace(std::string_view s, std::string_view with) const {
  return replace(s, [&](const Match &m) {
    std::string r;
    for (size_t i = 0; i < with.size(); i++) {
      if (with[i] == '$' && i + 1 < with.size() && with[i + 1] >= '0' && with[i + 1] <= '9') {
        auto g = static_cast<size_t>(with[++i] - '0');
        if (g < m.groups.size()) r += m.groups[g];
      } else {
        r += with[i];
      }
    }
    return r;
  });
}

namespace {

template <class F>
void each(std::string_view s, F f) {
  auto p = reinterpret_cast<const utf8proc_uint8_t *>(s.data());
  auto left = static_cast<utf8proc_ssize_t>(s.size());
  while (left > 0) {
    utf8proc_int32_t cp;
    auto k = utf8proc_iterate(p, left, &cp);
    if (k <= 0) { k = 1; cp = 0xFFFD; }
    f(cp);
    p += k;
    left -= k;
  }
}

void put(std::string &out, utf8proc_int32_t cp) {
  utf8proc_uint8_t buf[4];
  auto n = utf8proc_encode_char(cp, buf);
  out.append(reinterpret_cast<char *>(buf), static_cast<size_t>(n));
}

const Regex &spaces() {
  static const Regex r("\\s+");
  return r;
}

std::string no_soft_hyphens(std::string_view s) {
  std::string out;
  each(s, [&](utf8proc_int32_t c) {
    if (c != 0xAD) put(out, c);
  });
  return out;
}

}  // namespace

std::string lower(std::string_view s) {
  std::string out;
  each(s, [&](utf8proc_int32_t c) { put(out, utf8proc_tolower(c)); });
  return out;
}

bool is_lower(std::string_view s) { return lower(s) == s; }

std::int32_t java_hash(std::string_view s) {
  std::uint32_t h = 0;
  each(s, [&](utf8proc_int32_t c) {
    auto cp = static_cast<std::uint32_t>(c);
    if (cp < 0x10000) {
      h = h * 31 + cp;
    } else {
      cp -= 0x10000;
      h = h * 31 + (0xD800 + (cp >> 10));
      h = h * 31 + (0xDC00 + (cp & 0x3FF));
    }
  });
  return static_cast<std::int32_t>(h);
}

std::string nfc(std::string_view s) {
  utf8proc_uint8_t *r = nullptr;
  auto n = utf8proc_map(reinterpret_cast<const utf8proc_uint8_t *>(s.data()), static_cast<utf8proc_ssize_t>(s.size()), &r,
                        static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE));
  if (n < 0) return std::string(s);
  std::string out(reinterpret_cast<char *>(r), static_cast<size_t>(n));
  free(r);
  return out;
}

std::string norm_key(std::string_view s) {
  static const Regex edge("^\\s+|\\s+$");
  return lower(edge.replace(no_soft_hyphens(nfc(s)), ""));
}

std::string clean_query(std::string_view s) {
  static const Regex edge("^[\\p{P}\\p{S}\\s]+|[\\p{P}\\p{S}\\s]+$");
  return edge.replace(spaces().replace(no_soft_hyphens(s), " "), "");
}

std::string clean_phrase(std::string_view s) {
  static const Regex start("^(?:(?![¿¡])[\\p{P}\\p{S}\\s])+");
  static const Regex end("(?:(?![.!?…。！？])[\\p{P}\\p{S}\\s])+$");
  return end.replace(start.replace(spaces().replace(no_soft_hyphens(s), " "), ""), "");
}

std::string unescape(std::string_view s) {
  static const Regex e("&(lt|gt|quot|#x27|#39|amp|nbsp);");
  return e.replace(s, [](const Regex::Match &m) -> std::string {
    auto &n = m.groups[1];
    if (n == "lt") return "<";
    if (n == "gt") return ">";
    if (n == "quot") return "\"";
    if (n == "amp") return "&";
    if (n == "nbsp") return " ";
    return "'";
  });
}

std::string strip_tags(std::string_view html) {
  static const Regex tags("<[^>]+>");
  return unescape(tags.replace(html, ""));
}

std::string escape(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

}  // namespace lexipeek::text
