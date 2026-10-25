// A lexipeek-cli command: one file per group of commands, each registers itself (no shared list to edit).
#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace lexipeek::cli {

struct Args {
  std::string exe;                          // argv[0], for commands that run themselves
  std::filesystem::path data;               // --data or the platform's data dir
  std::map<std::string, std::string> opts;  // "--from" → "de"; a flag → "1"
  std::vector<std::string> rest;
  bool has(const std::string &k) const { return opts.contains(k); }
  std::string opt(const std::string &k, const std::string &def = "") const {
    auto i = opts.find(k);
    return i == opts.end() ? def : i->second;
  }
  std::string text() const;  // rest joined, or stdin when there is none
};

struct Command {
  std::string name, usage;      // usage: one line after the name, e.g. "--from de --to ru [TEXT...]"
  std::set<std::string> flags;  // options without a value
  std::function<int(const Args &)> run;
};

std::map<std::string, Command> &commands();

struct Register {
  explicit Register(Command c) { commands()[c.name] = std::move(c); }
};

}  // namespace lexipeek::cli
