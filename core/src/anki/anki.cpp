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
#include "lexipeek/ai.hpp"
#include "lexipeek/dicts.hpp"
#include "lexipeek/l10n.hpp"
#include "lexipeek/speech.hpp"
#include "lexipeek/store.hpp"
#include "packs/http.hpp"
#include "store/log.hpp"
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
  auto r = t(s.url, request(action, std::move(params), key).dump(-1, ' ', false, json::error_handler_t::replace));
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
    case Code::not_running: return tr("anki_not_running_t");
    case Code::need_key: return tr("anki_need_key_t");
    case Code::old: return tr("anki_old");
    case Code::model: return tr("anki_model_changed", {model_name, error});
    case Code::refused: return tr("anki_refused", {error});
  }
  return error;
}

// The note type made when missing; a "Peek" without our fields is not touched.
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

std::string key_of(const std::string &text, const std::string &lang, bool history, const std::string &answer) {
  SHA256 h;
  return (history ? "h-" : "") + text::lower(lang.empty() ? answer : lang) + "-" + h(text::norm_key(text)).substr(0, 32);
}

Note note_of(const std::string &text, const std::string &kind, const std::string &meaning, const std::string &lang,
             const std::string &quick, bool history, const std::string &answer) {
  auto line = text::blank(meaning) ? ai::brief(quick) : meaning;
  return {key_of(text, lang, history, answer), "", html(text), back_of(line), "", "", history ? "history" : kind, text, lang.empty() ? answer : lang};
}

std::vector<Note> notes(const Store &store, const AnkiSettings &s, const fs::path &data) {
  std::vector<Note> out;
  auto answer = load_settings(data).answer;
  // A blank line: the newest quick AI answer, kept under the language's English name (as InkPeek's).
  auto note = [&](const std::string &text, const std::string &kind, const std::string &meaning, const std::string &lang, bool history,
                  const std::string &deck) {
    auto quick = text::blank(meaning) ? store.newest_answer(text, catalog_language(lang.empty() ? answer : lang), "quick") : "";
    auto n = note_of(text, kind, meaning, lang, quick, history, answer);
    n.deck = deck, n.audio = reading_of(data, text);
    out.push_back(std::move(n));
  };
  for (auto &x : store.saved()) {
    bool word = x.kind == "word";
    if (word ? !s.words_on : !s.phrases_on) continue;
    note(x.text, word ? "word" : "phrase", x.meaning, x.lang, false, word ? s.words : s.phrases);
  }
  if (s.history_on)
    for (auto &x : store.history()) note(x.text, x.kind, x.meaning, x.lang, true, s.history);
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
  return store::write_whole(data / "anki.json", json{{"sent", sent}}.dump(-1, ' ', false, json::error_handler_t::replace) + "\n");
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
  if (version < 6) return {Code::old, text_of(Code::old, ""), version};
  auto decks = post(s, key, t, "deckNames");
  if (decks.code != Code::ok) return {decks.code, text_of(decks.code, decks.error), version};
  std::vector<std::string> names;
  for (auto &d : decks.result)
    if (d.is_string()) names.push_back(d.get<std::string>());
  auto models = post(s, key, t, "modelNames");
  if (models.code != Code::ok) return {models.code, text_of(models.code, models.error), version, names};
  bool have = std::find(models.result.begin(), models.result.end(), json(model_name)) != models.result.end();
  if (have) {
    auto got = post(s, key, t, "modelFieldNames", {{"modelName", model_name}});
    for (auto &f : fields)
      if (got.code == Code::ok && std::find(got.result.begin(), got.result.end(), json(f)) == got.result.end())
        return {Code::model, text_of(Code::model, f), version, names};
  }
  return {Code::ok, tr("anki_connected", {std::to_string(version), trn("anki_decks", static_cast<long long>(names.size()))}), version, names};
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
      json f{{"Front", n.front}, {"Back", n.back}, {"Example", n.example}, {"Key", n.key}};
      if (!sound[i].empty()) f["Audio"] = sound[i];  // no reading at hand: the note's Audio stays as it is
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
  if (r.failed == 0) r.code = Code::ok;
  return r;
}

Plan dedupe_plan(const std::vector<Found> &notes) {
  std::map<std::string, std::vector<Found>> by_key;
  for (auto &n : notes)
    if (!n.key.empty()) by_key[n.key].push_back(n);
  Plan p;
  for (auto &[k, ns] : by_key) {
    if (ns.size() < 2) continue;
    std::sort(ns.begin(), ns.end(), [](const Found &a, const Found &b) { return a.id < b.id; });
    if (ns[0].audio.empty())
      for (auto &n : ns)
        if (!n.audio.empty()) {
          p.audio[ns[0].id] = n.audio;
          break;
        }
    for (std::size_t i = 1; i < ns.size(); i++) p.deleted.push_back(ns[i].id);
  }
  std::sort(p.deleted.begin(), p.deleted.end());
  return p;
}

int dedupe(const std::vector<std::string> &keys, const AnkiSettings &s, const std::string &key, const Transport &t,
           const std::function<void(const std::string &)> &log) {
  std::set<std::string> ours(keys.begin(), keys.end());
  auto found = post(s, key, t, "findNotes", {{"query", std::string("\"note:") + model_name + "\""}});
  if (found.code != Code::ok || !found.result.is_array() || found.result.empty()) return 0;
  auto info = post(s, key, t, "notesInfo", {{"notes", found.result}});
  if (info.code != Code::ok || !info.result.is_array()) return 0;
  std::vector<Found> notes;
  std::map<long long, std::string> key_of_id;
  std::map<std::string, long long> kept;
  for (auto &n : info.result) {
    if (!n.is_object() || !n.contains("fields") || !n["noteId"].is_number_integer()) continue;
    auto field = [&](const char *f) { return n["fields"].value(f, json::object()).value("value", std::string()); };
    auto k = field("Key");
    if (!ours.count(k)) continue;
    auto id = n["noteId"].get<long long>();
    notes.push_back({id, k, field("Audio")});
    key_of_id[id] = k;
    kept[k] = kept.count(k) ? std::min(kept[k], id) : id;
  }
  auto plan = dedupe_plan(notes);
  for (auto &[id, audio] : plan.audio) post(s, key, t, "updateNoteFields", {{"note", {{"id", id}, {"fields", {{"Audio", audio}}}}}});
  std::map<std::string, json> gone;
  for (auto id : plan.deleted) gone[key_of_id[id]].push_back(id);
  int removed = 0;
  for (auto &[k, ids] : gone) {
    if (post(s, key, t, "deleteNotes", {{"notes", ids}}).code != Code::ok) continue;
    removed += static_cast<int>(ids.size());
    log("merged " + k + ": kept " + std::to_string(kept[k]) + ", deleted " + ids.dump());
  }
  return removed;
}

struct Sender::Impl {
  Impl(Store &the_store, fs::path dir, Transport to, Reader rd, std::chrono::milliseconds e)
      : store(the_store), data(std::move(dir)), t(std::move(to)), reader(std::move(rd)), every(e), sent(read_sent(data)) {}
  Store &store;
  fs::path data;
  Transport t;
  Reader reader;
  std::chrono::milliseconds every;
  mutable std::mutex mu;
  std::condition_variable cv;
  AnkiSettings s;
  std::string key;
  Sent sent;
  Sent failed;  // Key → the digest Anki refused: tried again after a while, at once when it changes or on "Send now"
  std::chrono::steady_clock::time_point refused_until{};
  std::map<std::string, std::string> audio;  // Key → its reading made for Anki
  std::set<std::string> no_audio;            // Keys whose reading cannot be made (no voice for the language)
  std::set<std::string> tried;               // Keys whose reading was tried: their note goes, without one if need be
  Status st;
  std::chrono::steady_clock::time_point retry{}, probed{}, deduped{};
  bool now = false, stop = false, more = false, unsynced = false;
  std::thread worker;

  bool voice() const { return reader.ready && reader.ready(); }

  // "Sync after sending": AnkiWeb once what waits went — a backlog sends a note a round, and a sync after each one kept
  // Anki busy ("Collection lifecycle operation is active" refused the next notes).
  void sync_when_done(const AnkiSettings &set, const std::string &k, std::unique_lock<std::mutex> &g) {
    if (!set.sync || !unsynced || more) return;
    unsynced = false;
    g.unlock();
    post(set, k, t, "sync");
    g.lock();
  }

  // What the binding did, for when nothing arrives: <data>/anki.log (the last 256 KB, then anki.log.1).
  void log(const std::string &line) {
    std::error_code ec;
    auto f = data / "anki.log";
    if (fs::file_size(f, ec) > 256 * 1024 && !ec) fs::rename(f, data / "anki.log.1", ec);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream(f, std::ios::app | std::ios::binary) << text::local_time(ms) << " " << line << "\n";
  }

  // The store's notes ([all], read without mu: the store may wait for its sync folder) with the readings made so far;
  // those still without one, unsent first.
  std::vector<Note> with_audio(std::vector<Note> all, std::vector<Note> &missing) {
    for (auto &n : all)
      if (auto i = audio.find(n.key); n.audio.empty() && i != audio.end()) n.audio = i->second;
    for (auto &n : all)
      if (n.audio.empty() && !no_audio.count(n.key)) missing.push_back(n);
    // Not tried yet first, then not sent yet: one that cannot be made now does not block the others.
    std::stable_partition(missing.begin(), missing.end(), [&](const Note &n) { return !sent.count(n.key); });
    std::stable_partition(missing.begin(), missing.end(), [&](const Note &n) { return !tried.count(n.key); });
    return all;
  }

  void count(const std::vector<Note> &all, const std::vector<Note> &wait, const std::vector<Note> &missing) {
    st.words = st.phrases = st.history = {};
    auto of = [&](const std::string &kind) -> Count & { return kind == "word" ? st.words : kind == "phrase" ? st.phrases : st.history; };
    std::set<std::string> waits, mute;
    for (auto &n : wait) waits.insert(n.key);
    for (auto &n : missing) mute.insert(n.key);
    for (auto &n : all) {
      auto &c = of(n.kind);
      c.sent += sent.count(n.key) > 0, c.waiting += waits.count(n.key) > 0, c.audio += mute.count(n.key) > 0;
    }
    st.waiting = st.words.waiting + st.phrases.waiting + st.history.waiting;
  }

  Status round() {
    std::unique_lock g(mu);
    auto set = s;
    auto k = key;
    bool forced = std::exchange(now, false);
    more = false;
    if (forced) failed.clear(), retry = {}, probed = {}, deduped = {};
    auto probe_at = probed;
    st.on = set.on;
    if (!set.on) {
      st.waiting = 0;
      return st;
    }
    g.unlock();
    bool ready = voice();
    // Anki asked every 30 s: the tab's connection line and its decks.
    if (std::chrono::steady_clock::now() >= probe_at) {
      auto c = check(set, k, t);
      g.lock();
      if (!st.probed || c.code != st.check.code) log("anki: " + c.text);
      st.check = c, st.probed = true, probed = std::chrono::steady_clock::now() + std::chrono::seconds(30);
      g.unlock();
    }
    std::vector<Note> missing;
    auto listed = notes(store, set, data);
    // Duplicates by Key (another app sent before AnkiWeb synced): every 10 min while Anki answers, and on "Send now".
    if (std::chrono::steady_clock::now() >= deduped && st.check.code == Code::ok) {
      deduped = std::chrono::steady_clock::now() + std::chrono::minutes(10);
      std::vector<std::string> keys;
      for (auto &n : listed) keys.push_back(n.key);
      dedupe(keys, set, k, t, [this](const std::string &line) { log(line); });
    }
    g.lock();
    st.voice = ready;
    auto all = with_audio(listed, missing);
    // One reading at a time, made before its note goes (notes go without one while the voice model is missing).
    if (ready && !missing.empty()) {
      auto n = missing.front();
      st.making = true;
      g.unlock();
      auto m = reader.make(n);
      g.lock();
      st.making = false;
      tried.insert(n.key);
      log("reading " + n.text + ": " + (!m.file.empty() ? "made" : m.later ? "not now" : "none for its language"));
      if (!m.file.empty()) audio[n.key] = m.file;
      else if (!m.later) no_audio.insert(n.key);
      missing.clear();
      all = with_audio(listed, missing);
      more = std::any_of(missing.begin(), missing.end(), [&](const Note &x) { return !tried.count(x.key); });
    }
    auto wait = backlog(all, sent);
    std::set<std::string> held;
    // A note waits for its reading until it was tried once; then it goes without and gets it later.
    if (ready)
      for (auto &n : missing)
        if (!tried.count(n.key)) held.insert(n.key);
    count(all, wait, missing);
    if (std::chrono::steady_clock::now() >= refused_until) failed.clear();
    std::erase_if(wait, [&](const Note &n) {
      auto i = failed.find(n.key);
      return held.count(n.key) || (i != failed.end() && i->second == digest(n));
    });
    if (wait.empty()) {
      sync_when_done(set, k, g);
      return st;
    }
    if (std::chrono::steady_clock::now() < retry) return st;
    st.sending = true;
    auto out = sent;
    g.unlock();
    auto r = send(wait, set, k, t, out);
    g.lock();
    log("sent " + std::to_string(wait.size()) + ": " + std::to_string(r.added) + " added, " + std::to_string(r.updated) + " updated, " +
        std::to_string(r.failed) + " failed" + (r.error.empty() ? "" : " — " + r.error));
    sent = out;
    write_sent(data, sent);
    if (r.added + r.updated > 0) unsynced = true;
    for (auto &n : wait)
      if (auto i = sent.find(n.key); i == sent.end() || i->second != digest(n)) failed[n.key] = digest(n);
    st.refused = static_cast<int>(failed.size());
    if (!failed.empty()) refused_until = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    // Anki not answering (or not set up) is tried again soon; a note it refused, in 2 minutes (at once when it changes).
    if (r.code == Code::not_running || r.code == Code::need_key || r.code == Code::old || r.code == Code::model) {
      for (auto &n : wait) failed.erase(n.key);
      retry = std::chrono::steady_clock::now() + std::chrono::seconds(r.code == Code::not_running ? 15 : 60);
      st.check.code = r.code, st.check.text = r.error, st.probed = true;
    }
    // A send that went: Anki answers (its line asked again next round).
    if (r.added + r.updated > 0 && st.check.code != Code::ok) st.check.code = Code::ok, probed = {};
    st.sending = false;
    st.text = r.added + r.updated > 0 || r.failed == 0 ? tr("anki_sent_line", {digits(r.added), digits(r.updated)}) : "";
    if (r.failed > 0) st.text += (st.text.empty() ? "" : " · ") + r.error;
    count(all, backlog(all, sent), missing);
    sync_when_done(set, k, g);
    return st;
  }
};

Sender::Sender(Store &store, fs::path data, Transport t, Reader reader, std::chrono::milliseconds every)
    : impl_(std::make_unique<Impl>(store, std::move(data), std::move(t), std::move(reader), every)) {}

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
    if (again) impl_->retry = {}, impl_->probed = {}, impl_->failed.clear();
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
    std::unique_lock lock(i->mu);
    while (!i->stop) {
      lock.unlock();
      try {
        i->round();
      } catch (const std::exception &e) {  // a round's surprise must not take the app down with this thread
        i->log(std::string("error: ") + e.what());
      }
      lock.lock();
      if (!i->more) i->cv.wait_for(lock, i->every, [i] { return i->stop || i->now; });
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
  std::unique_lock g(impl_->mu);
  // Not started (snapshots): what waits, worked out here; Anki not asked.
  if (!impl_->worker.joinable() && impl_->s.on) {
    auto set = impl_->s;
    g.unlock();
    auto listed = notes(impl_->store, set, impl_->data);
    g.lock();
    std::vector<Note> missing;
    auto all = impl_->with_audio(listed, missing);
    impl_->st.on = true, impl_->st.voice = impl_->voice();
    impl_->count(all, backlog(all, impl_->sent), missing);
  }
  return impl_->st;
}

bool Sender::sent(const std::string &key) const {
  std::lock_guard g(impl_->mu);
  return impl_->sent.count(key) > 0;
}

Status Sender::run_once() { return impl_->round(); }

}  // namespace lexipeek::anki
