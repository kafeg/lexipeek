// The facade for shells that cannot take the rest as is (Swift): the data dir as a string ("" = the default), plain
// structs, copyable handles, progress by polling — no std::function, std::filesystem::path or std::optional here.
#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "lexipeek/core.hpp"
#include "lexipeek/packs.hpp"
#include "lexipeek/settings.hpp"
#include "lexipeek/views.hpp"

namespace lexipeek::app {

std::string data_dir();
// This machine for the packs' requirements: memory and the int8 dot product (Apple Silicon: "asimddp").
Device device(int ram_mb, bool dotprod);

Settings settings(const std::string &data);
std::string save(const std::string &data, const Settings &s);
// The method's id ("fast", "direct", "llm"); "" for none.
std::string method_id(const Settings &s);
std::string method_id(Method m);
void set_method(Settings &s, const std::string &id);
std::string language(const std::string &code);

// One Core shared by the shell's queues: a copyable handle.
struct Engine {
  std::shared_ptr<Core> core;

  // With the settings of the data dir.
  static Engine make(const std::string &data, const Device &d);
  // The settings changed on disk: the next cards use them (no reload of the library or the store).
  void configure(const std::string &data) const;
  Card lookup(const std::string &text, Source source) const;
  Translation translate(const Card &card, Method m) const;
  SectionPage section(const Card &card, int entry, const std::string &key, int page) const;
  std::vector<Seen> recents(int n) const;
  void prepare() const;
  void reload() const;
  void release() const;
  void cancel() const;
};

// Settings → Packs.
std::vector<PairRow> rows(const std::string &data, const Device &d);
std::vector<std::string> answers(const std::string &data);
std::vector<std::string> missing_ids(const std::string &data, const std::string &from, const std::string &to);
std::vector<std::string> freed_ids(const std::string &data, const std::string &from, const std::string &to);
// The release's list (network): "" or why it is not fresh.
std::string refresh(const std::string &data);

// A download's progress and cancel, shared between the worker and the UI (polled).
struct Download {
  std::shared_ptr<std::atomic<std::int64_t>> done = std::make_shared<std::atomic<std::int64_t>>(0);
  std::shared_ptr<std::atomic<std::int64_t>> total = std::make_shared<std::atomic<std::int64_t>>(0);
  std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false);
  std::int64_t get_done() const { return *done; }
  std::int64_t get_total() const { return *total; }
  void cancel() const { *stop = true; }
};

// Blocking: the components one after another, progress over all of them.
Status install(const std::string &data, const std::vector<std::string> &ids, const Download &dl);
Status remove(const std::string &data, const std::vector<std::string> &ids);

}  // namespace lexipeek::app
