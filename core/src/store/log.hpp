// The sync log: lines, the hybrid logical clock, device folders, merging, compaction (InkPeek's docs/formats.md,
// "Sync log"; its cases are tests/inkpeek/tests/sync).
#pragma once
#include <algorithm>
#include <atomic>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "lexipeek/store.hpp"

namespace lexipeek::store {

struct Time {
  std::int64_t ms = 0, n = 0;
  auto operator<=>(const Time &) const = default;
};

// The device's clock: grows on every line, also when the wall clock goes back.
struct Clock {
  Time last;
  Time tick(std::int64_t now);
  void see(Time t) { last = std::max(last, t); }
};

enum class Op { saved_put, saved_del, history_seen, history_del, ai_put };

struct Line {
  Time t;
  std::string d;
  Op op = Op::saved_put;
  std::string text, kind, lang, meaning, base;
  std::int64_t c = 0;
  std::string section, model, answer;
  std::int64_t ver = 0;
  std::string raw;  // the line's bytes as read (without "\n"); format() for a new one
};

// The wall clock in ms; tests may fix it (0: the real one).
std::int64_t now();
inline std::atomic<std::int64_t> fixed_now{0};

// Order of lines: t, then the device, then the bytes.
bool before(const Line &a, const Line &b);

// nullopt: a line to skip (not a JSON object, unknown op, no t, d or text).
std::optional<Line> parse(const std::string &s);
std::string format(const Line &l);  // the canonical form, without "\n"
// "2026-11.jsonl": the month file of t[0] in UTC.
std::string month_file(std::int64_t ms);

// Complete lines of [file] (a last line without "\n" is not one).
std::vector<std::string> lines_of(const std::filesystem::path &file);
// The files a reader reads in a device folder: *.jsonl, not starting with ".".
std::vector<std::filesystem::path> log_files(const std::filesystem::path &folder);
// The device folders of [root]/inkpeek-log.
std::vector<std::filesystem::path> device_folders(const std::filesystem::path &root);
// The complete lines of these roots' device folders: byte-equal lines once, parsed, in order.
std::vector<Line> read_roots(const std::vector<std::filesystem::path> &roots);
std::vector<Line> read_device(const std::filesystem::path &folder);
// Appends [line] + "\n"; a file that does not end with "\n" is cut back to its last "\n" first.
std::string append(const std::filesystem::path &file, const std::string &line);
// Written whole: <file>.tmp, then renamed.
std::string write_whole(const std::filesystem::path &file, const std::string &bytes);

// A device folder's device.json.
struct DeviceFile {
  std::string device, app, name;
  std::int64_t created = 0;
};
std::optional<DeviceFile> read_device_file(const std::filesystem::path &file);
std::string format(const DeviceFile &d);  // the canonical form, with "\n"

// Two installs with one id: keep it when the folder's newest own lines ([own], of the id, in order) are all lines this
// install wrote ([known]: its last line, or every line of its own log); none: keep.
bool keep_id(const std::vector<Line> &own, const std::set<std::string> &known);

// The lines of one device that merge to the same state with any other logs ([lines] in order).
std::vector<Line> compact(const std::vector<Line> &lines);
// Compacts the device folder when its lines are at least [min] and twice as many as it keeps: every changed month file
// written whole, an empty one deleted. Returns "" or the error.
std::string compact_folder(const std::filesystem::path &folder, size_t min = 1000);

// The merge rules over lines in order.
Records merge(const std::vector<Line> &lines);
// The device's count of looks of [text] since its last history.del ([lines]: of that text, in order).
std::int64_t own_count(const std::vector<Line> &lines, const std::string &device);

}  // namespace lexipeek::store
