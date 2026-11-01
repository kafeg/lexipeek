// The store: InkPeek's sync cases (reader, identity, writer), the clock, the sync folder, the store's API.
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

const fs::path SYNC = fs::path(LEXIPEEK_INKPEEK) / "tests" / "sync";

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

// The wall clock fixed for a test, the real one again after it.
struct Now {
  explicit Now(std::int64_t ms) { store::fixed_now = ms; }
  ~Now() { store::fixed_now = 0; }
  void operator=(std::int64_t ms) const { store::fixed_now = ms; }
};

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

std::vector<fs::path> cases(const char *kind) {
  std::vector<fs::path> out;
  for (auto &e : fs::directory_iterator(SYNC / kind))
    if (e.is_directory()) out.push_back(e.path());
  std::sort(out.begin(), out.end());
  return out;
}

// A case folder copied as it is (names with spaces, brackets, leading dots).
fs::path copy_of(const fs::path &from, const std::string &name) {
  auto to = fresh(name);
  fs::copy(from, to, fs::copy_options::recursive);
  return to;
}

std::string state(const fs::path &folder) { return records_json(merge_folder(folder)); }

// Every file of a folder: name → bytes.
std::map<std::string, std::string> files(const fs::path &dir) {
  std::map<std::string, std::string> out;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(dir, ec)) out[e.path().filename().string()] = slurp(e.path());
  return out;
}

// What an install keeps of itself: <data>/device.json.
void local_device(const fs::path &data, const std::string &id, const std::string &name, std::int64_t created) {
  fs::create_directories(data);
  std::ofstream(data / "device.json") << json{{"device", id}, {"name", name}, {"created", created}}.dump();
}

std::vector<std::string> own_lines(const fs::path &folder) {
  std::vector<std::string> out;
  for (auto &f : store::log_files(folder))
    for (auto &s : store::lines_of(f)) out.push_back(s);
  return out;
}

}  // namespace

TEST_CASE("sync cases: reader — the merge") {
  auto all = cases("reader");
  REQUIRE(all.size() >= 14);
  for (auto &d : all) check_case(d, json::parse(state(d)));
}

TEST_CASE("sync cases: reader — the store's SQLite view of the folder") {
  for (auto &d : cases("reader")) {
    auto data = fresh("lexipeek-store-case");
    auto folder = copy_of(d, "lexipeek-store-case-folder");
    Store st({data, folder, "mac-test0000", ""});
    REQUIRE(st.error() == "");
    st.sync();
    INFO(d.filename().string());
    CHECK(st.sync_info().error == "");
    check_case(d, json::parse(records_json(st.records())));
    // The folder as the cache has it: the same without the folder.
    Store again({data, {}, "mac-test0000", ""});
    check_case(d, json::parse(records_json(again.records())));
  }
}

TEST_CASE("sync cases: identity — keep or new, by the pure check and by the store at start") {
  auto all = cases("identity");
  REQUIRE(all.size() >= 6);
  for (auto &d : all) {
    auto local = json::parse(slurp(d / "local.json"));
    auto want = json::parse(slurp(d / "expected.json"));
    INFO(d.filename().string());
    std::string id = local["device"];
    std::vector<store::Line> own;
    for (auto &l : store::read_device(d / "inkpeek-log" / id))
      if (l.d == id) own.push_back(l);
    std::set<std::string> known;
    if (local["last"].is_string()) known.insert(local["last"].get<std::string>());
    CHECK(json{{"decision", store::keep_id(own, known) ? "keep" : "new"}} == want);

    // The store: its own log holds only that last line; the check runs at its first read of the folder.
    auto data = fresh("lexipeek-store-identity");
    auto folder = copy_of(d, "lexipeek-store-identity-folder");
    local_device(data, id, "Test", 1790553600000);
    if (local["last"].is_string()) {
      auto last = local["last"].get<std::string>();
      store::append(data / "inkpeek-log" / id / store::month_file(store::parse(last)->t.ms), last);
    }
    Store st({data, folder, "", ""});
    REQUIRE(st.error() == "");
    st.sync();
    CHECK((st.device() == id) == (want["decision"] == "keep"));
  }
}

TEST_CASE("sync cases: writer — the device's files byte for byte") {
  auto all = cases("writer");
  REQUIRE(all.size() >= 2);
  for (auto &d : all) {
    auto sc = json::parse(slurp(d / "scenario.json"));
    std::string id = sc["device"];
    INFO(d.filename().string());
    auto data = fresh("lexipeek-store-writer");
    auto folder = fresh("lexipeek-store-writer-folder");
    if (fs::exists(d / "before")) {
      fs::copy(d / "before", folder, fs::copy_options::recursive);
      // The install's own log is what it wrote to the folder.
      fs::create_directories(data / "inkpeek-log" / id);
      fs::copy(d / "before" / "inkpeek-log" / id, data / "inkpeek-log" / id, fs::copy_options::recursive);
    }
    local_device(data, id, sc["name"], sc["created"]);
    Now now(sc["steps"][0]["now"].get<std::int64_t>());
    Store st({data, folder, "", ""});
    REQUIRE(st.error() == "");
    st.sync();
    REQUIRE(st.device() == id);
    for (auto &s : sc["steps"]) {
      now = s["now"].get<std::int64_t>();
      std::string op = s["op"], text = s["text"];
      if (op == "saved.put") CHECK(st.save(text, s["kind"], s["lang"], s["meaning"]) == "");
      else if (op == "saved.del") CHECK(st.unsave(text) == "");
      else if (op == "history.seen") CHECK(st.seen(text, s["kind"], s["lang"], s["meaning"], s.value("base", "")) == "");
      else if (op == "history.del") CHECK(st.forget(text) == "");
      else if (op == "ai.put") CHECK(st.put_answer(text, s["lang"], s["section"], s["ver"], s["model"], s["answer"]) == "");
      else FAIL("unknown op " << op);
    }
    st.sync(false);
    auto want = files(d / "after" / "inkpeek-log" / id), there = files(folder / "inkpeek-log" / id);
    CHECK(store::format(store::DeviceFile{id, sc["app"], sc["name"], sc["created"]}) == want["device.json"]);
    if (sc["app"] != "lexipeek") there.erase("device.json"), want.erase("device.json");  // this app writes its own name
    CHECK(there == want);
    auto local = files(data / "inkpeek-log" / id);
    want.erase("device.json"), local.erase("device.json");
    CHECK(local == want);
    if (fs::exists(d / "before"))
      for (auto &other : store::device_folders(d / "before"))
        if (other.filename() != id) CHECK(files(folder / "inkpeek-log" / other.filename()) == files(other));
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

TEST_CASE("lines: what is skipped, the canonical form, the month file") {
  CHECK(store::parse(R"({"t":[1,2],"d":"a","op":"saved.del","text":"x"})"));
  CHECK(store::parse(R"({"t":[1,2],"d":"a","op":"saved.del","text":"x","page":3})"));
  CHECK(!store::parse(R"({"t":[1],"d":"a","op":"saved.del","text":"x"})"));
  CHECK(!store::parse(R"({"t":[1,2.5],"d":"a","op":"saved.del","text":"x"})"));
  CHECK(!store::parse(R"({"t":[1,2],"op":"saved.del","text":"x"})"));
  CHECK(!store::parse(R"({"t":[1,2],"d":"a","op":"note.put","text":"x"})"));
  CHECK(!store::parse(R"({"t":[1,2],"d":"a","op":"saved.del"})"));
  CHECK(!store::parse(R"([1,2])"));
  CHECK(!store::parse(R"({"t":[1,2)"));
  store::Line l;
  l.t = {5, 1}, l.d = "mac-k3fw2xqa", l.op = store::Op::history_seen, l.text = "Häuser", l.kind = "word", l.lang = "ru";
  l.meaning = "дома\t\"x\"\x1f/", l.base = "Haus", l.c = 3;
  auto s = store::format(l);
  CHECK(s == R"({"t":[5,1],"d":"mac-k3fw2xqa","op":"history.seen","text":"Häuser","kind":"word","lang":"ru","meaning":"дома\t\"x\"\u001f/","base":"Haus","c":3})");
  auto back = store::parse(s);
  REQUIRE(back);
  CHECK(back->base == "Haus");
  CHECK(back->c == 3);
  CHECK(store::month_file(1793491199999) == "2026-10.jsonl");
  CHECK(store::month_file(1793491200000) == "2026-11.jsonl");
  CHECK(store::month_file(0) == "1970-01.jsonl");
  CHECK(store::month_file(951782400000) == "2000-02.jsonl");   // 2000-02-29
  CHECK(store::month_file(951868799999) == "2000-02.jsonl");
  CHECK(store::month_file(951868800000) == "2000-03.jsonl");
  CHECK(store::month_file(99999999999999) == "5138-11.jsonl");  // past Windows' gmtime_s
  CHECK(store::format(store::DeviceFile{"mac-k3fw2xqa", "lexipeek", "MacBook Air", 1793145600000}) ==
        "{\"device\":\"mac-k3fw2xqa\",\"app\":\"lexipeek\",\"name\":\"MacBook Air\",\"created\":1793145600000}\n");
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
  Store st({data, {}, {}, {}});
  REQUIRE(st.error() == "");
  auto device = st.device();
  CHECK(device.starts_with("mac-") + device.starts_with("win-") + device.starts_with("linux-") == 1);
  CHECK(device.size() == device.find('-') + 9);
  CHECK(json::parse(slurp(data / "device.json"))["device"] == device);

  CHECK(st.seen("Haus", "word", "ru", "дом") == "");
  CHECK(st.seen("Haus", "word", "ru", "дом") == "");
  CHECK(st.seen("Häuser", "word", "ru", "дома", "Haus") == "");
  auto h = st.history();
  REQUIRE(h.size() == 2);
  CHECK(h[0].text == "Häuser");
  CHECK(h[0].base == "Haus");
  CHECK(h[1].count == 2);
  CHECK(h[1].devices == std::vector<std::string>{device});
  CHECK(st.looks("Haus") == 3);
  auto r = st.recents(5);
  REQUIRE(r.size() == 1);
  CHECK(r[0].text == "Häuser");
  auto log = own_lines(data / "inkpeek-log" / device);
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
  CHECK(st.saved()[0].origin == device);
  CHECK(st.saved()[0].updated_by == device);
  CHECK(st.saved()[0].id.starts_with(device + ":"));

  CHECK(st.forget("Häuser") == "");
  CHECK(!st.answer("Häuser", "ru", "quick", 3));
  CHECK(st.answer("Haus", "ru", "quick", 3));
  CHECK(st.seen("Häuser", "word", "ru", "дома", "Haus") == "");
  CHECK(st.history()[0].count == 1);
  CHECK(st.unsave("Haus") == "");
  CHECK(!st.is_saved("Haus"));
  CHECK(st.answer("Haus", "ru", "quick", 3));  // Haus is still in the history

  auto before = st.records();
  Store again({data, {}, {}, {}});
  CHECK(again.device() == device);
  CHECK(json::parse(records_json(again.records())) == json::parse(records_json(before)));

  CHECK(again.clear_history() == "");
  CHECK(again.history().empty());
  CHECK(again.records().ai.empty());
}

TEST_CASE("store: another device's newer change, then ours") {
  auto data = fresh("lexipeek-store-merge");
  Store st({data, {}, "mac-00000001", {}});
  REQUIRE(st.error() == "");
  CHECK(st.save("Haus", "word", "ru", "дом") == "");
  // Another device, its clock far ahead: its delete is newer than our save.
  fs::create_directories(data / "inkpeek-log" / "boox-00000002");
  std::ofstream(data / "inkpeek-log" / "boox-00000002" / "5138-11.jsonl")
      << R"({"t":[99999999999999,4],"d":"boox-00000002","op":"saved.del","text":"Haus"})" << "\n"
      << R"({"t":[99999999999999,5],"d":"boox-00000002","op":"history.seen","text":"Haus","kind":"word","lang":"en","meaning":"house","c":4})"
      << "\n";
  CHECK(st.is_saved("Haus"));
  CHECK(st.merge() == "");
  CHECK(!st.is_saved("Haus"));
  CHECK(st.looks("Haus") == 4);
  CHECK(st.save("Haus", "word", "ru", "дом") == "");  // made after reading it: wins
  CHECK(st.is_saved("Haus"));
  CHECK(st.saved()[0].origin == "mac-00000001");  // saved again after a delete: a new entry
  CHECK(st.seen("Haus", "word", "ru", "дом") == "");
  CHECK(st.looks("Haus") == 5);
  CHECK(st.history()[0].lang == "ru");
  CHECK(st.history()[0].devices == std::vector<std::string>{"boox-00000002", "mac-00000001"});
  auto own = own_lines(data / "inkpeek-log" / "mac-00000001");
  REQUIRE(own.size() == 3);
  CHECK(store::parse(own[1])->t == store::Time{99999999999999, 6});
}

TEST_CASE("store: the local log before the sync format is imported once, with its times; then to the folder") {
  auto root = fresh("lexipeek-store-old-log");
  auto data = root / "data", folder = root / "Sync";
  fs::create_directories(data / "log");
  fs::create_directories(folder);
  std::ofstream(data / "log" / "mac-4be3.jsonl")
      << R"({"v":1,"t":[1761400000000,0],"d":"mac-4be3","op":"history.seen","text":"Haus","kind":"word","lang":"ru","meaning":"дом","c":1})" "\n"
      << R"({"v":1,"t":[1761400001000,0],"d":"mac-4be3","op":"saved.put","text":"Haus","kind":"word","lang":"ru","meaning":"дом"})" "\n"
      << R"({"v":1,"t":[1761400002000,0],"d":"mac-4be3","op":"ai.put","text":"Haus","lang":"ru","section":"quick","ver":3,"model":"m","answer":"{}"})" "\n"
      << R"({"v":1,"t":[1761400003000,0],"d":"mac-4be3","op":"saved.put","text":"Baum","kind":"word","lang":"ru","meaning":"дерево"})" "\n"
      << R"({"v":1,"t":[1761400004000,0],"d":"mac-4be3","op":"saved.del","text":"Baum"})" "\n"
      << R"({"v":1,"t":[1764000000000,0],"d":"mac-4be3","op":"history.seen","text":"Haus","kind":"word","lang":"ru","meaning":"дом","c":2})" "\n"
      << R"({"v":1,"t":[1764000001000,0],"d":"mac-4be3","op":"history.se)";  // a cut last line
  std::string id;
  {
    Store st({data, folder, "", ""});
    REQUIRE(st.error() == "");
    id = st.device();
    CHECK(st.is_saved("Haus"));
    CHECK(!st.is_saved("Baum"));
    CHECK(st.saved()[0].created == 1761400001000);
    CHECK(st.saved()[0].origin == id);
    CHECK(st.looks("Haus") == 2);
    CHECK(st.answer("Haus", "ru", "quick", 3) == "{}");
    CHECK(!fs::exists(data / "log"));
    CHECK(fs::exists(data / "log.imported" / "mac-4be3.jsonl"));
    auto own = data / "inkpeek-log" / id;
    CHECK(store::lines_of(own / "2025-10.jsonl").size() == 5);
    CHECK(store::lines_of(own / "2025-11.jsonl").size() == 1);
    CHECK(slurp(own / "2025-10.jsonl").find("\"v\"") == std::string::npos);
    st.sync();
    CHECK(own_lines(folder / "inkpeek-log" / id).size() == 6);
    CHECK(st.seen("Haus", "word", "ru", "дом") == "");
    CHECK(st.looks("Haus") == 3);
  }
  // A crash before the rename: the old folder again — nothing written twice.
  fs::rename(data / "log.imported", data / "log");
  Store again({data, folder, "", ""});
  CHECK(again.device() == id);
  CHECK(own_lines(data / "inkpeek-log" / id).size() == 7);
  CHECK(again.looks("Haus") == 3);
  CHECK(fs::exists(data / "log.imported"));
}

TEST_CASE("store: the history keeps the newest entries") {
  auto data = fresh("lexipeek-store-limit");
  Store st({data, {}, "mac-00000001", {}});
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
  Store again({data, {}, "mac-00000001", {}});
  CHECK(again.history().size() == size_t(Store::history_limit));
}

TEST_CASE("sync folder: two computers, one folder; away and back; stopped, the records stay") {
  auto root = fresh("lexipeek-sync-two");
  auto folder = root / "Sync";
  fs::create_directories(folder);
  Store a({root / "a", folder, "mac-0000000a", "MacBook Air"}), b({root / "b", {}, "win-0000000b", "Desktop"});
  REQUIRE(a.error() == "");
  REQUIRE(b.error() == "");
  CHECK(a.save("Haus", "word", "ru", "дом") == "");  // before the first read of the folder: written there by it
  CHECK(!fs::exists(folder / "inkpeek-log" / "mac-0000000a"));
  CHECK(a.sync());
  CHECK(own_lines(folder / "inkpeek-log" / "mac-0000000a").size() == 1);
  CHECK(slurp(folder / "inkpeek-log" / "mac-0000000a" / "device.json").find("\"name\":\"MacBook Air\"") != std::string::npos);
  auto info = a.sync_info();
  CHECK(info.folder == folder.string());
  CHECK(info.reachable);
  CHECK(info.read > 0);
  CHECK(info.wrote == a.saved()[0].created);

  CHECK(b.seen("Baum", "word", "ru", "дерево") == "");  // before b had a folder: its own log takes it there
  CHECK(b.set_folder(folder) == "");
  CHECK(b.is_saved("Haus"));
  CHECK(b.saved()[0].origin == "mac-0000000a");
  CHECK(a.sync(false));  // the folder changed
  CHECK(a.looks("Baum") == 1);
  CHECK(!a.sync(false));  // nothing new
  CHECK(fs::exists(root / "a" / "sync-cache" / "inkpeek-log" / "win-0000000b" / "device.json"));

  CHECK(b.save("Haus", "word", "ru", "дом, здание") == "");
  b.sync(false);  // b's line pushed (its pusher writes it at once, in its own thread)
  CHECK(a.sync(false));
  CHECK(a.saved()[0].origin == "mac-0000000a");
  CHECK(a.saved()[0].updated_by == "win-0000000b");

  CHECK(a.set_name("Work Mac") == "");
  a.sync(false);
  CHECK(slurp(folder / "inkpeek-log" / "mac-0000000a" / "device.json").find("\"name\":\"Work Mac\"") != std::string::npos);
  CHECK(a.own_name() == "Work Mac");

  // The folder gone (an unmounted share): the records stay, writes wait for it.
  fs::rename(folder, root / "away");
  CHECK(a.sync());
  CHECK(!a.sync_info().reachable);
  CHECK(a.looks("Baum") == 1);
  CHECK(a.seen("Baum", "word", "ru", "дерево") == "");
  fs::rename(root / "away", folder);
  CHECK_FALSE(a.sync(false));  // the waiting line written there; a's view was already so
  CHECK(own_lines(folder / "inkpeek-log" / "mac-0000000a") == own_lines(root / "a" / "inkpeek-log" / "mac-0000000a"));
  CHECK(b.sync(false));
  CHECK(b.looks("Baum") == 2);

  CHECK(b.set_folder({}) == "");
  CHECK(b.sync_info().folder == "");
  CHECK(b.is_saved("Haus"));
  CHECK(b.looks("Baum") == 2);
}

TEST_CASE("sync folder: a write goes there by itself, never waiting for it; a device folder that cannot be read keeps its copy") {
  auto root = fresh("lexipeek-sync-io");
  auto folder = root / "Sync";
  fs::create_directories(folder);
  Store a({root / "a", folder, "mac-0000000a", ""}), b({root / "b", folder, "win-0000000b", ""});
  a.sync(), b.sync();
  CHECK(b.save("Haus", "word", "ru", "дом") == "");
  CHECK(until([&] { return own_lines(folder / "inkpeek-log" / "win-0000000b").size() == 1; }));  // no sync() called
  CHECK(a.sync(false));
  CHECK(a.is_saved("Haus"));
#ifndef _WIN32
  // The share half gone (a folder it cannot list): the copy and the view stay as they were.
  fs::permissions(folder / "inkpeek-log" / "win-0000000b", fs::perms::none);
  CHECK(a.sync());
  fs::permissions(folder / "inkpeek-log" / "win-0000000b", fs::perms::owner_all);
  CHECK(a.is_saved("Haus"));
  CHECK(!a.sync_info().error.empty());
  CHECK(fs::exists(root / "a" / "sync-cache" / "inkpeek-log" / "win-0000000b" / store::month_file(a.saved()[0].created)));
  CHECK(a.sync(false));  // read again
  CHECK(a.sync_info().error.empty());
#endif
}

TEST_CASE("sync folder: a copied install takes a new id; data deleted, a new id reads the earlier logs") {
  auto root = fresh("lexipeek-sync-copy");
  auto folder = root / "Sync";
  fs::create_directories(folder);
  Now now(1793491200000);
  {
    Store a({root / "a", folder, "mac-0000000a", "MacBook Air"});
    a.sync();
    CHECK(a.save("Haus", "word", "ru", "дом") == "");
  }
  fs::copy(root / "a", root / "copy", fs::copy_options::recursive);
  {
    Store a({root / "a", folder, "", ""});
    a.sync();
    CHECK(a.seen("Haus", "word", "ru", "дом") == "");
  }
  now = 1793491200000 + 1000;
  Store copy({root / "copy", folder, "", ""});
  copy.sync();
  CHECK(copy.device() != "mac-0000000a");
  CHECK(copy.own_name() == "MacBook Air");
  CHECK(copy.looks("Haus") == 1);
  CHECK(copy.seen("Haus", "word", "ru", "дом") == "");
  copy.sync(false);
  CHECK(own_lines(folder / "inkpeek-log" / copy.device()).size() == 1);

  // Its data deleted: a new device; the old id's logs are read as any other device's.
  Store fresh_install({root / "new", folder, "", ""});
  fresh_install.sync();
  CHECK(fresh_install.device() != "mac-0000000a");
  CHECK(fresh_install.device() != copy.device());
  CHECK(fresh_install.is_saved("Haus"));
  CHECK(fresh_install.saved()[0].origin == "mac-0000000a");
  CHECK(fresh_install.looks("Haus") == 2);
}

TEST_CASE("compaction: the reader cases merge the same with any one device compacted") {
  for (auto &d : cases("reader"))
    for (auto &dev : store::device_folders(d)) {
      auto folder = copy_of(d, "lexipeek-store-compact-case");
      auto own = folder / "inkpeek-log" / dev.filename();
      REQUIRE(store::compact_folder(own, 0) == "");
      INFO(d.filename().string(), " ", dev.filename().string());
      check_case(d, json::parse(state(folder)));
    }
}

TEST_CASE("compaction: a long log of two devices merges the same, the clock goes on") {
  auto root = fresh("lexipeek-store-compact");
  auto folder = root / "Sync";
  fs::create_directories(folder);
  std::mt19937 rnd(7);
  const std::vector<std::string> words = {"Haus", "Häuser", "Baum", "gehen", "ging", "das Haus", "Weg", "alt"};
  {
    Store a({root / "a", folder, "mac-00000001", ""}), b({root / "b", folder, "boox-00000002", ""});
    REQUIRE(a.error() == "");
    REQUIRE(b.error() == "");
    a.sync(), b.sync();
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
  auto own = folder / "inkpeek-log" / "mac-00000001", other = folder / "inkpeek-log" / "boox-00000002";
  auto before = state(folder);
  auto other_files = files(other);
  auto old_lines = own_lines(own).size();
  store::Time last;
  for (auto &l : store::read_roots({folder})) last = std::max(last, l.t);

  Store st({root / "a", folder, "mac-00000001", ""});
  REQUIRE(st.error() == "");
  st.sync();
  auto new_lines = own_lines(own).size();
  MESSAGE(old_lines, " lines compacted to ", new_lines);
  CHECK(new_lines * 2 <= old_lines);
  CHECK(own_lines(root / "a" / "inkpeek-log" / "mac-00000001") == own_lines(own));
  CHECK(files(other) == other_files);
  CHECK(state(folder) == before);
  CHECK(by_key(json::parse(records_json(st.records()))) == by_key(json::parse(before)));

  CHECK(store::compact_folder(other, 0) == "");
  CHECK(state(folder) == before);
  CHECK(store::compact_folder(own, 0) == "");  // twice: nothing left to drop
  CHECK(own_lines(own).size() == new_lines);

  CHECK(st.seen("Haus", "word", "ru", "дом") == "");
  st.sync(false);
  auto tail = store::parse(own_lines(own).back());
  REQUIRE(tail);
  CHECK(last < tail->t);
}

TEST_CASE("compaction: a small log stays as it is") {
  auto data = fresh("lexipeek-store-compact-small");
  {
    Store st({data, {}, "mac-00000001", ""});
    for (int i = 0; i < 500; i++) REQUIRE(st.seen("Haus", "word", "ru", "дом") == "");
  }
  auto own = data / "inkpeek-log" / "mac-00000001";
  auto bytes = files(own);
  Store st({data, {}, "mac-00000001", ""});
  CHECK(files(own) == bytes);
  CHECK(st.looks("Haus") == 500);
  CHECK(store::compact_folder(own, 0) == "");
  CHECK(own_lines(own).size() == 1);
  CHECK(Store({data, {}, "mac-00000001", ""}).looks("Haus") == 500);
}

TEST_CASE("backup: export, then import into another store adds what it lacks, with the dates") {
  auto a = fresh("lexipeek-backup-a"), b = fresh("lexipeek-backup-b");
  {
    Store s(StoreOptions{a, {}, "mac-0000000a", ""});
    s.save("Haus", "word", "ru", "дом");
    s.seen("Haus", "word", "ru", "дом");
    s.seen("Haus", "word", "ru", "дом");
    s.put_answer("Haus", "Russian", "quick", 7, "claude", "{\"translation\":\"дом\"}");
    CHECK(s.backup(a / "copy.db").empty());
  }
  REQUIRE(fs::exists(a / "copy.db"));
  CHECK(!fs::exists(a / "copy.db.tmp"));
  Store t(StoreOptions{b, {}, "mac-0000000b", ""});
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
  Store s(StoreOptions{a / "store", {}, "mac-0000000c", ""});
  s.save("Tür", "word", "ru", "дверь");
  s.unsave("Tür");
  auto r = s.restore(a / "old.db");
  CHECK(r.error.empty());
  CHECK(r.added == 2);
  CHECK(s.is_saved("Tür"));  // its later unsave does not hide it: it got now
  CHECK(s.looks("Tür") == 4);
}

TEST_CASE("backup: InkPeek's names its languages; they come in as codes (the Anki Key, Notes and the log as InkPeek's)") {
  auto a = fresh("lexipeek-backup-inkpeek");
  sqlite3 *db = nullptr;
  sqlite3_open((a / "inkpeek.db").string().c_str(), &db);
  sqlite3_exec(db,
               "CREATE TABLE saved(text TEXT PRIMARY KEY, kind TEXT, meaning TEXT, created INTEGER, lang TEXT);"
               "CREATE TABLE history(text TEXT PRIMARY KEY, kind TEXT, seen INTEGER, count INTEGER, meaning TEXT, lang TEXT, base TEXT);"
               "CREATE TABLE ai(text TEXT, lang TEXT, section TEXT, ver INTEGER, model TEXT, answer TEXT, created INTEGER);"
               "INSERT INTO saved VALUES('Haus', 'word', 'дом', 1000, 'Russian');"
               "INSERT INTO history VALUES('Baum', 'word', 1000, 2, 'дерево', 'Russian', NULL);"
               "INSERT INTO ai VALUES('Haus', 'Russian', 'quick', 7, 'claude', '{}', 1000);",
               nullptr, nullptr, nullptr);
  sqlite3_close(db);
  Store s(StoreOptions{a / "store", {}, "mac-0000000e", ""});
  CHECK(s.restore(a / "inkpeek.db").added == 3);
  CHECK(s.saved()[0].lang == "ru");
  CHECK(s.history()[0].lang == "ru");
  CHECK(s.answer("Haus", "Russian", "quick", 7) == "{}");  // answers keep the name, in both apps
}

TEST_CASE("backup: a copy a day, the last five kept, none while empty") {
  auto a = fresh("lexipeek-daily");
  Store s(StoreOptions{a, {}, "mac-0000000d", ""});
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
