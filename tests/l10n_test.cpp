// The UI's texts: every key in every language, the same placeholders, plural forms; no user-visible English left in Swift.
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "lexipeek/l10n.hpp"
#include "lexipeek/views.hpp"
#include "text/text.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

namespace {

// "card_more.one" → "card_more".
std::string base(const std::string &key) {
  auto dot = key.rfind('.');
  return dot == std::string::npos ? key : key.substr(0, dot);
}

std::set<std::string> marks(const std::string &s, bool count) {
  static const text::Regex re("\\{[0-9n]\\}");
  std::set<std::string> out;
  for (auto &m : re.all(s))
    if (count || m.groups[0] != "{n}") out.insert(m.groups[0]);
  return out;
}

std::string read(const fs::path &p) {
  std::ifstream f(p);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

// Files under root with the extension.
std::vector<fs::path> files(const fs::path &root, const std::string &ext) {
  std::vector<fs::path> out;
  for (auto &e : fs::recursive_directory_iterator(root))
    if (e.is_regular_file() && e.path().extension() == ext) out.push_back(e.path());
  return out;
}

struct Lang {
  explicit Lang(const std::string &code) { set_ui_language(code); }
  ~Lang() { set_ui_language("en"); }
  Lang(const Lang &) = delete;
  Lang &operator=(const Lang &) = delete;
};

}  // namespace

TEST_CASE("l10n: every key in every language, its placeholders, its plural forms") {
  std::map<std::string, std::set<std::string>> en;  // base key → placeholders
  std::set<std::string> plural;
  for (auto &[k, t] : texts("en")) {
    CHECK_MESSAGE(!en.contains(k), k);
    bool p = k.find('.') != std::string::npos;
    if (p) plural.insert(base(k));
    en[p ? base(k) : k] = marks(t, !p);
  }
  CHECK(en.size() > 250);
  for (auto &lang : ui_languages()) {
    std::set<std::string> seen, keys;
    for (auto &[k, t] : texts(lang)) {
      CHECK_MESSAGE(!t.empty(), lang << " " << k);
      CHECK_MESSAGE(seen.insert(k).second, lang << " twice: " << k);
      auto b = k.find('.') != std::string::npos ? base(k) : k;
      keys.insert(b);
      REQUIRE_MESSAGE(en.contains(b), lang << " has a key English lacks: " << k);
      CHECK_MESSAGE(marks(t, !plural.contains(b)) == en[b], lang << " " << k << ": " << t);
      if (plural.contains(b)) {
        auto f = plural_forms(lang);
        CHECK_MESSAGE(std::find(f.begin(), f.end(), k.substr(b.size() + 1)) != f.end(), lang << " " << k);
      }
    }
    for (auto &[k, m] : en) CHECK_MESSAGE(keys.contains(k), lang << " lacks " << k);
    for (auto &k : plural)
      for (auto &f : plural_forms(lang)) CHECK_MESSAGE(seen.contains(k + "." + f), lang << " lacks " << k << "." << f);
  }
}

TEST_CASE("l10n: a Windows text replaces its key there, with no placeholder the key lacks") {
  auto all = texts("en");
  std::map<std::string, std::string> en(all.begin(), all.end());
  int n = 0;
  for (auto &[k, t] : en) {
    auto at = k.find("@win");
    if (at == std::string::npos) continue;
    ++n;
    auto b = k.substr(0, at);
    REQUIRE_MESSAGE(en.contains(b), k);
    for (auto &m : marks(t, true)) CHECK_MESSAGE(marks(en[b], true).contains(m), k << " " << m);
  }
  CHECK(n > 10);
  CHECK(ui_platform() == "mac");
  CHECK(tr("show_finder") == "Show in Finder");
  set_ui_platform("windows");
  CHECK(tr("show_finder") == "Show in Explorer");
  CHECK(tr("tab_dictionary") == "Dictionary");
  {
    Lang de("de");
    CHECK(tr("show_finder") == "Im Explorer zeigen");
  }
  set_ui_platform("mac");
}

TEST_CASE("l10n: the app's language, texts, counts and numbers") {
  CHECK(ui_language("", "de") == "de");
  CHECK(ui_language("", "ja") == "en");
  CHECK(ui_language("fr", "de") == "fr");
  CHECK(ui_language("xx", "pl") == "pl");
  CHECK(ui_language() == "en");
  CHECK(tr("tab_dictionary") == "Dictionary");
  CHECK(tr("card_no_pack", {"French", "Russian"}) == "No language pack from French into Russian yet.");
  CHECK(trn("folder_dicts", 1) == "1 dictionary");
  CHECK(trn("folder_dicts", 12345) == "12,345 dictionaries");
  CHECK(digits(1000) == "1,000");
  {
    Lang ru("ru");
    CHECK(tr("tab_dictionary") == "Словарь");
    CHECK(trn("folder_dicts", 1) == "1 словарь");
    CHECK(trn("folder_dicts", 3) == "3 словаря");
    CHECK(trn("folder_dicts", 11) == "11 словарей");
    CHECK(trn("folder_dicts", 21) == "21 словарь");
    CHECK(trn("folder_dicts", 25) == "25 словарей");
    CHECK(digits(20000) == "20 000");
    CHECK(decimal(1.26) == "1,3");
    CHECK(language_name("de") == "Немецкий");
    CHECK(language_name("kha") == "kha");
    CHECK(size_text(84'000'000) == "84 МБ");
    CHECK(frequency(800) == "в 1 000 самых частых слов");
  }
  {
    Lang pl("pl");
    CHECK(trn("folder_dicts", 2) == "2 słowniki");
    CHECK(trn("folder_dicts", 5) == "5 słowników");
    CHECK(trn("folder_dicts", 22) == "22 słowniki");
    CHECK(digits(5000) == "5000");
  }
  {
    Lang de("de");
    CHECK(digits(5000) == "5.000");
    CHECK(settings_sections()[0] == "Sprachen");
  }
  set_ui_language("xx");
  CHECK(ui_language() == "en");
}

TEST_CASE("l10n: every screen in every language asks only for keys that exist") {
  Settings s;
  SettingsState st;
  st.version = "0.1.305", st.answers = {"ru", "en", "de"}, st.update_url = "https://example.com", st.update_version = "0.2";
  Card c;
  c.kind = Card::Kind::word, c.text = "Haus", c.lang = "de", c.answer = "ru", c.save_text = "Haus";
  c.entries = {{"InkPeek · de → ru", "дом", "", "de-ru", "Haus"}};
  c.methods = {{Method::fast, "", true, ""}, {Method::direct, "", false, "Not installed"}};
  c.method = Method::fast;
  Manifest m;
  for (auto &lang : ui_languages()) {
    Lang l(lang);
    for (auto sec : {SettingsSection::languages, SettingsSection::card, SettingsSection::ai, SettingsSection::general})
      for (int tab = 0; tab < 3; tab++) {
        settings_view(sec, tab, s, st);
        st.has_pair = !st.has_pair, st.trusted = !st.trusted;
        settings_view(sec, tab, s, st);
      }
    for (bool full : {false, true}) {
      auto cs = first_state(c);
      cs.full_entry = full, cs.saved = full;
      card_view(c, cs);
      cs.tab = CardTab::translate;
      card_view(c, cs);
    }
    FirstRunState fr;
    first_run_view(m, {}, fr);
    fr.trusted = true;
    first_run_view(m, {}, fr);
    fr.downloading = true, fr.total = 10;
    first_run_view(m, {}, fr);
    for (auto n : {0, 1, 2, 5}) {
      std::vector<PairRow> rows(static_cast<size_t>(n));
      for (auto &r : rows) r.installed = true, r.level = "basic";
      packs_summary(rows);
      if (n) pair_detail(rows[0]);
    }
    for (auto lv : {"full", "basic", "experimental"}) CHECK(has_text(std::string("level_") + lv));
    for (auto src : {"wikdict", "wiktionary", "freedict"}) CHECK(has_text(std::string("cat_") + src));
    update_check("0.1", 404, "", "");
    update_check("0.1", 200, R"({"tag_name":"v0.2"})", "");
    update_check("0.2", 200, R"({"tag_name":"v0.2"})", "");
  }
  CHECK(missing_texts().empty());
  for (auto &k : missing_texts()) MESSAGE("missing: " << k);
}

TEST_CASE("l10n: every key the core and the Swift and WinUI shells ask for by name exists") {
  static const text::Regex core_key("\\btrn?\\(\"([a-z0-9_]*[a-z0-9])\"");
  static const text::Regex swift_key("\\bL\\.t\\(\"([a-z0-9_]*[a-z0-9])\"");
  int found = 0;
  for (auto &dir : {LEXIPEEK_ROOT "/core/src", LEXIPEEK_ROOT "/win"})
    for (auto &f : files(dir, ".cpp"))
      for (auto &m : core_key.all(read(f))) {
        CHECK_MESSAGE((has_text(m.groups[1]) || has_text(m.groups[1] + ".one")), f.filename().string() << ": " << m.groups[1]);
        found++;
      }
  for (auto &f : files(LEXIPEEK_ROOT "/mac/Sources", ".swift"))
    for (auto &m : swift_key.all(read(f))) {
      CHECK_MESSAGE((has_text(m.groups[1]) || has_text(m.groups[1] + ".one")), f.filename().string() << ": " << m.groups[1]);
      found++;
    }
  CHECK(found > 150);
}

TEST_CASE("l10n: no user-visible English literal left in the Swift shell") {
  // SwiftUI and AppKit labels with a literal, and sentence-like literals anywhere (logs, comments and Snapshot.swift's
  // simulated errors aside).
  static const text::Regex label(
      "\\b(Text|Button|Label|Toggle|Section|TextField|ProgressView|Picker|Menu|Link|LabeledContent|NSMenuItem|item)\\((title: )?\"[^\"]*[A-Za-z]");
  static const text::Regex modifier("\\.(help|alert|confirmationDialog|navigationTitle)\\(\"[^\"]*[A-Za-z]");
  static const text::Regex assign("\\b(title|messageText|informativeText|prompt|message|toolTip|withTitle)\\s*[:=]\\s*\"[^\"]*[A-Za-z]");
  static const text::Regex sentence("\"[A-Z][a-z]+( [a-z]+)+");
  int lines = 0;
  for (auto &f : files(LEXIPEEK_ROOT "/mac/Sources/LexiPeek", ".swift")) {
    std::stringstream in(read(f));
    int n = 0;
    for (std::string line; std::getline(in, line);) {
      n++, lines++;
      auto code = line.substr(0, line.find("//"));
      if (code.find("log.") != std::string::npos || code.find("Logger(") != std::string::npos) continue;
      bool bad = label.all(code).size() + modifier.all(code).size() + assign.all(code).size() > 0;
      if (f.filename() != "Snapshot.swift") bad |= !sentence.all(code).empty();
      CHECK_MESSAGE(!bad, f.filename().string() << ":" << n << ": " << line);
    }
  }
  CHECK(lines > 1000);
}

TEST_CASE("l10n: an argument's own {2} is not filled again") {
  set_ui_language("en");
  CHECK(tr("download_size", {"{2}"}).find("{2}") != std::string::npos);
}
