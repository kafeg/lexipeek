// Records (history, saved, answers): written to the device's sync log, read from the SQLite view of all logs.
// Format and merge rules: InkPeek's docs/formats.md (Records, Sync log).
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lexipeek {

struct Saved {
  std::string text, kind, lang, meaning;  // kind: "word" / "phrase"; lang: the language of meaning
  std::int64_t created = 0;               // ms
};

struct Seen {
  std::string text, kind, lang, meaning;
  std::string base;        // the base form of a looked-up form, "" = none
  std::int64_t seen = 0;   // ms of the last look
  std::int64_t count = 0;  // looks, all devices
};

struct Answer {
  std::string text, lang, section;  // section: quick, analysis, context (AI), offline-<from>
  std::int64_t ver = 0;
  std::string model, answer;
  std::int64_t created = 0;
};

// The merged state (sorted by text, then lang, section, ver).
struct Records {
  std::vector<Saved> saved;
  std::vector<Seen> history;
  std::vector<Answer> ai;
};

struct StoreOptions {
  std::filesystem::path data;  // <data>/device, <data>/lexipeek.db
  std::filesystem::path logs;  // the folder of *.jsonl; "" = <data>/log
  std::string device;          // "" = <data>/device, made once ("mac-91c0")
};

// The merged state of the logs in [folder] (every *.jsonl in it).
Records merge_folder(const std::filesystem::path &folder);
// The state as InkPeek's tests/sync/*/expected.json.
std::string records_json(const Records &r);

class Store {
 public:
  static constexpr int history_limit = 1000;

  explicit Store(StoreOptions o);  // reads the logs, rebuilds the view; failures go to error()
  ~Store();
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;

  std::string error() const;  // "" = open
  std::string device() const;

  // Writes return "" or the error.
  std::string seen(const std::string &text, const std::string &kind, const std::string &lang, const std::string &meaning,
                   const std::string &base = "");
  // A word saved as its base form ([from]: the form looked up) takes that form's answers along.
  std::string save(const std::string &text, const std::string &kind, const std::string &lang, const std::string &meaning,
                   const std::string &from = "");
  std::string unsave(const std::string &text);
  std::string put_answer(const std::string &text, const std::string &lang, const std::string &section, std::int64_t ver,
                         const std::string &model, const std::string &answer);
  std::string forget(const std::string &text);  // one history entry
  std::string clear_history();
  // Re-reads the folder (other devices' logs), rebuilds the view.
  std::string merge();

  std::optional<std::string> answer(const std::string &text, const std::string &lang, const std::string &section,
                                    std::int64_t ver) const;
  // The text's newest answer in lang, any version, any section (or that one) ("" = none): a saved or seen entry's line.
  std::string newest_answer(const std::string &text, const std::string &lang, const std::string &section = "") const;
  bool is_saved(const std::string &text) const;
  std::vector<Seen> history(int limit = history_limit) const;  // newest first
  std::vector<Saved> saved() const;                            // newest first
  // The menu's Recents: the newest looks, one per word across its forms.
  std::vector<Seen> recents(int n) const;
  // Looks of a word across its forms.
  std::int64_t looks(const std::string &word) const;
  Records records() const;

  // The view as one SQLite file (InkPeek's backup: the same tables and columns), written whole, then renamed.
  std::string backup(const std::filesystem::path &file) const;
  // What a backup (ours or InkPeek's, an older one too: the columns both have) has and this store does not, as lines of
  // this device: a text the logs do not know keeps its dates, another one gets now. added: the rows; error: "" or why.
  struct Restored {
    int added = 0;
    std::string error;
  };
  Restored restore(const std::filesystem::path &file);
  // A copy a day in [dir] (lexipeek-<date>.db, local date), the last [keep] kept; nothing while the store is empty.
  std::string daily_backup(const std::filesystem::path &dir, int keep = 5) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lexipeek
