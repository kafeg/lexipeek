#include "lexipeek/l10n.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace lexipeek {

namespace {

using Table = std::vector<std::pair<std::string, std::string>>;

const std::vector<Table> &tables() {
  static const std::vector<Table> t{
      {
#include "en.inc"
      },
      {
#include "ru.inc"
      },
      {
#include "de.inc"
      },
      {
#include "fr.inc"
      },
      {
#include "es.inc"
      },
      {
#include "it.inc"
      },
      {
#include "pt.inc"
      },
      {
#include "pl.inc"
      }};
  return t;
}

const std::vector<std::map<std::string, std::string, std::less<>>> &maps() {
  static const auto m = [] {
    std::vector<std::map<std::string, std::string, std::less<>>> out;
    for (auto &t : tables()) out.emplace_back(t.begin(), t.end());
    return out;
  }();
  return m;
}

std::atomic<int> current{0};
std::mutex missing_mu;
std::set<std::string> missing;

int index_of(const std::string &code) {
  auto &l = ui_languages();
  auto i = std::find(l.begin(), l.end(), code);
  return i == l.end() ? -1 : static_cast<int>(i - l.begin());
}

const std::string *find(int lang, std::string_view key) {
  auto &m = maps()[static_cast<size_t>(lang)];
  auto i = m.find(key);
  return i == m.end() ? nullptr : &i->second;
}

std::string fill(std::string s, const std::vector<std::string> &args) {
  for (size_t i = 0; i < args.size() && i < 9; i++) {
    auto mark = "{" + std::to_string(i + 1) + "}";
    for (auto at = s.find(mark); at != std::string::npos; at = s.find(mark, at + args[i].size())) s.replace(at, mark.size(), args[i]);
  }
  return s;
}

}  // namespace

const std::vector<std::string> &ui_languages() {
  static const std::vector<std::string> l{"en", "ru", "de", "fr", "es", "it", "pt", "pl"};
  return l;
}

std::string ui_language(const std::string &setting, const std::string &system) {
  if (index_of(setting) >= 0) return setting;
  return index_of(system) >= 0 ? system : "en";
}

void set_ui_language(const std::string &code) { current = std::max(0, index_of(code)); }
std::string ui_language() { return ui_languages()[static_cast<size_t>(current.load())]; }

std::string tr(std::string_view key, const std::vector<std::string> &args) {
  auto *t = find(current, key);
  if (!t) t = find(0, key);
  if (!t) {
    std::lock_guard g(missing_mu);
    missing.emplace(key);
  }
  return fill(t ? *t : std::string(key), args);
}

bool has_text(std::string_view key) { return find(0, key) != nullptr; }

std::string trn(std::string_view key, long long n, const std::vector<std::string> &args) {
  auto k = std::string(key) + "." + plural_form(ui_language(), n);
  auto s = tr(k, args);
  for (auto at = s.find("{n}"); at != std::string::npos; at = s.find("{n}")) s.replace(at, 3, digits(n));
  return s;
}

std::string digits(long long n) {
  auto l = ui_language();
  std::string sep = l == "en" ? "," : l == "ru" || l == "fr" || l == "pl" ? " " : ".";
  auto s = std::to_string(n < 0 ? -n : n);
  // Spanish and Polish leave four digits as they are.
  if (s.size() > 4 || ((l != "es" && l != "pl") && s.size() > 3))
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), sep);
  return (n < 0 ? "-" : "") + s;
}

std::string decimal(double x) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", x);
  std::string s = buf;
  if (ui_language() != "en") std::replace(s.begin(), s.end(), '.', ',');
  return s;
}

std::vector<std::pair<std::string, std::string>> texts(const std::string &lang) {
  auto i = index_of(lang);
  return i < 0 ? Table{} : tables()[static_cast<size_t>(i)];
}

std::vector<std::string> plural_forms(const std::string &lang) {
  if (lang == "ru" || lang == "pl") return {"one", "few", "many"};
  return {"one", "other"};
}

std::vector<std::string> missing_texts() {
  std::lock_guard g(missing_mu);
  return {missing.begin(), missing.end()};
}

std::string plural_form(const std::string &lang, long long n) {
  auto a = n < 0 ? -n : n;
  if (lang == "ru") return a % 10 == 1 && a % 100 != 11 ? "one" : a % 10 >= 2 && a % 10 <= 4 && (a % 100 < 12 || a % 100 > 14) ? "few" : "many";
  if (lang == "pl") return a == 1 ? "one" : a % 10 >= 2 && a % 10 <= 4 && (a % 100 < 12 || a % 100 > 14) ? "few" : "many";
  if (lang == "fr") return a <= 1 ? "one" : "other";
  return a == 1 ? "one" : "other";
}

}  // namespace lexipeek
