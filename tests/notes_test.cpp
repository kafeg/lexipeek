// ☆ (what a card saves, as InkPeek's saveKey) and Notes (Words · Phrases · History) on a small pack dictionary.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <fstream>
#include <thread>
#include "lexipeek/app.hpp"
#include "lexipeek/session.hpp"
#include "lexipeek/core.hpp"
#include "lexipeek/views.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

namespace {

void be32(std::string &s, uint32_t v) {
  for (int i = 3; i >= 0; i--) s += static_cast<char>((v >> (i * 8)) & 0xff);
}

// <data>/dicts/dict-de-ru: Haus (form Häusern), Baum; articles with the packs' head line.
fs::path pack_data(const std::string &name) {
  auto d = fs::temp_directory_path() / name;
  wipe(d);
  auto dir = d / "dicts" / "dict-de-ru";
  fs::create_directories(dir);
  std::vector<std::pair<std::string, std::string>> words{
      {"Baum", "<p class=\"head\">der Baum (Baum(e)s, pl. Bäume) — дерево</p>"},
      {"Haus", "<p class=\"head\">das Haus (Hauses, pl. Häuser) — дом, здание</p>"}};
  std::string idx, dict;
  for (auto &[w, a] : words) {
    idx += w + '\0';
    be32(idx, static_cast<uint32_t>(dict.size()));
    be32(idx, static_cast<uint32_t>(a.size()));
    dict += a;
  }
  std::string syn = std::string("Häusern") + '\0';
  be32(syn, 1);
  std::ofstream(dir / "dict-de-ru.idx", std::ios::binary) << idx;
  std::ofstream(dir / "dict-de-ru.dict", std::ios::binary) << dict;
  std::ofstream(dir / "dict-de-ru.syn", std::ios::binary) << syn;
  std::ofstream(dir / "dict-de-ru.ifo") << "StarDict's dict ifo file\nversion=3.0.0\nbookname=de-ru\nwordcount=2\nsynwordcount=1\nidxfilesize="
                                         << idx.size() << "\nsametypesequence=h\n";
  return d;
}

}  // namespace

TEST_CASE("☆: a word as its base form, also after its article; a phrase as it is") {
  auto d = pack_data("lexipeek-notes-save");
  {
    CoreOptions o;
    o.data = d;
    Core core(o);
    auto c = core.lookup(Request{.text = "Häusern", .source = Source::hotkey});
    REQUIRE(c.kind == Card::Kind::word);
    CHECK(c.save_text == "Haus");
    CHECK(c.save_kind == "word");
    CHECK(c.save_line == "das Haus (Hauses, pl. Häuser) — дом, здание");
    CHECK(!c.saved);
    CHECK(!first_state(c).saved);
    CHECK(!card_view(c, first_state(c)).saved);

    CHECK(core.toggle_saved(c, "дома"));
    CHECK(core.store().is_saved("Haus"));
    CHECK(!core.store().is_saved("Häusern"));
    REQUIRE(core.store().saved().size() == 1);
    CHECK(core.store().saved()[0].meaning == "das Haus (Hauses, pl. Häuser) — дом, здание");
    auto again = core.lookup(Request{.text = "Haus", .source = Source::hotkey});
    CHECK(again.saved);
    auto s = first_state(again);
    CHECK(s.saved);
    CHECK(card_view(again, s).saved);
    CHECK(card_view(again, s).star_help == "Remove Haus from Notes");

    auto article = core.lookup(Request{.text = "das Haus", .source = Source::hotkey});
    CHECK(article.kind == Card::Kind::phrase);
    CHECK(article.save_text == "Haus");
    CHECK(article.save_kind == "word");
    CHECK(article.saved);
    CHECK(!core.toggle_saved(article, ""));
    CHECK(!core.store().is_saved("Haus"));

    // Not the pack's header: "ein Haus" is a phrase of its own.
    auto other = core.lookup(Request{.text = "ein Haus", .source = Source::hotkey});
    CHECK(other.save_text == "ein Haus");
    CHECK(other.save_kind == "phrase");
    CHECK(other.save_line.empty());
    CHECK(core.toggle_saved(other, "один дом"));
    CHECK(core.store().saved()[0].meaning == "один дом");

    // A word the dictionaries do not know is saved as asked.
    auto unknown = core.lookup(Request{.text = "Fenster", .source = Source::hotkey});
    CHECK(unknown.save_text == "Fenster");
    CHECK(unknown.save_kind == "word");

    // Saved as the form by an older version: ★, and ☆ removes both.
    core.store().save("Häusern", "word", "ru", "дома");
    auto form = core.lookup(Request{.text = "Häusern", .source = Source::hotkey});
    CHECK(form.saved);
    CHECK(!core.toggle_saved(form, ""));
    CHECK(!core.store().is_saved("Häusern"));
  }
  wipe(d);
}

TEST_CASE("notes: three lists, newest first, a line for each; remove and clear") {
  auto d = fs::temp_directory_path() / "lexipeek-notes-view";
  wipe(d);
  {
    Store st({d, {}, {}});
    auto v = notes_view(st, NotesTab::words, "ru");
    CHECK(v.tabs == std::vector<std::string>{"Words (0)", "Phrases (0)", "History (0)"});
    CHECK(v.rows.empty());
    CHECK(!v.empty.empty());
    CHECK(!v.clear);

    // Looks a few ms apart: the history's order is by the look's time.
    auto look = [&](const std::string &t, const std::string &kind, const std::string &line) {
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      st.seen(t, kind, "ru", line);
    };
    look("Haus", "word", "das Haus — дом");
    look("Haus", "word", "das Haus — дом");
    look("Ich mache die Tür zu", "phrase", "");
    st.put_answer("Ich mache die Tür zu", "ru", "offline-de", 7, "bergamot", "Я закрываю дверь");
    look("Wort", "word", "");
    st.save("Haus", "word", "ru", "das Haus — дом");
    st.save("Baum", "word", "ru", "der Baum — дерево");
    st.save("Ich mache die Tür zu", "phrase", "ru", "");

    v = notes_view(st, NotesTab::words, "ru");
    CHECK(v.tabs == std::vector<std::string>{"Words (2)", "Phrases (1)", "History (3)"});
    REQUIRE(v.rows.size() == 2);
    CHECK(v.rows[0].text == "Baum");
    CHECK(v.rows[0].line == "der Baum — дерево");
    CHECK(v.rows[0].looks.empty());
    CHECK(v.rows[1].text == "Haus");
    CHECK(v.empty.empty());
    CHECK(!v.clear);

    v = notes_view(st, NotesTab::phrases, "ru");
    REQUIRE(v.rows.size() == 1);
    CHECK(v.rows[0].line == "Я закрываю дверь");  // no saved line: the cached translation

    v = notes_view(st, NotesTab::history, "ru");
    REQUIRE(v.rows.size() == 3);
    CHECK(v.rows[0].text == "Wort");
    CHECK(v.rows[0].line == "—");
    CHECK(v.rows[2].text == "Haus");
    CHECK(v.rows[2].looks == "2×");
    CHECK(v.clear);

    CHECK(remove_note(st, NotesTab::history, "Wort").empty());
    CHECK(notes_view(st, NotesTab::history, "ru").rows.size() == 2);
    CHECK(remove_note(st, NotesTab::words, "Baum").empty());
    CHECK(!st.is_saved("Baum"));
    CHECK(st.clear_history().empty());
    v = notes_view(st, NotesTab::history, "ru");
    CHECK(v.rows.empty());
    CHECK(!v.clear);
    CHECK(notes_view(st, NotesTab::words, "ru").tabs[0] == "Words (1)");
  }
  wipe(d);
}

TEST_CASE("app: notes through the engine") {
  auto d = pack_data("lexipeek-notes-engine");
  {
    auto e = app::Engine::make(d.string(), app::device(8192, {"asimddp"}, "arm64"));
    auto c = e.lookup("Baum", Source::hotkey);
    CHECK(e.toggle_saved(c, ""));
    CHECK(e.saved(c));
    auto v = e.notes(NotesTab::words, 0);
    REQUIRE(v.rows.size() == 1);
    CHECK(v.rows[0].text == "Baum");
    CHECK(e.notes(NotesTab::history, 0).rows.size() == 1);
    e.remove_note(NotesTab::words, "Baum");
    CHECK(e.notes(NotesTab::words, 0).rows.empty());
    e.clear_history();
    CHECK(e.notes(NotesTab::history, 0).rows.empty());
  }
  wipe(d);
}

TEST_CASE("notes: TSV for Anki — text, meaning, AI translation, language, date; cells on one line") {
  auto d = fs::temp_directory_path() / "lexipeek-tsv";
  wipe(d);
  Store s(StoreOptions{d, {}, "mac-t"});
  s.save("Haus", "word", "ru", "дом,\tздание");
  s.put_answer("Haus", "Russian", "quick", 1, "m", "{\"translation\":\"дом\\nи двор\"}");
  s.save("Ich mache die Tür zu", "phrase", "ru", "");
  auto tsv = notes_tsv(s, NotesTab::words, "ru");
  auto tab = tsv.find('\t');
  REQUIRE(tab != std::string::npos);
  CHECK(tsv.starts_with("Haus\tдом, здание\tдом и двор\tru\t20"));
  CHECK(tsv.ends_with("\n"));
  CHECK(std::count(tsv.begin(), tsv.end(), '\n') == 1);
  CHECK(std::count(tsv.begin(), tsv.end(), '\t') == 4);
  CHECK(notes_tsv(s, NotesTab::phrases, "ru").starts_with("Ich mache die Tür zu\t\t\tru\t"));
  CHECK(notes_tsv(s, NotesTab::history, "ru").empty());
}

TEST_CASE("notes: 50 a page, the pager; TSV takes them all") {
  auto d = fs::temp_directory_path() / "lexipeek-notes-pages";
  wipe(d);
  Store s(StoreOptions{d, {}, "mac-p"});
  for (int i = 0; i < 120; i++) s.seen("w" + std::to_string(i), "word", "ru", "m");
  auto v = notes_view(s, NotesTab::history, "ru");
  CHECK(v.rows.size() == 50);
  CHECK(v.pages == 3);
  CHECK(v.pager == "1–50 of 120");
  v = notes_view(s, NotesTab::history, "ru", 2);
  CHECK(v.rows.size() == 20);
  CHECK(v.pager == "101–120 of 120");
  CHECK(notes_view(s, NotesTab::history, "ru", 9).page == 2);
  auto tsv = notes_tsv(s, NotesTab::history, "ru");
  CHECK(std::count(tsv.begin(), tsv.end(), '\n') == 120);
}

TEST_CASE("app: a card session — a look-up arrives at a poll, ☆, a word opened and ←, a newer look-up wins") {
  auto d = pack_data("lexipeek-session");
  {
    auto e = app::Engine::make(d.string(), app::device(8192, {"asimddp"}, "arm64"));
    auto s = app::CardSession::make(e);
    auto settle = [&] {
      for (int i = 0; i < 500 && (s.busy() || s.looking()); i++) {
        s.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      s.poll();
    };
    s.look("Haus", Source::hotkey, "", "");
    CHECK(s.looking());
    settle();
    REQUIRE(s.has_card());
    CHECK(s.nav().card().text == "Haus");
    CHECK(s.notes_changed());  // its history
    CHECK(!s.notes_changed());
    s.toggle_saved();
    CHECK(s.view().saved);
    CHECK(s.notes_changed());
    s.open("Baum");
    settle();
    CHECK(s.nav().card().text == "Baum");
    CHECK(s.view().back == "Haus");
    s.back();
    settle();
    CHECK(s.nav().card().text == "Haus");
    CHECK(s.view().saved);
    // Two look-ups: the later one's card is shown.
    s.look("Baum", Source::hotkey, "", "");
    s.look("Haus", Source::hotkey, "", "");
    settle();
    CHECK(s.nav().card().text == "Haus");
    s.reset();
    CHECK(s.poll());
    CHECK(!s.has_card());
  }
  wipe(d);
}
