// app::CardSession's steps as every shell carries them out: sections and their pages, full entries turned, one
// dictionary's entry, tabs, the badge's re-lookup, ☆ checked again, a method and AI for this card only.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <atomic>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include "lexipeek/ai.hpp"
#include "lexipeek/app.hpp"
#include "lexipeek/session.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

namespace {

void be32(std::string &s, uint32_t v) {
  for (int i = 3; i >= 0; i--) s += static_cast<char>((v >> (i * 8)) & 0xff);
}

// <dir>/<id>.*: words (sorted) with their articles, a .rel line per word that has one.
void dictionary(const fs::path &dir, const std::string &id, const std::map<std::string, std::string> &words,
                const std::map<std::string, std::string> &rel = {}) {
  fs::create_directories(dir);
  std::string idx, dict;
  for (auto &[w, a] : words) {
    idx += w + '\0';
    be32(idx, static_cast<uint32_t>(dict.size()));
    be32(idx, static_cast<uint32_t>(a.size()));
    dict += a;
  }
  std::ofstream(dir / (id + ".idx"), std::ios::binary) << idx;
  std::ofstream(dir / (id + ".dict"), std::ios::binary) << dict;
  std::ofstream(dir / (id + ".ifo")) << "StarDict's dict ifo file\nversion=3.0.0\nbookname=" << id << "\nwordcount=" << words.size()
                                     << "\nidxfilesize=" << idx.size() << "\nsametypesequence=h\n";
  if (rel.empty()) return;
  std::ofstream r(dir / (id + ".rel"), std::ios::binary);
  for (auto &[w, line] : rel) r << w << '\t' << line << '\n';
}

void pack(const fs::path &data, const std::string &id, const std::map<std::string, std::string> &words,
          const std::map<std::string, std::string> &rel = {}) {
  dictionary(data / "dicts" / id, id, words, rel);
}

std::string head(const std::string &line) { return "<p class=\"head\">" + line + "</p>"; }

// de → ru with Haus's related words (22: two pages of 20), de → en, an own dictionary; Haus in each.
fs::path data(const std::string &name) {
  auto d = fs::temp_directory_path() / name;
  wipe(d);
  std::map<std::string, std::string> ru{{"Baum", head("der Baum (Baum(e)s, pl. Bäume) — дерево")},
                                        {"Haus", head("das Haus (Hauses, pl. Häuser) — дом, здание")}};
  std::string related;
  for (int i = 0; i < 22; i++) {
    auto w = "Haus" + std::string(1, static_cast<char>('a' + i));
    related += (i ? "|" : "") + w;
    ru[w] = head(w + " — дом " + std::to_string(i));
  }
  pack(d, "dict-de-ru", ru, {{"Haus", related + "\tGebäude\t\t\t[haʊ̯s]\t\tHauses:genitive\t120\t"}});
  pack(d, "dict-de-en", {{"Haus", head("das Haus (Hauses, pl. Häuser) — house")}});
  dictionary(d / "dicts-own" / "Own de-ru", "own", {{"Haus", "<div>дом, жилище</div>"}});
  return d;
}

void settle(app::CardSession &s) {
  until([&] { return s.poll(), !s.busy() && !s.looking(); });
  s.poll();
}

}  // namespace

TEST_CASE("card session: a section and its pages, full entries turned, one dictionary's entry, ← each back") {
  set_ui_language("en");
  auto d = data("lexipeek-session-steps");
  {
    auto e = app::Engine::make(d.string(), app::device(16384, {"asimddp"}, "arm64"));
    auto s = app::CardSession::make(e);
    s.look("Haus", Source::hotkey);
    settle(s);
    REQUIRE(s.has_card());
    REQUIRE(s.nav().card().entries.size() == 2);
    CHECK(s.nav().card().entries[0].dictionary == "InkPeek · de → ru");
    CHECK(s.nav().card().entries[1].dictionary == "own");
    CHECK(s.nav().state.tab == CardTab::dictionary);

    // A section: its first page comes from the look-up queue, › asks the next one, ← back to the entry.
    s.open_section(0, "rel");
    CHECK(s.busy());
    settle(s);
    auto v = s.view();
    CHECK(s.nav().state.section.key == "rel");
    CHECK(v.pager == "1–20 of 22");
    REQUIRE(v.rows.size() == 21);
    CHECK(v.rows[0].text == "Haus · Related words");
    CHECK(v.rows[1].label == "Hausa");
    CHECK(v.rows[1].text == "— дом 0");
    CHECK(v.next);
    s.turn(1);
    settle(s);
    CHECK(s.view().pager == "21–22 of 22");
    CHECK(s.view().rows.size() == 3);
    s.turn(1);  // no page after the last
    CHECK(!s.busy());
    s.back();
    settle(s);
    CHECK(s.nav().state.section.key.empty());
    CHECK(s.nav().card().text == "Haus");
    // ← while a page is on its way: the page does not come back over the entry.
    s.open_section(0, "rel");
    s.back();
    settle(s);
    CHECK(s.nav().state.section.key.empty());

    // Full entries: one dictionary at a time, ‹ › to the other.
    s.full_entries();
    CHECK(s.poll());
    CHECK(s.nav().state.full_entry);
    CHECK(s.view().pager == "1 / 2");
    s.turn(1);
    CHECK(s.view().pager == "2 / 2");
    s.turn(1);
    CHECK(s.view().pager == "2 / 2");
    s.turn(-1);
    CHECK(s.view().pager == "1 / 2");

    // One dictionary's line clicked: its entry alone, ← back to all of them.
    s.open_entry(1);
    CHECK(s.poll());
    CHECK(s.nav().state.entry_open == 1);
    CHECK(s.view().back == "All dictionaries");
    s.back();
    CHECK(s.nav().state.entry_open == -1);

    // Tabs: Translate and back, round both ways.
    s.select(CardTab::translate);
    CHECK(s.nav().state.tab == CardTab::translate);
    s.next_tab(1);
    CHECK(s.nav().state.tab == CardTab::dictionary);
    s.next_tab(-1);
    CHECK(s.nav().state.tab == CardTab::translate);
    // ← on Translate is the card before: none here, the card stays.
    s.back();
    CHECK(s.has_card());
  }
  wipe(d);
}

TEST_CASE("card session: the badge's re-lookup, ☆ checked again after Notes, this card's method") {
  set_ui_language("en");
  auto d = data("lexipeek-session-badge");
  {
    auto e = app::Engine::make(d.string(), app::device(16384, {"asimddp"}, "arm64"));
    auto s = app::CardSession::make(e);
    s.look("Haus", Source::hotkey);
    settle(s);
    REQUIRE(s.has_card());
    CHECK(s.nav().card().answer == "ru");
    CHECK(s.view().badge == "de → ru");

    // The answer language picked in the badge's menu: the text looked up again, its ← is the card before.
    s.relookup("", "en");
    settle(s);
    CHECK(s.nav().card().answer == "en");
    CHECK(s.nav().card().text == "Haus");
    CHECK(s.view().badge == "de → en");
    REQUIRE(!s.nav().card().entries.empty());
    CHECK(s.nav().card().entries[0].line.find("house") != std::string::npos);
    // Picked again: the answer language back (a pick replaces the card, ← is not it).
    s.relookup("", "ru");
    settle(s);
    CHECK(s.nav().card().answer == "ru");

    // ☆, then the note removed in Notes: the card's ★ follows when checked again.
    s.toggle_saved();
    CHECK(s.nav().state.saved);
    CHECK(s.notes_changed());
    s.poll();
    s.recheck_saved();
    CHECK(!s.poll());  // nothing changed
    e.remove_note(NotesTab::words, "Haus");
    s.recheck_saved();
    CHECK(s.poll());
    CHECK(!s.nav().state.saved);
    CHECK(!s.view().saved);

    // A method picked for this card: no model here, so its translation comes back as an error, nothing left waiting.
    s.use(Method::fast);
    CHECK(s.nav().state.method == Method::fast);
    CHECK(s.nav().state.translating);
    settle(s);
    CHECK(!s.nav().state.translating);
    CHECK(s.nav().state.translated);
    CHECK(!s.nav().state.translation.ok());
    CHECK(app::settings(d.string()).method != Method::fast);  // Settings → Translation's method is the shell's to save
    // No card: use() asks nothing.
    s.reset();
    s.use(Method::direct);
    CHECK(!s.busy());
  }
  wipe(d);
}

TEST_CASE("card session: AI translation picked in the card, the provider's key per card, a closed card's answer dropped") {
  set_ui_language("en");
  auto d = data("lexipeek-session-ai");
  {
    std::ofstream(d / "settings.json") << R"({"ai": {"provider": "claude-cli", "translate": true, "prefetch": false}})";
  }
  std::atomic<int> calls{0}, done{0};
  std::atomic<bool> go{true};
  ai::set_cli_runner([&](const ai::CliCall &) {
    calls++;
    until([&] { return go.load(); }, 10);
    done++;
    nlohmann::json body{{"base", "Haus"}, {"article", "das"}, {"plural", ""}, {"pos", ""}, {"forms", ""}, {"irregular", false},
                        {"form_of", ""}, {"meanings", {"дом"}}, {"translation", ""}};
    return ai::CliRan{0, nlohmann::json{{"type", "result"}, {"is_error", false}, {"result", body.dump()}}.dump(), ""};
  });
  {
    auto e = app::Engine::make(d.string(), app::device(16384, {"asimddp"}, "arm64"));
    auto s = app::CardSession::make(e);
    s.look("Haus", Source::hotkey);
    settle(s);
    REQUIRE(s.has_card());
    CHECK(calls == 0);  // only on the person's action
    CHECK(s.nav().card().ai.translate);
    s.select(CardTab::translate);
    s.choose_ai();
    CHECK(s.nav().state.by_ai);
    CHECK(s.nav().state.quick.asking);
    settle(s);
    CHECK(calls == 1);
    CHECK(s.nav().state.quick.ok);
    CHECK(s.nav().state.quick.json.find("дом") != std::string::npos);
    // Again from the store, no request.
    s.look("Haus", Source::hotkey);
    settle(s);
    s.select(CardTab::translate);
    s.choose_ai();
    settle(s);
    CHECK(calls == 1);
    CHECK(s.nav().state.quick.saved);

    // A closed card: its answer on its way is dropped, the card waits for nothing.
    go = false;
    s.look("Baum", Source::hotkey);
    settle(s);
    s.select(CardTab::translate);
    s.choose_ai();
    CHECK(s.busy());
    REQUIRE(until([&] { return calls == 2; }));  // the request is out, its answer held back
    s.cancel();
    CHECK(!s.busy());
    CHECK(!s.nav().state.quick.asking);
    go = true;
    REQUIRE(until([&] { return done == 2; }));
    until([&] { return s.poll(), false; }, 1);  // what it posts comes in now: dropped
    CHECK(!s.nav().state.quick.answered);
  }
  ai::set_cli_runner(nullptr);
  {
    // A provider that needs a key: none in the shell's store → the card says so and sends nothing (a word no answer is kept for).
    std::ofstream(d / "settings.json") << R"({"ai": {"provider": "anthropic", "translate": true, "prefetch": false}})";
  }
  {
    auto e = app::Engine::make(d.string(), app::device(16384, {"asimddp"}, "arm64"));
    auto s = app::CardSession::make(e);
    s.look("Hausb", Source::hotkey);
    settle(s);
    REQUIRE(s.has_card());
    CHECK(s.nav().card().ai.no_key);
    s.select(CardTab::translate);
    s.choose_ai();
    settle(s);
    CHECK(!s.nav().state.quick.ok);
    CHECK(s.nav().state.quick.error.find("key") != std::string::npos);
    s.set_key("sk-test");
    s.look("Hausb", Source::hotkey);
    settle(s);
    CHECK(!s.nav().card().ai.no_key);
  }
  wipe(d);
}
