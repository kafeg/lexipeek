// The sync log: lines, the hybrid logical clock, reading, appending, merging (InkPeek's docs/formats.md).
#pragma once
#include <algorithm>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
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
};

// Order of lines: t, then the device.
bool before(const Line &a, const Line &b);

// nullopt: a line to skip (not JSON, v ≠ 1, unknown op, no t, d or text).
std::optional<Line> parse(const std::string &s);
std::string format(const Line &l);  // one JSON line, without "\n"

// Complete lines of [file] (a last line without "\n" is not one).
std::vector<std::string> lines_of(const std::filesystem::path &file);
// Every *.jsonl in [folder]: parsed, equal lines once, in order.
std::vector<Line> read_folder(const std::filesystem::path &folder);
// Appends [line] + "\n"; a file that does not end with "\n" is cut back to its last "\n" first.
std::string append(const std::filesystem::path &file, const std::string &line);

// Lines that merge to the same state as [lines] (in order) with any other logs: per text its last saved.*, its last
// history.del, per device its last history.seen after that del, per answer key its last ai.put.
std::vector<Line> compact(const std::vector<Line> &lines);
// Compacts [file] when it has at least [min] lines and twice as many as it compacts to: written whole, then renamed.
// Returns "" or the error.
std::string compact_file(const std::filesystem::path &file, size_t min = 1000);

// The merge rules over lines in order.
Records merge(const std::vector<Line> &lines);
// The device's count of looks of [text] since its last history.del ([lines]: of that text, in order).
std::int64_t own_count(const std::vector<Line> &lines, const std::string &device);

}  // namespace lexipeek::store
