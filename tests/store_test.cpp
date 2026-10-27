// The store: InkPeek's sync cases, the clock, the log's last line, the store's API.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <algorithm>
#include <fstream>
#include <random>
#include <nlohmann/json.hpp>
#include "lexipeek/store.hpp"
#include <sqlite3.h>
#include "store/log.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path fresh(const std::string &name) {
  auto d = fs::temp_directory_path() / name;
  wipe(d);
  fs::create_directories(d);
  return d;
}

std::string slurp(const fs::path &f) {
  std::ifstream in(f, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

// The lists sorted by their keys: compared as sets.
json by_key(json j) {
  auto key = [](const json &e) { return e.value("text", "") + "\n" + e.value("lang", "") + "\n" + e.value("section", "") + "\n" +
                                        std::to_string(e.value("ver", 0)); };
  for (auto k : {"saved", "history", "ai"})
    std::sort(j[k].begin(), j[k].end(), [&](const json &a, const json &b) { return key(a) < key(b); });
  return j;
}

void check_case(const fs::path &dir, const json &got) {
  auto want = by_key(json::parse(slurp(dir / "expected.json")));
  auto have = by_key(got);
  for (auto k : {"saved", "history", "ai"}) {
    INFO(dir.filename().string(), " ", k, "\nwant: ", want[k].dump(), "\nhave: ", have[k].dump());
    CHECK(want[k] == have[k]);
  }
}

std::vector<fs::path> cases() {
  std::vector<fs::path> out;
  for (auto &e : fs::directory_iterator(fs::path(LEXIPEEK_INKPEEK) / "tests" / "sync"))
    if (fs::exists(e.path() / "expected.json")) out.push_back(e.path());
  std::sort(out.begin(), out.end());
  return out;
}

// Every *.jsonl in [from] copied to a new folder.
fs::path copy_logs(const fs::path &from, const std::string &name) {
  auto to = fresh(name);
  for (auto &e : fs::directory_iterator(from))
    if (e.path().extension() == ".jsonl") fs::copy_file(e.path(), to / e.path().filename());
  return to;
}

std::string state(const fs::path &folder) { return records_json(merge_folder(folder)); }

}  // namespace

TEST_CASE("sync cases: the merge") {
  auto all = cases();
  REQUIRE(all.size() >= 7);
  for (auto &d : all) check_case(d, json::parse(records_json(merge_folder(d))));
}

TEST_CASE("sync cases: the store's SQLite view") {
  for (auto &d : cases()) {
    auto data = fresh("lexipeek-store-case");
    fs::create_directories(data / "log");
    for (auto &e : fs::directory_iterator(d))
      if (e.path().extension() == ".jsonl") fs::copy_file(e.path(), data / "log" / e.path().filename());
    Store st({data, {}, "test-0000"});
    REQUIRE(st.error() == "");
    check_case(d, json::parse(records_json(st.records())));
  }
}

TEST_CASE("clock: grows when the wall clock goes back, follows a newer one read") {
  store::Clock c;
  CHECK(c.tick(1000) == store::Time{1000, 0});
  CHECK(c.tick(1000) == store::Time{1000, 1});
  CHECK(c.tick(900) == store::Time{1000, 2});
  CHECK(c.tick(1001) == store::Time{1001, 0});
  c.see({5000, 7});
  CHECK(c.tick(1002) == store::Time{5000, 8});
  c.see({10, 0});
  CHECK(c.tick(6000) == store::Time{6000, 0});
}

TEST_CASE("lines: what is skipped") {
  CHECK(store::parse(R"({"v":1,"t":[1,2],"d":"a","op":"saved.del","text":"x"})"));
  CHECK(!store::parse(R"({"v":2,"t":[1,2],"d":"a","op":"saved.del","text":"x"})"));
  CHECK(!store::parse(R"({"v":1,"t":[1],"d":"a","op":"saved.del","text":"x"})"));
  CHECK(!store::parse(R"({"v":1,"t":[1,2.5],"d":"a","op":"saved.del","text":"x"})"));
  CHECK(!store::parse(R"({"v":1,"t":[1,2],"op":"saved.del","text":"x"})"));
  CHECK(!store::parse(R"({"v":1,"t":[1,2],"d":"a","op":"note.put","text":"x"})"));
  CHECK(!store::parse(R"({"v":1,"t":[1,2],"d":"a","op":"saved.del"})"));
  CHECK(!store::parse(R"([1,2])"));
  CHECK(!store::parse(R"({"v":1,"t":[1,2)"));
  store::Line l;
  l.t = {5, 1}, l.d = "mac-0001", l.op = store::Op::history_seen, l.text = "Häuser", l.kind = "word", l.lang = "ru";
  l.meaning = "дома", l.base = "Haus", l.c = 3;
  auto s = store::format(l);
  CHECK(s == R"({"v":1,"t":[5,1],"d":"mac-0001","op":"history.seen","text":"Häuser","kind":"word","lang":"ru","meaning":"дома","base":"Haus","c":3})");
  auto back = store::parse(s);
  REQUIRE(back);
  CHECK(back->base == "Haus");
  CHECK(back->c == 3);
}

TEST_CASE("append cuts a broken last line") {
  auto d = fresh("lexipeek-store-append");
  auto f = d / "a.jsonl";
  CHECK(store::append(f, "one") == "");
  std::ofstream(f, std::ios::app) << "{\"broken";
  CHECK(store::lines_of(f).size() == 1);
  CHECK(store::append(f, "two") == "");
  CHECK(slurp(f) == "one\ntwo\n");
  std::ofstream(f, std::ios::trunc) << "no newline at all";
  CHECK(store::append(f, "three") == "");
  CHECK(slurp(f) == "three\n");
}

TEST_CASE("store: looks, saved, answers") {
  auto data = fresh("lexipeek-store-api");
  Store st({data, {}, {}});
  REQUIRE(st.error() == "");
  auto device = st.device();
  CHECK(device.find('-') != std::string::npos);
  CHECK(slurp(data / "device") == device + "\n");

  CHECK(st.seen("Haus", "word", "ru", "дом") == "");
  CHECK(st.seen("Haus", "word", "ru", "дом") == "");
  CHECK(st.seen("Häuser", "word", "ru", "дома", "Haus") == "");
  auto h = st.history();
  REQUIRE(h.size() == 2);
  CHECK(h[0].text == "Häuser");
  CHECK(h[0].base == "Haus");
  CHECK(h[1].count == 2);
  CHECK(st.looks("Haus") == 3);
  auto r = st.recents(5);
  REQUIRE(r.size() == 1);
  CHECK(r[0].text == "Häuser");
  auto log = store::lines_of(data / "log" / (device + ".jsonl"));
  REQUIRE(log.size() == 3);
  CHECK(store::parse(log[1])->c == 2);

  CHECK(st.put_answer("Häuser", "ru", "quick", 3, "m", "{\"translation\":\"дома\"}") == "");
  CHECK(st.answer("Häuser", "ru", "quick", 3) == "{\"translation\":\"дома\"}");
  CHECK(!st.answer("Häuser", "ru", "quick", 2));
  CHECK(st.save("Haus", "word", "ru", "дом", "Häuser") == "");
  CHECK(st.is_saved("Haus"));
  CHECK(st.answer("Haus", "ru", "quick", 3) == "{\"translation\":\"дома\"}");
  CHECK(st.put_answer("Häuser", "ru", "analysis", 1, "m", "{}") == "");
  CHECK(st.answer("Haus", "ru", "analysis", 1) == "{}");
  REQUIRE(st.saved().size() == 1);

  CHECK(st.forget("Häuser") == "");
  CHECK(!st.answer("Häuser", "ru", "quick", 3));
  CHECK(st.answer("Haus", "ru", "quick", 3));
  CHECK(st.seen("Häuser", "word", "ru", "дома", "Haus") == "");
  CHECK(st.history()[0].count == 1);
  CHECK(st.unsave("Haus") == "");
  CHECK(!st.is_saved("Haus"));
  CHECK(st.answer("Haus", "ru", "quick", 3));  // Haus is still in the history

  auto before = st.records();
  Store again({data, {}, {}});
  CHECK(again.device() == device);
  CHECK(json::parse(records_json(again.records())) == json::parse(records_json(before)));

  CHECK(again.clear_history() == "");
  CHECK(again.history().empty());
  CHECK(again.records().ai.empty());
}

TEST_CASE("store: another device's newer change, then ours") {
  auto data = fresh("lexipeek-store-merge");
  Store st({data, {}, "mac-0001"});
  REQUIRE(st.error() == "");
  CHECK(st.save("Haus", "word", "ru", "дом") == "");
  // Another device, its clock far ahead: its delete is newer than our save.
  std::ofstream(data / "log" / "boox-0002.jsonl")
      << R"({"v":1,"t":[99999999999999,4],"d":"boox-0002","op":"saved.del","text":"Haus"})" << "\n"
      << R"({"v":1,"t":[99999999999999,5],"d":"boox-0002","op":"history.seen","text":"Haus","kind":"word","lang":"en","meaning":"house","c":4})"
      << "\n";
  CHECK(st.is_saved("Haus"));
  CHECK(st.merge() == "");
  CHECK(!st.is_saved("Haus"));
  CHECK(st.looks("Haus") == 4);
  CHECK(st.save("Haus", "word", "ru", "дом") == "");  // made after reading it: wins
  CHECK(st.is_saved("Haus"));
  CHECK(st.seen("Haus", "word", "ru", "дом") == "");
  CHECK(st.looks("Haus") == 5);
  CHECK(st.history()[0].lang == "ru");
  auto own = store::lines_of(data / "log" / "mac-0001.jsonl");
  REQUIRE(own.size() == 3);
  CHECK(store::parse(own[1])->t == store::Time{99999999999999, 6});
}

TEST_CASE("store: the history keeps the newest entries") {
  auto data = fresh("lexipeek-store-limit");
  Store st({data, {}, "mac-0001"});
  REQUIRE(st.error() == "");
  CHECK(st.seen("w0", "word", "ru", "") == "");
  CHECK(st.put_answer("w0", "ru", "quick", 1, "m", "a") == "");
  CHECK(st.answer("w0", "ru", "quick", 1));
  for (int i = 1; i <= Store::history_limit; i++) REQUIRE(st.seen("w" + std::to_string(i), "word", "ru", "") == "");
  CHECK(st.history().size() == size_t(Store::history_limit));
  CHECK(st.history(1)[0].text == "w1000");
  CHECK(st.looks("w0") == 0);
  CHECK(st.looks("w1") == 1);
  CHECK(!st.answer("w0", "ru", "quick", 1));
  Store again({data, {}, "mac-0001"});
  CHECK(again.history().size() == size_t(Store::history_limit));
}

TEST_CASE("compaction: the sync cases merge the same with any one file compacted") {
  for (auto &d : cases())
    for (auto &f : fs::directory_iterator(d)) {
      if (f.path().extension() != ".jsonl") continue;
      auto logs = copy_logs(d, "lexipeek-store-compact-case");
      auto file = logs / f.path().filename();
      std::vector<store::Line> own;
      for (auto &s : store::lines_of(file))
        if (auto l = store::parse(s)) own.push_back(*l);
      std::stable_sort(own.begin(), own.end(), store::before);
      std::ofstream out(file, std::ios::trunc);
      for (auto &l : store::compact(own)) out << store::format(l) << "\n";
      out.close();
      INFO(d.filename().string(), " ", f.path().filename().string());
      check_case(d, json::parse(state(logs)));
    }
}

TEST_CASE("compaction: a long log of two devices merges the same, the clock goes on") {
  auto data = fresh("lexipeek-store-compact");
  auto logs = data / "log";
  std::mt19937 rnd(7);
  const std::vector<std::string> words = {"Haus", "Häuser", "Baum", "gehen", "ging", "das Haus", "Weg", "alt"};
  {
    Store a({data / "a", logs, "mac-0001"}), b({data / "b", logs, "boox-0002"});
    REQUIRE(a.error() == "");
    REQUIRE(b.error() == "");
    for (int i = 0; i < 3000; i++) {
      auto &st = rnd() % 3 ? a : b;
      auto &w = words[rnd() % words.size()];
      auto op = rnd() % 100;
      if (op < 80) REQUIRE(st.seen(w, "word", "ru", "m" + std::to_string(i), w == "Häuser" ? "Haus" : "") == "");
      else if (op < 86) REQUIRE(st.save(w, "word", "ru", "s" + std::to_string(i)) == "");
      else if (op < 90) REQUIRE(st.unsave(w) == "");
      else if (op < 95) REQUIRE(st.put_answer(w, "ru", rnd() % 2 ? "quick" : "analysis", rnd() % 2, "m", std::to_string(i)) == "");
      else if (op < 99) REQUIRE(st.forget(w) == "");
      else REQUIRE(st.clear_history() == "");
      if (rnd() % 20 == 0) REQUIRE((rnd() % 2 ? a : b).merge() == "");
    }
  }
  auto own = logs / "mac-0001.jsonl", other = logs / "boox-0002.jsonl";
  auto before = state(logs);
  auto other_bytes = slurp(other);
  auto old_lines = store::lines_of(own).size();
  store::Time last;
  for (auto &l : store::read_folder(logs)) last = std::max(last, l.t);

  Store st({data, logs, "mac-0001"});
  REQUIRE(st.error() == "");
  auto new_lines = store::lines_of(own).size();
  MESSAGE(old_lines, " lines compacted to ", new_lines);
  CHECK(new_lines * 2 <= old_lines);
  CHECK(slurp(other) == other_bytes);
  CHECK(!fs::exists(fs::path(own).concat(".tmp")));
  CHECK(state(logs) == before);
  CHECK(by_key(json::parse(records_json(st.records()))) == by_key(json::parse(before)));

  CHECK(store::compact_file(other, 0) == "");
  CHECK(state(logs) == before);
  CHECK(store::compact_file(own, 0) == "");  // twice: nothing left to drop, the file stays
  CHECK(store::lines_of(own).size() == new_lines);

  CHECK(st.seen("Haus", "word", "ru", "дом") == "");
  auto tail = store::parse(store::lines_of(own).back());
  REQUIRE(tail);
  CHECK(last < tail->t);
}

TEST_CASE("compaction: a small log stays as it is") {
  auto data = fresh("lexipeek-store-compact-small");
  {
    Store st({data, {}, "mac-0001"});
    for (int i = 0; i < 500; i++) REQUIRE(st.seen("Haus", "word", "ru", "дом") == "");
  }
  auto own = data / "log" / "mac-0001.jsonl";
  auto bytes = slurp(own);
  Store st({data, {}, "mac-0001"});
  CHECK(slurp(own) == bytes);
  CHECK(st.looks("Haus") == 500);
  CHECK(store::compact_file(own, 0) == "");
  CHECK(store::lines_of(own).size() == 1);
  CHECK(Store({data, {}, "mac-0001"}).looks("Haus") == 500);
}

TEST_CASE("backup: export, then import into another store adds what it lacks, with the dates") {
  auto a = fresh("lexipeek-backup-a"), b = fresh("lexipeek-backup-b");
  {
    Store s(StoreOptions{a, {}, "mac-a"});
    s.save("Haus", "word", "ru", "дом");
    s.seen("Haus", "word", "ru", "дом");
    s.seen("Haus", "word", "ru", "дом");
    s.put_answer("Haus", "Russian", "quick", 7, "claude", "{\"translation\":\"дом\"}");
    CHECK(s.backup(a / "copy.db").empty());
  }
  REQUIRE(fs::exists(a / "copy.db"));
  CHECK(!fs::exists(a / "copy.db.tmp"));
  Store t(StoreOptions{b, {}, "mac-b"});
  t.save("Baum", "word", "ru", "дерево");
  auto r = t.restore(a / "copy.db");
  CHECK(r.error.empty());
  CHECK(r.added == 3);  // saved, history, ai
  CHECK(t.is_saved("Haus"));
  CHECK(t.is_saved("Baum"));
  CHECK(t.looks("Haus") == 2);
  CHECK(t.answer("Haus", "Russian", "quick", 7) == "{\"translation\":\"дом\"}");
  // Again: nothing new.
  CHECK(t.restore(a / "copy.db").added == 0);
  // The dates came along: Haus was saved before Baum.
  auto saved = t.saved();
  REQUIRE(saved.size() == 2);
  CHECK(saved[0].text == "Baum");
  // Not a backup.
  std::ofstream(b / "list.tsv") << "Haus\tдом\n";
  CHECK(t.restore(b / "list.tsv").error == "not a backup");
}

TEST_CASE("backup: an older backup without some columns, a removed word comes back now") {
  auto a = fresh("lexipeek-backup-old");
  sqlite3 *db = nullptr;
  sqlite3_open((a / "old.db").string().c_str(), &db);
  sqlite3_exec(db,
               "CREATE TABLE saved(text TEXT PRIMARY KEY, kind TEXT, meaning TEXT, created INTEGER);"
               "CREATE TABLE history(text TEXT PRIMARY KEY, kind TEXT, seen INTEGER, count INTEGER);"
               "CREATE TABLE ai(text TEXT, lang TEXT, section TEXT, ver INTEGER, model TEXT, answer TEXT, created INTEGER);"
               "INSERT INTO saved VALUES('Tür', 'word', 'дверь', 1000);"
               "INSERT INTO history VALUES('Tür', 'word', 1000, 4);",
               nullptr, nullptr, nullptr);
  sqlite3_close(db);
  Store s(StoreOptions{a / "store", {}, "mac-c"});
  s.save("Tür", "word", "ru", "дверь");
  s.unsave("Tür");
  auto r = s.restore(a / "old.db");
  CHECK(r.error.empty());
  CHECK(r.added == 2);
  CHECK(s.is_saved("Tür"));  // its later unsave does not hide it: it got now
  CHECK(s.looks("Tür") == 4);
}

TEST_CASE("backup: a copy a day, the last five kept, none while empty") {
  auto a = fresh("lexipeek-daily");
  Store s(StoreOptions{a, {}, "mac-d"});
  CHECK(s.daily_backup(a / "backup").empty());
  CHECK((!fs::exists(a / "backup") || fs::is_empty(a / "backup")));
  s.seen("Haus", "word", "ru", "дом");
  fs::create_directories(a / "backup");
  for (auto d : {"2020-01-01", "2020-01-02", "2020-01-03", "2020-01-04", "2020-01-05", "2020-01-06"})
    std::ofstream(a / "backup" / (std::string("lexipeek-") + d + ".db")) << "x";
  CHECK(s.daily_backup(a / "backup").empty());
  std::vector<std::string> names;
  for (auto &f : fs::directory_iterator(a / "backup")) names.push_back(f.path().filename().string());
  std::sort(names.begin(), names.end());
  CHECK(names.size() == 5);
  CHECK(names.front() == "lexipeek-2020-01-03.db");  // today's and the four newest before it
}
