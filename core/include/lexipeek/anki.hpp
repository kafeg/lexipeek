// Anki through its add-on AnkiConnect (its API version 6 at the root, which every AnkiConnect speaks): once the binding is
// on, saved words and phrases (and the history when set so) go to their decks by themselves while Anki runs, each with
// its reading (made by Read aloud when none is kept, one at a time); what could not go waits and goes when Anki answers.
// One note per entry by its Key field: sending again updates it, a note deleted in Anki is added again. Only to the
// address in the settings (Anki on this computer).
#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "lexipeek/settings.hpp"
#include "lexipeek/speech.hpp"

namespace lexipeek {
class Store;
}

namespace lexipeek::anki {

// The note type, shared with InkPeek: its name and fields (Key in no template).
inline constexpr const char *model_name = "Peek";
inline const std::vector<std::string> fields{"Front", "Back", "Audio", "Example", "Key"};

// One entry as a note.
struct Note {
  std::string key, deck, front, back, example;
  std::string audio;  // a reading's file ("" = none yet)
  std::string kind;   // "word", "phrase", "history"
  std::string text, lang;  // the entry as stored: its text, the answer language (settings.json's when it has none)
};

// The Key field: the answer language, then the first 32 hex digits of sha256 of InkPeek's lookup key (normKey) of the
// text; "h-" before a history note's ("ru-3fa9…", "h-ru-3fa9…"): letters, digits and "-" only, searched as they are.
// An entry without its language (InkPeek's rows before its schema 3): [answer], the app's answer language, as InkPeek.
std::string key_of(const std::string &text, const std::string &lang, bool history = false, const std::string &answer = "");
// What the binding sends from the store: saved words, saved phrases, the history, each when on (its reading: the
// note's kept one under <data>/readings/saved/, if any); an entry without its language keys and reads by settings.json's
// answer (with none its reading found no language: never made).
std::vector<Note> notes(const Store &store, const AnkiSettings &s, const std::filesystem::path &data);
// A note's fields as sent: a change sends it again (the deck is not in it: a renamed deck takes only new notes).
std::string digest(const Note &n);

// What went: Key → its digest; <data>/anki.json, written whole, then renamed. Per computer (Anki's collection is).
using Sent = std::map<std::string, std::string>;
Sent read_sent(const std::filesystem::path &data);
std::string write_sent(const std::filesystem::path &data, const Sent &sent);
// Those of [all] not sent or changed since.
std::vector<Note> backlog(const std::vector<Note> &all, const Sent &sent);

enum class Code {
  ok,
  off,          // the binding is off
  not_running,  // nothing answers at the address: Anki is not running, or AnkiConnect is not installed
  need_key,     // AnkiConnect wants its key
  old,          // AnkiConnect older than API version 6
  model,        // the note type "Peek" lacks a field
  refused,      // Anki refused something (its message)
};

// One HTTP POST of [body] to [url]; tests and snapshots give their own.
struct Reply {
  long status = 0;
  std::string body, error;  // error: no answer
};
using Transport = std::function<Reply(const std::string &url, const std::string &body)>;
Transport default_transport();  // libcurl

// Settings → Anki's Check: only reads (version, decks, note types), creates nothing.
struct Check {
  Code code = Code::ok;
  std::string text;  // "Connected: AnkiConnect 6 · 14 decks", or what is wrong
  int version = 0;
  std::vector<std::string> decks = {};  // Anki's decks
};
Check check(const AnkiSettings &s, const std::string &key, const Transport &t);

// A send: decks and the note type made when missing, each note found by its Key, then updated or added; [sent] gets
// those that went. The audio goes into Anki's media as lexipeek_<its Key>.wav.
struct Result {
  Code code = Code::ok;
  int added = 0, updated = 0, failed = 0;
  std::string error;  // the first failure's text
};
Result send(const std::vector<Note> &notes, const AnkiSettings &s, const std::string &key, const Transport &t, Sent &sent);

// Two notes with one Key (LexiPeek and InkPeek sent before AnkiWeb synced: its merge goes by a GUID neither can set):
// among the "Peek" notes of [keys], the oldest (smallest id) is kept, with another's Audio when its own is empty; the
// others are deleted. InkPeek's rule, word for word. [log] gets a line per merge; the count of notes deleted.
int dedupe(const std::vector<std::string> &keys, const AnkiSettings &s, const std::string &key, const Transport &t,
           const std::function<void(const std::string &)> &log);

// Per deck: in Anki, waiting (not sent or changed since), without their reading yet.
struct Count {
  int sent = 0, waiting = 0, audio = 0;
};

// What Settings → Anki shows of the binding.
struct Status {
  bool on = false;
  Count words, phrases, history;
  int waiting = 0;       // all decks'
  bool sending = false, making = false;  // a send, a reading being made
  bool voice = true;     // Read aloud's voice model is there (else notes go without audio, and get it later)
  bool probed = false;   // Anki was asked: code says how it went
  Check check;           // the last time Anki was asked (every 30 s while on): its code, version, decks
  std::string text;      // the last send's line: "last time: 3 added, 1 updated", the error; "" before any
};

// A note's reading for the sender, from the core's Read aloud: found or made now (blocking); ready: the model is there.
struct Reader {
  std::function<speech::Made(const Note &)> make;
  std::function<bool()> ready;
};

// The binding at work: one thread that looks at the store every few seconds and sends what waits; Anki not answering
// is tried again a little later. Started by the shell (never in snapshots, tests start their own).
class Sender {
 public:
  // every: how often it looks at the store.
  Sender(Store &store, std::filesystem::path data, Transport t = default_transport(), Reader reader = {},
         std::chrono::milliseconds every = std::chrono::seconds(3));
  ~Sender();
  Sender(const Sender &) = delete;
  Sender &operator=(const Sender &) = delete;

  void configure(const AnkiSettings &s);
  void set_key(const std::string &key);  // AnkiConnect's key, from the Keychain / Credential Manager
  void start();
  void send_now();  // Settings → Anki's "Send now": at once, the failed ones too
  Status status() const;  // not started: what waits, worked out now
  bool sent(const std::string &key) const;  // Notes' mark
  // Tests: one round now, in the caller's thread.
  Status run_once();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lexipeek::anki
