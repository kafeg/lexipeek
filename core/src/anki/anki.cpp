// Anki through AnkiConnect (API version 6): notes from the store, the sent record, the protocol, the sender's thread.
#include "lexipeek/anki.hpp"
#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <nlohmann/json.hpp>
#include <sha256.h>
#include "lexipeek/l10n.hpp"
#include "lexipeek/speech.hpp"
#include "lexipeek/store.hpp"
#include "packs/http.hpp"
#include "text/text.hpp"

namespace lexipeek::anki {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr std::size_t batch = 50;

const char *front_html = R"(<div class="front">{{Front}}</div>{{Audio}})";
const char *back_html =
    R"({{FrontSide}}<hr id=answer><div class="back">{{Back}}</div>{{#Example}}<div class="example">{{Example}}</div>{{/Example}})";
const char *css =
    ".card { font-family: arial, sans-serif; font-size: 20px; text-align: center; color: black; background: white; }\n"
    ".front { font-size: 26px; }\n.back { font-size: 18px; }\n.head { font-size: 16px; color: #555; margin-bottom: 4px; }\n"
    ".example { font-size: 15px; color: #666; font-style: italic; margin-top: 10px; }\n"
    ".nightMode .head, .nightMode .example { color: #aaa; }\n";

std::string html(const std::string &s) {
  std::string out;
  for (char c : text::escape(s)) out += c == '\n' ? std::string("<br>") : std::string(1, c);
  return out;
}

// The store's line of a word, "das Haus (Hauses, pl. Häuser) — house, home": the header apart from the meaning.
std::string back_of(const std::string &line) {
  auto dash = line.find(" — ");
  if (dash == std::string::npos) return html(line);
  return "<div class=\"head\">" + html(line.substr(0, dash)) + "</div>" + html(line.substr(dash + 5));
}

std::string reading_of(const fs::path &data, const std::string &text) {
  std::error_code ec;
  std::vector<fs::path> wavs;
  for (auto &e : fs::directory_iterator(speech::saved_dir(data, text), ec))
    if (e.path().extension() == ".wav") wavs.push_back(e.path());
  std::sort(wavs.begin(), wavs.end());
  return wavs.empty() ? "" : text::utf8_of(wavs.front());
}

std::string base64(const std::string &in) {
  static const char *abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  std::size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    auto n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
    out += {abc[(n >> 18) & 63], abc[(n >> 12) & 63], abc[(n >> 6) & 63], abc[n & 63]};
  }
  if (i + 1 == in.size()) {
    auto n = static_cast<unsigned char>(in[i]) << 16;
    out += {abc[(n >> 18) & 63], abc[(n >> 12) & 63], '=', '='};
  } else if (i + 2 == in.size()) {
    auto n = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
    out += {abc[(n >> 18) & 63], abc[(n >> 12) & 63], abc[(n >> 6) & 63], '='};
  }
  return out;
}

std::string file_bytes(const std::string &path) {
  std::ifstream f(text::path_of(path), std::ios::binary);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}

// One action's answer: its result, or why not.
struct Answer {
  Code code = Code::ok;
  json result;
  std::string error;
};

Code code_of(const std::string &error) {
  auto e = text::lower(error);
  return e.find("api key") != std::string::npos || e.find("apikey") != std::string::npos ? Code::need_key : Code::refused;
}

json request(const std::string &action, json params, const std::string &key) {
  json j{{"action", action}, {"version", 6}, {"params", std::move(params)}};
  if (!key.empty()) j["key"] = key;
  return j;
}

// The envelope {"result": …, "error": …} (a nested one in multi too).
Answer envelope(const json &j) {
  if (!j.is_object()) return {Code::refused, {}, "?"};
  if (auto e = j.find("error"); e != j.end() && e->is_string()) return {code_of(e->get<std::string>()), {}, e->get<std::string>()};
  return {Code::ok, j.value("result", json()), ""};
}

Answer post(const AnkiSettings &s, const std::string &key, const Transport &t, const std::string &action, json params = json::object()) {
  auto r = t(s.url, request(action, std::move(params), key).dump());
  if (!r.error.empty() || r.status == 0) return {Code::not_running, {}, r.error};
  if (r.status == 401 || r.status == 403) return {Code::need_key, {}, std::to_string(r.status)};
  try {
    return envelope(json::parse(r.body));
  } catch (const std::exception &) {
    return {Code::refused, {}, "HTTP " + std::to_string(r.status)};
  }
}

// Several actions in one request; each answer in its place (multi goes on after an item's error).
std::vector<Answer> multi(const AnkiSettings &s, const std::string &key, const Transport &t, const std::vector<json> &actions, Answer &whole) {
  json list = json::array();
  for (auto &a : actions) list.push_back(a);
  whole = post(s, key, t, "multi", {{"actions", list}});
  std::vector<Answer> out;
  if (whole.code != Code::ok) return out;
  for (auto &x : whole.result) out.push_back(envelope(x));
  out.resize(actions.size(), {Code::refused, {}, "?"});
  return out;
}

std::string text_of(Code c, const std::string &error) {
  switch (c) {
    case Code::ok: return "";
    case Code::off: return tr("anki_off");
    case Code::not_running: return tr("anki_not_running");
    case Code::need_key: return tr("anki_need_key");
    case Code::old: return tr("anki_old");
    case Code::model: return tr("anki_model_changed", {model_name, error});
    case Code::refused: return tr("anki_refused", {error});
  }
  return error;
}

// The note type made when missing; a "Vocabulary" without our fields is not touched.
Answer ensure_model(const AnkiSettings &s, const std::string &key, const Transport &t) {
  auto names = post(s, key, t, "modelNames");
  if (names.code != Code::ok) return names;
  bool have = names.result.is_array() && std::find(names.result.begin(), names.result.end(), json(model_name)) != names.result.end();
  if (!have) {
    json tpl = json::array({{{"Name", "Card 1"}, {"Front", front_html}, {"Back", back_html}}});
    return post(s, key, t, "createModel", {{"modelName", model_name}, {"inOrderFields", fields}, {"css", css}, {"cardTemplates", tpl}});
  }
  auto got = post(s, key, t, "modelFieldNames", {{"modelName", model_name}});
  if (got.code != Code::ok) return got;
  for (auto &f : fields)
    if (!got.result.is_array() || std::find(got.result.begin(), got.result.end(), json(f)) == got.result.end()) return {Code::model, {}, f};
  return {};
}

}  // namespace

std::string key_of(const std::string &text, const std::string &lang, bool history) {
  SHA256 h;
  return (history ? "h-" : "") + text::lower(lang) + "-" + h(text::norm_key(text)).substr(0, 32);
}

std::vector<Note> notes(const Store &store, const AnkiSettings &s, const fs::path &data) {
  std::vector<Note> out;
  auto audio = [&](const std::string &text) { return s.audio ? reading_of(data, text) : ""; };
  for (auto &x : store.saved()) {
    bool word = x.kind == "word";
    if (word ? !s.words_on : !s.phrases_on) continue;
    out.push_back({key_of(x.text, x.lang), word ? s.words : s.phrases, html(x.text), back_of(x.meaning), "", audio(x.text), word ? "word" : "phrase"});
  }
  if (s.history_on)
    for (auto &x : store.history())
      out.push_back({key_of(x.text, x.lang, true), s.history, html(x.text), back_of(x.meaning), "", audio(x.text), "history"});
  return out;
}

std::string digest(const Note &n) {
  SHA256 h;
  return h(n.front + "\x1f" + n.back + "\x1f" + n.example + "\x1f" + n.audio).substr(0, 16);
}

Sent read_sent(const fs::path &data) {
  Sent out;
  std::ifstream f(data / "anki.json");
  if (!f) return out;
  try {
    auto j = json::parse(f).value("sent", json::object());
    for (auto &[k, v] : j.items())
      if (v.is_string()) out[k] = v.get<std::string>();
  } catch (const std::exception &) {
  }
  return out;
}

std::string write_sent(const fs::path &data, const Sent &sent) {
  std::error_code ec;
  fs::create_directories(data, ec);
  auto tmp = data / "anki.json.tmp";
  {
    std::ofstream f(tmp, std::ios::binary);
    if (!f) return tr("err_write", {text::utf8_of(tmp.filename())});
    f << json{{"sent", sent}}.dump() << "\n";
  }
  fs::rename(tmp, data / "anki.json", ec);
  return ec ? ec.message() : "";
}

std::vector<Note> backlog(const std::vector<Note> &all, const Sent &sent) {
  std::vector<Note> out;
  for (auto &n : all)
    if (auto i = sent.find(n.key); i == sent.end() || i->second != digest(n)) out.push_back(n);
  return out;
}

Transport default_transport() {
  return [](const std::string &url, const std::string &body) {
    auto r = http::request("POST", url, {{"Content-Type", "application/json"}}, body, 30);
    return Reply{r.status, r.body, r.error};
  };
}

Check check(const AnkiSettings &s, const std::string &key, const Transport &t) {
  auto v = post(s, key, t, "version");
  if (v.code != Code::ok) return {v.code, text_of(v.code, v.error)};
  int version = v.result.is_number_integer() ? v.result.get<int>() : 0;
  if (version < 6) return {Code::old, text_of(Code::old, "")};
  auto decks = post(s, key, t, "deckNames");
  if (decks.code != Code::ok) return {decks.code, text_of(decks.code, decks.error)};
  auto models = post(s, key, t, "modelNames");
  if (models.code != Code::ok) return {models.code, text_of(models.code, models.error)};
  bool have = std::find(models.result.begin(), models.result.end(), json(model_name)) != models.result.end();
  if (have) {
    auto got = post(s, key, t, "modelFieldNames", {{"modelName", model_name}});
    for (auto &f : fields)
      if (got.code == Code::ok && std::find(got.result.begin(), got.result.end(), json(f)) == got.result.end())
        return {Code::model, text_of(Code::model, f)};
  }
  return {Code::ok, tr("anki_connected", {std::to_string(version), trn("anki_decks", static_cast<long long>(decks.result.size()))})};
}

Result send(const std::vector<Note> &notes, const AnkiSettings &s, const std::string &key, const Transport &t, Sent &sent) {
  Result r;
  auto fail = [&](const Answer &a) {
    r.code = a.code, r.error = text_of(a.code, a.error), r.failed = static_cast<int>(notes.size()) - r.added - r.updated;
    return r;
  };
  if (notes.empty()) return r;
  auto v = post(s, key, t, "version");
  if (v.code != Code::ok) return fail(v);
  if (!v.result.is_number_integer() || v.result.get<int>() < 6) return fail({Code::old, {}, ""});
  if (auto m = ensure_model(s, key, t); m.code != Code::ok) return fail(m);
  std::set<std::string> decks;
  for (auto &n : notes) decks.insert(n.deck);
  std::vector<json> made;
  for (auto &d : decks) made.push_back(request("createDeck", {{"deck", d}}, key));
  Answer whole;
  multi(s, key, t, made, whole);
  if (whole.code != Code::ok) return fail(whole);

  for (std::size_t at = 0; at < notes.size(); at += batch) {
    std::vector<Note> part(notes.begin() + static_cast<std::ptrdiff_t>(at), notes.begin() + static_cast<std::ptrdiff_t>(std::min(notes.size(), at + batch)));
    // Each note's Key → the notes that have it (more than one when two computers added it before Anki synced).
    std::string query = std::string("\"note:") + model_name + "\" (";
    for (std::size_t i = 0; i < part.size(); i++) query += (i ? " OR " : "") + std::string("Key:") + part[i].key;
    auto found = post(s, key, t, "findNotes", {{"query", query + ")"}});
    if (found.code != Code::ok) return fail(found);
    std::map<std::string, std::vector<json>> ids;
    if (found.result.is_array() && !found.result.empty()) {
      auto info = post(s, key, t, "notesInfo", {{"notes", found.result}});
      if (info.code != Code::ok) return fail(info);
      for (auto &n : info.result)
        if (n.is_object() && n.contains("fields")) ids[n["fields"].value("Key", json::object()).value("value", "")].push_back(n["noteId"]);
    }
    std::vector<json> media;
    std::vector<std::size_t> with_audio;
    for (std::size_t i = 0; i < part.size(); i++) {
      if (part[i].audio.empty()) continue;
      auto bytes = file_bytes(part[i].audio);
      if (bytes.empty()) continue;
      media.push_back(request("storeMediaFile", {{"filename", "lexipeek_" + part[i].key + ".wav"}, {"data", base64(bytes)}}, key));
      with_audio.push_back(i);
    }
    std::vector<std::string> sound(part.size());
    auto stored = media.empty() ? std::vector<Answer>{} : multi(s, key, t, media, whole);
    for (std::size_t k = 0; k < stored.size(); k++)
      if (stored[k].code == Code::ok && stored[k].result.is_string()) sound[with_audio[k]] = "[sound:" + stored[k].result.get<std::string>() + "]";

    std::vector<json> writes;
    std::vector<std::pair<std::size_t, bool>> whose;  // the note and whether it is an update
    for (std::size_t i = 0; i < part.size(); i++) {
      auto &n = part[i];
      json f{{"Front", n.front}, {"Back", n.back}, {"Audio", sound[i]}, {"Example", n.example}, {"Key", n.key}};
      if (auto it = ids.find(n.key); it != ids.end()) {
        for (auto &id : it->second) {
          writes.push_back(request("updateNoteFields", {{"note", {{"id", id}, {"fields", f}}}}, key));
          whose.push_back({i, true});
        }
      } else {
        json note{{"deckName", n.deck}, {"modelName", model_name}, {"fields", f}, {"tags", {"lexipeek"}}, {"options", {{"allowDuplicate", true}}}};
        writes.push_back(request("addNote", {{"note", note}}, key));
        whose.push_back({i, false});
      }
    }
    auto done = multi(s, key, t, writes, whole);
    if (whole.code != Code::ok) return fail(whole);
    std::vector<int> bad(part.size(), 0), good(part.size(), 0);
    for (std::size_t k = 0; k < done.size(); k++) {
      auto [i, update] = whose[k];
      if (done[k].code == Code::ok) {
        good[i] = update ? 2 : 1;
      } else {
        bad[i] = 1;
        if (r.error.empty()) r.code = done[k].code, r.error = text_of(done[k].code, done[k].error);
      }
    }
    for (std::size_t i = 0; i < part.size(); i++) {
      if (bad[i]) r.failed++;
      else if (good[i]) {
        (good[i] == 2 ? r.updated : r.added)++;
        sent[part[i].key] = digest(part[i]);
      }
    }
  }
  if (s.sync && r.added + r.updated > 0) post(s, key, t, "sync");
  if (r.failed == 0) r.code = Code::ok;
  return r;
}

struct Sender::Impl {
  Impl(Store &st, fs::path d, Transport tr, std::chrono::milliseconds e) : store(st), data(std::move(d)), t(std::move(tr)), every(e), sent(read_sent(data)) {}
  Store &store;
  fs::path data;
  Transport t;
  std::chrono::milliseconds every;
  mutable std::mutex mu;
  std::condition_variable cv;
  AnkiSettings s;
  std::string key;
  Sent sent;
  Sent failed;  // Key → the digest that failed: not tried again until it changes or "Send now"
  Status st;
  std::chrono::steady_clock::time_point retry{};
  bool now = false, stop = false;
  std::thread worker;

  Status round() {
    std::unique_lock g(mu);
    auto set = s;
    auto k = key;
    bool forced = std::exchange(now, false);
    if (forced) failed.clear(), retry = {};
    st.on = set.on;
    if (!set.on) {
      st.waiting = 0;
      return st;
    }
    g.unlock();
    auto all = notes(store, set, data);
    g.lock();
    auto wait = backlog(all, sent);
    st.waiting = static_cast<int>(wait.size());
    std::erase_if(wait, [&](const Note &n) {
      auto i = failed.find(n.key);
      return i != failed.end() && i->second == digest(n);
    });
    if (wait.empty() || std::chrono::steady_clock::now() < retry) return st;
    st.sending = true;
    auto out = sent;
    g.unlock();
    auto r = send(wait, set, k, t, out);
    g.lock();
    sent = out;
    write_sent(data, sent);
    for (auto &n : wait)
      if (auto i = sent.find(n.key); i == sent.end() || i->second != digest(n)) failed[n.key] = digest(n);
    // Anki not answering (or not set up) is tried again; a note it refused waits for a change or "Send now".
    if (r.code == Code::not_running || r.code == Code::need_key || r.code == Code::old || r.code == Code::model) {
      for (auto &n : wait) failed.erase(n.key);
      retry = std::chrono::steady_clock::now() + std::chrono::seconds(r.code == Code::not_running ? 15 : 60);
    }
    st.sending = false;
    st.code = r.code;
    st.text = r.added + r.updated > 0 || r.failed == 0 ? tr("anki_sent_line", {digits(r.added), digits(r.updated)}) : "";
    if (r.failed > 0) st.text += (st.text.empty() ? "" : " · ") + r.error;
    st.waiting = static_cast<int>(backlog(all, sent).size());
    return st;
  }
};

Sender::Sender(Store &store, fs::path data, Transport t, std::chrono::milliseconds every)
    : impl_(std::make_unique<Impl>(store, std::move(data), std::move(t), every)) {}

Sender::~Sender() {
  {
    std::lock_guard g(impl_->mu);
    impl_->stop = true;
  }
  impl_->cv.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
}

void Sender::configure(const AnkiSettings &s) {
  {
    std::lock_guard g(impl_->mu);
    bool again = s.url != impl_->s.url || (s.on && !impl_->s.on);
    impl_->s = s;
    if (again) impl_->retry = {}, impl_->failed.clear();
  }
  impl_->cv.notify_all();
}

void Sender::set_key(const std::string &key) {
  {
    std::lock_guard g(impl_->mu);
    if (key != impl_->key) impl_->retry = {};
    impl_->key = key;
  }
  impl_->cv.notify_all();
}

void Sender::start() {
  std::lock_guard g(impl_->mu);
  if (impl_->worker.joinable()) return;
  impl_->worker = std::thread([i = impl_.get()] {
    std::unique_lock g(i->mu);
    while (!i->stop) {
      g.unlock();
      i->round();
      g.lock();
      i->cv.wait_for(g, i->every, [i] { return i->stop || i->now; });
    }
  });
}

void Sender::send_now() {
  {
    std::lock_guard g(impl_->mu);
    impl_->now = true;
  }
  impl_->cv.notify_all();
}

Status Sender::status() const {
  std::lock_guard g(impl_->mu);
  auto st = impl_->st;
  // Not started (snapshots): what waits, worked out here.
  if (!impl_->worker.joinable() && impl_->s.on) st.on = true, st.waiting = static_cast<int>(backlog(notes(impl_->store, impl_->s, impl_->data), impl_->sent).size());
  return st;
}

bool Sender::sent(const std::string &key) const {
  std::lock_guard g(impl_->mu);
  return impl_->sent.count(key) > 0;
}

Status Sender::run_once() { return impl_->round(); }

}  // namespace lexipeek::anki
