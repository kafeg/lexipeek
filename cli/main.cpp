// lexipeek-cli: the core from a terminal; the commands are in cli/*.cpp.
#include <iostream>
#include <iterator>
#include "command.hpp"
#include "lexipeek/packs.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

namespace lexipeek::cli {

std::map<std::string, Command> &commands() {
  static std::map<std::string, Command> all;
  return all;
}

std::string Args::text() const {
  std::string t;
  for (auto &r : rest) t += (t.empty() ? "" : " ") + r;
  if (t.empty()) t.assign(std::istreambuf_iterator<char>(std::cin), {});
  return t;
}

}  // namespace lexipeek::cli

using namespace lexipeek::cli;

namespace {

int usage(int code) {
  auto &out = code ? std::cerr : std::cout;
  out << "Usage: lexipeek-cli [--data DIR] [--idle-ms N] <command> [options]\n";
  for (auto &[name, c] : commands()) out << "  " << name << " " << c.usage << "\n";
  out << "DIR: the data dir with the packs (default: the platform's, or LEXIPEEK_DATA).\n";
  return code;
}

}  // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);  // with utf8.manifest: argv and the console in UTF-8, not the code page
#endif
  Args a;
  a.exe = argv[0];
  std::string name;
  const Command *c = nullptr;
  for (int i = 1; i < argc; i++) {
    std::string s = argv[i];
    if (s.starts_with("--") && c && c->flags.contains(s)) a.opts[s] = "1";
    else if (s.starts_with("-") && s.size() > 1 && i + 1 < argc) a.opts[s] = argv[++i];
    else if (!c) {
      name = s;
      auto f = commands().find(s);
      if (f == commands().end()) return usage(s == "help" ? 0 : 2);
      c = &f->second;
    } else a.rest.push_back(s);
  }
  if (!c) return usage(0);
  a.data = a.has("--data") ? std::filesystem::path(a.opt("--data")) : lexipeek::default_data_dir();
  return c->run(a);
}
