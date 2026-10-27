// Settings → General → Updates: GitHub's latest release of LexiPeek against this version.
#include <nlohmann/json.hpp>
#include "lexipeek/l10n.hpp"
#include "lexipeek/views.hpp"

namespace lexipeek {

namespace {

std::vector<long> numbers(std::string v) {
  if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
  std::vector<long> out;
  long n = -1;
  for (char c : v) {
    if (c >= '0' && c <= '9') n = (n < 0 ? 0 : n * 10) + (c - '0');
    else if (c == '.' && n >= 0) out.push_back(n), n = -1;
    else break;
  }
  if (n >= 0) out.push_back(n);
  return out;
}

}  // namespace

bool newer_version(const std::string &release, const std::string &current) {
  auto a = numbers(release), b = numbers(current);
  a.resize(std::max(a.size(), b.size())), b.resize(a.size());
  return a > b;
}

UpdateCheck update_check(const std::string &version, long status, const std::string &body, const std::string &error) {
  if (status == 404) return {tr("update_none"), "", ""};
  if (!error.empty()) return {tr("failed", {error}), "", ""};
  auto j = nlohmann::json::parse(body, nullptr, false);
  if (!j.is_object() || !j.contains("tag_name") || !j["tag_name"].is_string()) return {tr("failed", {"GitHub"}), "", ""};
  auto tag = j["tag_name"].get<std::string>();
  auto name = tag.starts_with("v") ? tag.substr(1) : tag;
  if (!newer_version(tag, version)) return {tr("update_latest", {version}), "", ""};
  return {tr("update_new", {"LexiPeek " + name}), name, j.value("html_url", "https://github.com/kafeg/lexipeek/releases/latest")};
}

}  // namespace lexipeek
