// Text helpers shared by the modules: PCRE2 regex, InkPeek's lookup key, lower case, HTML of the packs' articles.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lexipeek::text {

// A compiled PCRE2 pattern in UTF mode (\p{L} Unicode, \s ASCII as in Java's regex).
class Regex {
 public:
  explicit Regex(std::string_view pattern, bool ignore_case = false, bool dotall = false);
  ~Regex();
  Regex(const Regex &) = delete;
  Regex &operator=(const Regex &) = delete;

  struct Match {
    size_t begin = 0, end = 0;          // bytes
    std::vector<std::string> groups;    // 0: the whole match
  };
  bool find(std::string_view s, Match &m, size_t from = 0) const;
  std::vector<Match> all(std::string_view s) const;
  bool matches(std::string_view s) const;  // the whole string
  std::string replace(std::string_view s, std::string_view with) const;  // every match; "$1" = group 1
  std::string replace(std::string_view s, const std::function<std::string(const Match &)> &with) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Kotlin's lowercase() (Locale.ROOT), code point by code point.
std::string lower(std::string_view s);
std::string nfc(std::string_view s);
// InkPeek's normKey: NFC, soft hyphens out, trimmed, lower case.
std::string norm_key(std::string_view s);
// InkPeek's cleanQuery: soft hyphens out, spaces collapsed, punctuation, symbols and spaces at the edges out.
std::string clean_query(std::string_view s);
// InkPeek's cleanPhrase: like clean_query, but ¿¡ at the start and .!?… at the end stay.
std::string clean_phrase(std::string_view s);
// Tags out, the five entities back.
std::string strip_tags(std::string_view html);
std::string unescape(std::string_view s);
std::string escape(std::string_view s);
bool is_lower(std::string_view s);  // equal to its lower case
// Java's String.hashCode over UTF-16 units (InkPeek's versions of kept answers).
std::int32_t java_hash(std::string_view s);

}  // namespace lexipeek::text
