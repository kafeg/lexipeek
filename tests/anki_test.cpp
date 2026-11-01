// Anki through AnkiConnect, without Anki: a fake AnkiConnect (API version 6) on 127.0.0.1 keeps decks, note types,
// notes and media in memory; the binding sends the store's entries to it through libcurl.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <regex>
#include <set>
#include <nlohmann/json.hpp>
#include <sha256.h>
#include "lexipeek/anki.hpp"
#include "lexipeek/app.hpp"
#include "lexipeek/l10n.hpp"
#include "lexipeek/speech.hpp"
#include "lexipeek/store.hpp"
#include "lexipeek/views.hpp"
#include "text/text.hpp"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define closesocket close
#endif

using namespace lexipeek;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// AnkiConnect as its docs say: the envelope {"result", "error"}, multi, the actions the binding uses.
struct FakeAnki {
  struct NoteRec {
    long long id;
    std::string deck, model;
    std::map<std::string, std::string> fields;
  };
  std::mutex mu;
  std::set<std::string> decks{"Default"};
  std::map<std::string, std::vector<std::string>> models{{"Basic", {"Front", "Back"}}};
  std::map<std::string, std::string> model_css;
  std::vector<NoteRec> notes;
  std::map<std::string, std::string> media;
  std::string key;               // apiKey ("" = none)
  std::set<long long> editing;   // notes open in an editor: their update fails
  int requests = 0, syncs = 0;
  long long next = 1000;

  json action(const json &r) {
    if (!key.empty() && r.value("key", "") != key) return {{"result", nullptr}, {"error", "valid api key must be provided"}};
    auto a = r.value("action", "");
    auto p = r.value("params", json::object());
    auto ok = [](json v) { return json{{"result", std::move(v)}, {"error", nullptr}}; };
    auto err = [](const std::string &e) { return json{{"result", nullptr}, {"error", e}}; };
    if (a == "version") return ok(6);
    if (a == "deckNames") return ok(decks);
    if (a == "createDeck") return decks.insert(p["deck"].get<std::string>()), ok(1);
    if (a == "modelNames") {
      json out = json::array();
      for (auto &[n, f] : models) out.push_back(n);
      return ok(out);
    }
    if (a == "modelFieldNames") {
      auto i = models.find(p["modelName"].get<std::string>());
      return i == models.end() ? err("model was not found") : ok(i->second);
    }
    if (a == "createModel") {
      auto n = p["modelName"].get<std::string>();
      if (models.count(n)) return err("Model name already exists");
      models[n] = p["inOrderFields"].get<std::vector<std::string>>();
      model_css[n] = p.value("css", "");
      return ok(json::object());
    }
    if (a == "findNotes") {
      auto q = p["query"].get<std::string>();
      std::set<std::string> keys;
      static const std::regex k("Key:([a-z0-9-]+)");
      for (std::sregex_iterator i(q.begin(), q.end(), k), e; i != e; ++i) keys.insert((*i)[1]);
      json out = json::array();
      for (auto &n : notes)
        if (q.find("\"note:" + n.model + "\"") != std::string::npos && (keys.empty() || keys.count(n.fields["Key"]))) out.push_back(n.id);
      return ok(out);
    }
    if (a == "notesInfo") {
      json out = json::array();
      for (auto &id : p["notes"])
        for (auto &n : notes)
          if (n.id == id.get<long long>()) {
            json f = json::object();
            for (auto &[name, v] : n.fields) f[name] = {{"value", v}, {"order", 0}};
            out.push_back({{"noteId", n.id}, {"modelName", n.model}, {"fields", f}});
          }
      return ok(out);
    }
    if (a == "addNote") {
      auto &n = p["note"];
      if (!decks.count(n["deckName"].get<std::string>())) return err("deck was not found: " + n["deckName"].get<std::string>());
      auto m = models.find(n["modelName"].get<std::string>());
      if (m == models.end()) return err("model was not found: " + n["modelName"].get<std::string>());
      NoteRec rec{next++, n["deckName"].get<std::string>(), n["modelName"].get<std::string>(), {}};
      for (auto &f : m->second) rec.fields[f] = n["fields"].value(f, "");
      notes.push_back(rec);
      return ok(rec.id);
    }
    if (a == "updateNoteFields") {
      auto id = p["note"]["id"].get<long long>();
      if (editing.count(id)) return err("the note is open in an editor");
      for (auto &n : notes)
        if (n.id == id) {
          for (auto &[f, v] : p["note"]["fields"].items()) n.fields[f] = v.get<std::string>();
          return ok(nullptr);
        }
      return err("note was not found: " + std::to_string(id));
    }
    if (a == "deleteNotes") {
      for (auto &id : p["notes"]) std::erase_if(notes, [&](const NoteRec &n) { return n.id == id.get<long long>(); });
      return ok(nullptr);
    }
    if (a == "storeMediaFile") return media[p["filename"].get<std::string>()] = p["data"].get<std::string>(), ok(p["filename"]);
    if (a == "sync") return syncs++, ok(nullptr);
    if (a == "multi") {
      json out = json::array();
      for (auto &x : p["actions"]) out.push_back(action(x));
      return ok(out);
    }
    return err("unsupported action");
  }

  std::string answer(const std::string &body) {
    std::lock_guard g(mu);
    requests++;
    try {
      return action(json::parse(body)).dump();
    } catch (const std::exception &e) {
      return json{{"result", nullptr}, {"error", e.what()}}.dump();
    }
  }

  const NoteRec *by_key(const std::string &k) {
    std::lock_guard g(mu);
    for (auto &n : notes)
      if (n.fields["Key"] == k) return &n;
    return nullptr;
  }
};

// A tiny HTTP/1.1 server on 127.0.0.1 (a free port): one request per connection, closed after the answer.
class Server {
 public:
  explicit Server(FakeAnki &anki) : anki_(anki) {
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#endif
    s_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    bind(s_, reinterpret_cast<sockaddr *>(&a), sizeof a);
    listen(s_, 16);
    socklen_t n = sizeof a;
    getsockname(s_, reinterpret_cast<sockaddr *>(&a), &n);
    port_ = ntohs(a.sin_port);
    t_ = std::thread([this] { loop(); });
  }
  ~Server() {
    stop_ = true;
    t_.join();
    closesocket(s_);
  }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

 private:
  void loop() {
    while (!stop_) {
      fd_set r;
      FD_ZERO(&r);
      FD_SET(s_, &r);
      timeval tv{0, 20000};
      if (select(static_cast<int>(s_ + 1), &r, nullptr, nullptr, &tv) <= 0) continue;
      auto c = accept(s_, nullptr, nullptr);
      std::string in;
      char buf[65536];
      std::size_t want = std::string::npos;
      while (in.size() < want) {
        auto got = recv(c, buf, sizeof buf, 0);
        if (got <= 0) break;
        in.append(buf, static_cast<std::size_t>(got));
        auto end = in.find("\r\n\r\n");
        if (end != std::string::npos && want == std::string::npos) {
          std::smatch m;
          static const std::regex len("[Cc]ontent-[Ll]ength: *([0-9]+)");
          auto head = in.substr(0, end);
          want = end + 4 + (std::regex_search(head, m, len) ? std::stoul(m[1]) : 0);
        }
      }
      auto body = want == std::string::npos ? "" : in.substr(in.find("\r\n\r\n") + 4);
      auto out = anki_.answer(body);
      auto reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(out.size()) +
                   "\r\nConnection: close\r\n\r\n" + out;
      send(c, reply.data(), static_cast<int>(reply.size()), 0);
      closesocket(c);
    }
  }

  FakeAnki &anki_;
  socket_t s_;
  int port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread t_;
};

// A port nothing listens on: a server's, closed.
std::string closed_url() {
  FakeAnki a;
  return Server(a).url();
}

fs::path fresh(const std::string &name) {
  auto d = fs::temp_directory_path() / name;
  wipe(d);
  fs::create_directories(d);
  return d;
}

AnkiSettings on(const std::string &url) {
  AnkiSettings s;
  s.on = true, s.url = url;
  return s;
}

}  // namespace

TEST_CASE("anki: the Key — the answer language and sha256 of InkPeek's lookup key, history apart") {
  auto k = anki::key_of("Haus", "ru");
  CHECK(k.size() == 3 + 32);
  CHECK(k.starts_with("ru-"));
  CHECK(anki::key_of("haus", "ru") == k);          // normKey: lower case
  CHECK(anki::key_of(" Haus ", "ru") == k);        // trimmed
  CHECK(anki::key_of("Haus", "en") != k);          // another answer language, another note
  CHECK(anki::key_of("Haus", "ru", true) == "h-" + k);
  SHA256 h;
  CHECK(k == "ru-" + h("haus").substr(0, 32));
  CHECK(std::regex_match(anki::key_of("Ich mache die Tür zu!", "ru"), std::regex("[a-z0-9-]+")));
  // An entry without its language (InkPeek's rows before its schema 3): the answer language, as InkPeek's Anki.key.
  CHECK(anki::key_of("Haus", "", false, "ru") == k);
  CHECK(anki::key_of("Haus", "", true, "ru") == "h-" + k);
  CHECK(anki::key_of("Haus", "en", false, "ru") != k);
}

// tests/inkpeek/tests/anki (InkPeek runs them too): what differs, a line each (the first 20).
json anki_cases(const char *name) {
  std::ifstream f(fs::path(LEXIPEEK_INKPEEK) / "tests" / "anki" / name);
  auto c = json::parse(f)["cases"];
  REQUIRE(c.size() >= 50);
  return c;
}

void differs(std::vector<std::string> &bad, const json &c, const json &want, const json &got) {
  if (want != got && bad.size() < 20) bad.push_back(c.dump() + "\n    got: " + got.dump());
}

TEST_CASE("anki: InkPeek's cases — the Key, a note's fields, the rule for duplicates") {
  std::vector<std::string> bad;
  for (auto &c : anki_cases("keys.json"))
    differs(bad, c, c["key"], anki::key_of(c["text"], c["lang"], c["history"].get<bool>(), c["answer"]));
  for (auto &c : anki_cases("fields.json")) {
    auto n = anki::note_of(c["text"], c["kind"], c["meaning"], c["lang"], c["quick"].is_string() ? c["quick"].get<std::string>() : "",
                           c["history"].get<bool>(), c["answer"]);
    differs(bad, c, c["fields"], json{{"Front", n.front}, {"Back", n.back}, {"Example", n.example}, {"Key", n.key}});
  }
  for (auto &c : anki_cases("dedupe.json")) {
    std::vector<anki::Found> notes;
    for (auto &n : c["notes"]) notes.push_back({n["id"].get<long long>(), n["key"], n["audio"]});
    auto p = anki::dedupe_plan(notes);
    json audio = json::object();
    for (auto &[id, a] : p.audio) audio[std::to_string(id)] = a;
    differs(bad, c, json{{"audio", c["audio"]}, {"deleted", c["deleted"]}}, json{{"audio", audio}, {"deleted", p.deleted}});
  }
  std::string all;
  for (auto &b : bad) all += b + "\n";
  INFO(all);
  CHECK(bad.empty());
}

TEST_CASE("anki: the notes — saved words and phrases into their decks, the history when on, a word's header apart") {
  auto d = fresh("lexipeek-anki-notes");
  {
    Store st({d, {}, {}, {}});
    st.save("Haus", "word", "ru", "das Haus (Hauses, pl. Häuser) — дом, здание");
    st.save("Ich mache die Tür zu", "phrase", "ru", "Я закрываю дверь");
    st.seen("Baum", "word", "ru", "der Baum — дерево");
    AnkiSettings s;
    auto n = anki::notes(st, s, d);
    REQUIRE(n.size() == 2);
    auto &phrase = n[0].kind == "phrase" ? n[0] : n[1];
    auto &word = n[0].kind == "word" ? n[0] : n[1];
    CHECK(word.deck == "Peek::Words");
    CHECK(word.front == "Haus");
    CHECK(word.back == "<div class=\"head\">das Haus (Hauses, pl. Häuser)</div>дом, здание");
    CHECK(phrase.deck == "Peek::Phrases");
    CHECK(phrase.back == "Я закрываю дверь");
    s.history_on = true, s.words_on = false;
    n = anki::notes(st, s, d);
    REQUIRE(n.size() == 2);
    CHECK(n[1].deck == "Peek::History");
    CHECK(n[1].key == anki::key_of("Baum", "ru", true));
    st.save("<b>&", "phrase", "ru", "a\nb");
    CHECK(anki::notes(st, s, d)[0].front == "&lt;b&gt;&amp;");
    CHECK(anki::notes(st, s, d)[0].back == "a<br>b");
    // Without its language (InkPeek's older rows): the answer language, for its Key and its reading.
    st.save("Lösung", "word", "", "die Lösung — решение");
    s.words_on = true;
    int found = 0;
    for (auto &x : anki::notes(st, s, d))
      if (x.text == "Lösung") {
        found++;
        CHECK(x.lang == "ru");
        CHECK(x.key == anki::key_of("Lösung", "ru"));
      }
    CHECK(found == 1);
    // A blank line: the newest quick AI answer's line (kept under the English name), as InkPeek's Back.
    st.save("Wolke", "word", "ru", "");
    st.put_answer("Wolke", "Russian", "quick", 3, "m", R"({"translation":"облако"})");
    st.save("Regen", "word", "", " ");
    st.put_answer("Regen", "Russian", "quick", 7, "m", R"({"article":"der","base":"Regen","meanings":["дождь"]})");
    for (auto &x : anki::notes(st, s, d))
      if (x.text == "Wolke") CHECK(x.back == "облако");
      else if (x.text == "Regen") CHECK(x.back == "<div class=\"head\">der Regen</div>дождь");
  }
  wipe(d);
}

TEST_CASE("anki: sent once, then only what changed; a note deleted in Anki is added again; the record survives a restart") {
  auto d = fresh("lexipeek-anki-send");
  FakeAnki anki;
  Server srv(anki);
  {
    Store st({d, {}, {}, {}});
    st.save("Haus", "word", "ru", "das Haus — дом");
    st.save("Ich mache die Tür zu", "phrase", "ru", "Я закрываю дверь");
    anki::Sender snd(st, d, anki::default_transport());
    snd.configure(on(srv.url()));
    auto s = snd.run_once();
    CHECK(s.check.code == anki::Code::ok);
    CHECK(s.waiting == 0);
    CHECK(anki.decks.count("Peek::Words"));
    CHECK(anki.decks.count("Peek::Phrases"));
    CHECK(anki.models["Peek"] == anki::fields);
    REQUIRE(anki.notes.size() == 2);
    auto *haus = anki.by_key(anki::key_of("Haus", "ru"));
    REQUIRE(haus);
    CHECK(haus->deck == "Peek::Words");
    CHECK(haus->fields.at("Front") == "Haus");
    CHECK(snd.sent(anki::key_of("Haus", "ru")));
    CHECK(s.text.find("2") != std::string::npos);
    {
      std::ifstream f(d / "anki.log");
      std::string log((std::istreambuf_iterator<char>(f)), {});
      CHECK(log.find("sent 2: 2 added, 0 updated, 0 failed") != std::string::npos);
    }

    // Nothing waits: Anki is not asked at all.
    auto before = anki.requests;
    snd.run_once();
    CHECK(anki.requests == before);

    // A new meaning updates the same note.
    st.save("Haus", "word", "ru", "das Haus — дом, здание");
    s = snd.run_once();
    CHECK(anki.notes.size() == 2);
    CHECK(anki.by_key(anki::key_of("Haus", "ru"))->fields.at("Back").find("здание") != std::string::npos);

    // Deleted in Anki, then changed: added again, not lost.
    std::erase_if(anki.notes, [](auto &n) { return n.fields["Front"] == "Haus"; });
    st.save("Haus", "word", "ru", "das Haus — жилище");
    snd.run_once();
    CHECK(anki.notes.size() == 2);
    CHECK(anki.by_key(anki::key_of("Haus", "ru")));
  }
  {
    Store st({d, {}, {}, {}});
    anki::Sender again(st, d, anki::default_transport());
    again.configure(on(srv.url()));
    CHECK(again.sent(anki::key_of("Ich mache die Tür zu", "ru")));
    CHECK(again.run_once().waiting == 0);  // Anki asked once (the tab's connection line), nothing sent
    auto before = anki.requests;
    CHECK(again.run_once().waiting == 0);
    CHECK(anki.requests == before);
  }
  wipe(d);
}

TEST_CASE("anki: Anki not running — everything waits, then goes once it answers") {
  auto d = fresh("lexipeek-anki-down");
  FakeAnki anki;
  {
    Store st({d, {}, {}, {}});
    st.save("Haus", "word", "ru", "дом");
    anki::Sender snd(st, d, anki::default_transport());
    snd.configure(on(closed_url()));
    auto s = snd.run_once();
    CHECK(s.check.code == anki::Code::not_running);
    CHECK(s.waiting == 1);
    CHECK_FALSE(snd.sent(anki::key_of("Haus", "ru")));
    Server srv(anki);
    snd.configure(on(srv.url()));  // another address: tried at once
    s = snd.run_once();
    CHECK(s.check.code == anki::Code::ok);
    CHECK(s.waiting == 0);
    CHECK(anki.notes.size() == 1);
  }
  wipe(d);
}

TEST_CASE("anki: the thread sends by itself; Send now; the key; the note type changed; a refused note") {
  auto d = fresh("lexipeek-anki-thread");
  FakeAnki anki;
  Server srv(anki);
  {
    Store st({d, {}, {}, {}});
    anki::Sender snd(st, d, anki::default_transport(), {}, std::chrono::milliseconds(20));
    snd.configure(on(srv.url()));
    snd.start();
    st.save("Haus", "word", "ru", "дом");
    CHECK(until([&] { return anki.by_key(anki::key_of("Haus", "ru")) != nullptr; }));
    CHECK(until([&] { return snd.status().waiting == 0 && !snd.status().sending; }));

    // AnkiConnect wants a key: nothing goes until it is given.
    {
      std::lock_guard g(anki.mu);
      anki.key = "secret";
    }
    st.save("Baum", "word", "ru", "дерево");
    CHECK(until([&] { return snd.status().check.code == anki::Code::need_key; }));
    CHECK(snd.status().waiting == 1);
    snd.set_key("secret");
    CHECK(until([&] { return anki.by_key(anki::key_of("Baum", "ru")) != nullptr; }));

    // A note open in Anki's editor: refused, not tried again until "Send now".
    auto baum = anki.by_key(anki::key_of("Baum", "ru"))->id;
    {
      std::lock_guard g(anki.mu);
      anki.editing.insert(baum);
    }
    st.save("Baum", "word", "ru", "дерево, древесина");
    CHECK(until([&] { return snd.status().text.find("editor") != std::string::npos; }));
    auto before = anki.requests;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(snd.status().waiting == 1);
    {
      std::lock_guard g(anki.mu);
      anki.editing.clear();
    }
    snd.send_now();
    CHECK(until([&] { return snd.status().waiting == 0 && snd.status().check.code == anki::Code::ok; }));
    CHECK(anki.requests > before);
    CHECK(anki.by_key(anki::key_of("Baum", "ru"))->fields.at("Back").find("древесина") != std::string::npos);
  }
  wipe(d);

  // Someone's own "Peek" without our fields is not touched.
  d = fresh("lexipeek-anki-model");
  FakeAnki other;
  other.models["Peek"] = {"Front", "Back"};
  Server srv2(other);
  {
    Store st({d, {}, {}, {}});
    st.save("Haus", "word", "ru", "дом");
    anki::Sender snd(st, d, anki::default_transport());
    snd.configure(on(srv2.url()));
    auto s = snd.run_once();
    CHECK(s.check.code == anki::Code::model);
    CHECK(other.notes.empty());
    CHECK(other.models["Peek"].size() == 2);
    CHECK(anki::check(on(srv2.url()), "", anki::default_transport()).code == anki::Code::model);
  }
  wipe(d);
}

TEST_CASE("anki: audio — a kept reading, else one made for the note (one at a time); none while the voice model is missing, sent again once it is there") {
  auto d = fresh("lexipeek-anki-audio");
  FakeAnki anki;
  Server srv(anki);
  {
    Store st({d, {}, {}, {}});
    st.save("Haus", "word", "ru", "дом");
    st.save("Baum", "word", "ru", "дерево");
    st.seen("Tür", "word", "ru", "дверь");
    auto dir = speech::saved_dir(d, "Haus");
    fs::create_directories(dir);
    std::ofstream(dir / "a.wav", std::ios::binary) << "RIFF1";
    // The voice model: missing first; a made reading is a file named by its text.
    std::atomic<bool> model{false};
    std::vector<std::string> made;
    anki::Reader reader{[&](const anki::Note &n) {
                          made.push_back(n.text);
                          auto f = d / (n.text + ".wav");
                          std::ofstream(f, std::ios::binary) << "RIFF2";
                          return speech::Made{text::utf8_of(f)};
                        },
                        [&] { return model.load(); }};
    auto s = on(srv.url());
    s.history_on = true, s.sync = true;
    anki::Sender snd(st, d, anki::default_transport(), reader);
    snd.configure(s);
    auto r = snd.run_once();
    CHECK_FALSE(r.voice);
    CHECK(made.empty());
    CHECK(anki.notes.size() == 3);  // without the model: all go, Haus with its kept reading
    auto key = anki::key_of("Haus", "ru");
    auto name = "lexipeek_" + key + ".wav";
    REQUIRE(anki.media.count(name));
    CHECK(anki.media[name] == "UklGRjE=");
    CHECK(anki.by_key(key)->fields.at("Audio") == "[sound:" + name + "]");
    CHECK(anki.by_key(anki::key_of("Baum", "ru"))->fields.at("Audio").empty());
    CHECK(r.words.sent == 2);
    CHECK(r.words.audio == 1);
    CHECK(r.history.audio == 1);
    CHECK(anki.syncs == 1);
    // The model is there: one reading a round, its note updated with it.
    model = true;
    r = snd.run_once();
    CHECK(r.voice);
    REQUIRE(made.size() == 1);
    CHECK(made[0] == "Baum");
    CHECK(anki.by_key(anki::key_of("Baum", "ru"))->fields.at("Audio") == "[sound:lexipeek_" + anki::key_of("Baum", "ru") + ".wav]");
    CHECK(r.history.audio == 1);
    CHECK(anki.syncs == 1);  // AnkiWeb once the backlog went, not after each note
    r = snd.run_once();
    CHECK(made.size() == 2);
    CHECK(anki.by_key(anki::key_of("Tür", "ru", true))->deck == "Peek::History");
    CHECK(anki.by_key(anki::key_of("Tür", "ru", true))->fields.at("Audio").starts_with("[sound:"));
    CHECK(r.words.audio + r.phrases.audio + r.history.audio == 0);
    CHECK(r.waiting == 0);
    CHECK(anki.syncs == 2);
    snd.run_once();
    CHECK(anki.syncs == 2);
  }
  wipe(d);
}

TEST_CASE("anki: Check reads only; its texts") {
  FakeAnki anki;
  Server srv(anki);
  auto c = anki::check(on(srv.url()), "", anki::default_transport());
  CHECK(c.code == anki::Code::ok);
  CHECK(c.text == tr("anki_connected", {"6", trn("anki_decks", 1)}));
  CHECK(c.decks == std::vector<std::string>{"Default"});
  CHECK(anki.decks.size() == 1);
  CHECK(anki.models.count("Peek") == 0);
  anki.key = "k";
  CHECK(anki::check(on(srv.url()), "", anki::default_transport()).code == anki::Code::need_key);
  CHECK(anki::check(on(srv.url()), "k", anki::default_transport()).code == anki::Code::ok);
  auto down = anki::check(on(closed_url()), "", anki::default_transport());
  CHECK(down.code == anki::Code::not_running);
  CHECK(down.text == tr("anki_not_running_t"));
}

TEST_CASE("anki: Sync → Anki — the rows, their changes, an empty deck name is its default again") {
  Settings s;
  SettingsState st;
  auto keys = settings_tab_keys(SettingsSection::sync);
  auto at = static_cast<int>(std::find(keys.begin(), keys.end(), "anki") - keys.begin());
  REQUIRE(at < static_cast<int>(keys.size()));
  auto page = settings_view(SettingsSection::sync, at, s, st);
  auto has = [&](const std::string &key) {
    return std::any_of(page.rows.begin(), page.rows.end(), [&](auto &r) { return r.key == key; });
  };
  CHECK(has("anki_on"));
  CHECK(has("anki_check"));
  CHECK_FALSE(has("anki_send"));  // off: nothing to send
  CHECK(has("anki_words"));
  CHECK_FALSE(has("anki_history"));  // the history's deck only when on
  CHECK(has("anki_key"));
  settings_toggle(s, "anki_on", true);
  settings_toggle(s, "anki_history_on", true);
  settings_text(s, "anki_words", "  Deutsch::Wörter ");
  CHECK(s.anki.on);
  CHECK(s.anki.words == "Deutsch::Wörter");
  settings_text(s, "anki_words", "");
  CHECK(s.anki.words == "Peek::Words");
  st.anki.on = true, st.anki.waiting = 3, st.anki.words = {120, 3, 2};
  st.anki.voice = false;
  st.anki.probed = true, st.anki.check.code = anki::Code::not_running, st.anki.check.decks = {"Default", "Deutsch"};
  page = settings_view(SettingsSection::sync, at, s, st);
  auto row = [&](const std::string &key) { return *std::find_if(page.rows.begin(), page.rows.end(), [&](auto &r) { return r.key == key; }); };
  CHECK(has("anki_send"));
  CHECK(has("anki_history"));
  CHECK(has("anki_voice"));  // no voice model: a line to it
  CHECK(row("anki_voice").go_section == SettingsSection::speech);
  CHECK(row("anki_send").about == tr("anki_waiting", {"3"}));
  CHECK(row("anki_send").enabled);
  CHECK(row("anki_words_on").about == tr("anki_counts", {"120", "3", "2"}));
  CHECK(row("anki_words").choices == std::vector<std::string>{"Peek::Words", "Default", "Deutsch"});
  // Nothing answers: Anki not running, or (Anki runs) the add-on missing; a key wanted.
  CHECK(row("anki_check").about.starts_with(tr("anki_not_running_t")));
  CHECK(row("anki_check").tone == SettingRow::Tone::bad);
  CHECK(row("anki_words_on").tone == SettingRow::Tone::bad);  // waiting while Anki does not answer
  st.anki_app = true;
  page = settings_view(SettingsSection::sync, at, s, st);
  CHECK(row("anki_check").about.starts_with(tr("anki_no_addon_t")));
  st.anki.check.code = anki::Code::need_key;
  page = settings_view(SettingsSection::sync, at, s, st);
  CHECK(row("anki_check").about.starts_with(tr("anki_need_key_t")));
  st.anki.check = {anki::Code::ok, "Connected"};
  st.anki.words = {120, 0, 0};
  page = settings_view(SettingsSection::sync, at, s, st);
  CHECK(row("anki_check").about == "✓ Connected");
  CHECK(row("anki_check").tone == SettingRow::Tone::good);
  CHECK(row("anki_words_on").tone == SettingRow::Tone::good);
  st.anki.words = {120, 3, 0};
  page = settings_view(SettingsSection::sync, at, s, st);
  CHECK(row("anki_words_on").tone == SettingRow::Tone::warn);
  auto d = fresh("lexipeek-anki-settings");
  CHECK(save_settings(d, s).empty());
  auto back = load_settings(d);
  CHECK(back.anki.on);
  CHECK(back.anki.history_on);
  CHECK(back.anki.url == "http://127.0.0.1:8765");
  wipe(d);
}

TEST_CASE("anki: Notes' rows carry their Key") {
  auto d = fresh("lexipeek-anki-rows");
  {
    Store st({d, {}, {}, {}});
    st.save("Haus", "word", "ru", "дом");
    st.seen("Haus", "word", "ru", "дом");
    CHECK(notes_view(st, NotesTab::words, "ru").rows.at(0).key == anki::key_of("Haus", "ru"));
    CHECK(notes_view(st, NotesTab::history, "ru").rows.at(0).key == anki::key_of("Haus", "ru", true));
  }
  wipe(d);
}

TEST_CASE("anki: on at launch — the engine applies settings.json's binding, not only a change in Settings") {
  auto d = fresh("lexipeek-anki-launch");
  std::ofstream(d / "settings.json") << R"({"anki": {"on": true}})";
  {
    auto e = app::Engine::make(d.string(), app::device(16000, {}, "arm64"));
    CHECK(e.anki_status().on);
  }
  wipe(d);
}

TEST_CASE("anki: a reading that cannot be made now does not hold its note: it goes without after one try") {
  auto d = fresh("lexipeek-anki-later");
  FakeAnki anki;
  Server srv(anki);
  {
    Store st({d, {}, {}, {}});
    st.save("Haus", "word", "ru", "дом");
    st.save("Baum", "word", "ru", "дерево");
    int tries = 0;
    anki::Reader reader{[&](const anki::Note &) {
                          tries++;
                          return speech::Made{"", true};
                        },
                        [] { return true; }};
    anki::Sender snd(st, d, anki::default_transport(), reader);
    snd.configure(on(srv.url()));
    snd.run_once();  // one tried: it goes, the other waits for its try
    CHECK(tries == 1);
    CHECK(anki.notes.size() == 1);
    auto r = snd.run_once();
    CHECK(tries == 2);
    CHECK(anki.notes.size() == 2);
    CHECK(r.waiting == 0);
    CHECK(r.words.audio == 2);  // still tried later
  }
  wipe(d);
}

TEST_CASE("anki: an update without a reading at hand keeps the note's Audio (a restart while the voice model is gone)") {
  auto d = fresh("lexipeek-anki-keep-audio");
  FakeAnki anki;
  Server srv(anki);
  {
    Store st({d, {}, {}, {}});
    st.save("Haus", "word", "ru", "дом");
    auto f = d / "Haus.wav";
    std::ofstream(f, std::ios::binary) << "RIFF1";
    anki::Reader made{[&](const anki::Note &) { return speech::Made{text::utf8_of(f)}; }, [] { return true; }};
    auto key = anki::key_of("Haus", "ru");
    {
      anki::Sender snd(st, d, anki::default_transport(), made);
      snd.configure(on(srv.url()));
      snd.run_once();
      snd.run_once();
      REQUIRE(anki.by_key(key));
      REQUIRE(anki.by_key(key)->fields.at("Audio").starts_with("[sound:"));
    }
    anki::Sender again(st, d, anki::default_transport(), anki::Reader{{}, [] { return false; }});
    again.configure(on(srv.url()));
    again.run_once();
    CHECK(anki.by_key(key)->fields.at("Audio") == "[sound:lexipeek_" + key + ".wav]");
  }
  wipe(d);
}

TEST_CASE("anki: duplicates by Key after AnkiWeb merged two apps' notes — the oldest kept with an Audio, the others deleted; only ours, only Peek") {
  auto d = fresh("lexipeek-anki-dedupe");
  FakeAnki anki;
  anki.models["Peek"] = anki::fields;
  anki.decks.insert("Peek::Words");
  auto haus = anki::key_of("Haus", "ru");
  auto add = [&](long long id, const std::string &model, const std::string &key, const std::string &audio) {
    anki.notes.push_back({id, "Peek::Words", model, {{"Front", "x"}, {"Back", "y"}, {"Audio", audio}, {"Example", ""}, {"Key", key}}});
  };
  add(9, "Peek", haus, "[sound:lexipeek_" + haus + ".wav]");  // InkPeek's, with its reading
  add(5, "Peek", haus, "");                                    // LexiPeek's, older
  add(7, "Peek", haus, "");
  add(3, "Peek", "ru-notours", ""), add(4, "Peek", "ru-notours", "");  // not an entry of this app
  add(2, "Basic", haus, "");                                        // another note type
  Server srv(anki);
  {
    Store st({d, {}, {}, {}});
    st.save("Haus", "word", "ru", "дом");
    anki::Sender snd(st, d, anki::default_transport());
    snd.configure(on(srv.url()));
    snd.run_once();
    std::vector<long long> ids;
    for (auto &n : anki.notes) ids.push_back(n.id);
    std::sort(ids.begin(), ids.end());
    CHECK(ids == std::vector<long long>{2, 3, 4, 5});
    auto *kept = anki.by_key(haus);
    REQUIRE(kept);
    CHECK(kept->fields.at("Audio") == "[sound:lexipeek_" + haus + ".wav]");  // kept by the send that followed
    std::ifstream f(d / "anki.log");
    std::string log((std::istreambuf_iterator<char>(f)), {});
    CHECK(log.find("merged " + haus + ": kept 5, deleted [7,9]") != std::string::npos);
  }
  wipe(d);
}
