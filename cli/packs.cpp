// packs: list, status, install, remove, update.
#include <algorithm>
#include <format>
#include <iostream>
#include "command.hpp"
#include "lexipeek/packs.hpp"

namespace lexipeek::cli {

namespace {

std::string mb(std::int64_t bytes) { return std::format("{:.1f} MB", static_cast<double>(bytes) / 1e6); }

Packs packs(const Args &a) { return Packs(a.data, a.opt("--base", PACKS_BASE)); }

int install(const Packs &p, const Component &c) {
  std::cerr << c.id << " (" << mb(c.size) << ")\n";
  int last = -1;
  auto s = p.install(c, [&](std::int64_t done, std::int64_t total) {
    int pct = total > 0 ? static_cast<int>(done * 100 / total) : 0;
    if (pct != last) std::cerr << "\r  " << (last = pct) << "%" << std::flush;
  });
  std::cerr << "\n";
  if (!s.ok()) std::cerr << c.id << ": " << (s.cancelled ? "cancelled" : s.error) << "\n";
  return s.ok() ? 0 : 1;
}

int run(const Args &a) {
  if (a.rest.empty()) {
    std::cerr << "packs: list | status | install <id>... | remove <id>... | update\n";
    return 2;
  }
  auto p = packs(a);
  auto cmd = a.rest[0];
  if (cmd == "remove") {
    int rc = 0;
    for (size_t i = 1; i < a.rest.size(); i++)
      if (auto s = p.remove(a.rest[i]); !s.ok()) {
        std::cerr << a.rest[i] << ": " << s.error << "\n";
        rc = 1;
      }
    return rc;
  }
  auto f = cmd == "status" ? Fetched{p.cached(), ""} : p.fetch();
  if (!f.error.empty()) std::cerr << f.error << "\n";
  if (!f.manifest.error.empty()) {
    std::cerr << f.manifest.error << "\n";
    return 1;
  }
  auto &m = f.manifest;
  auto have = p.installed();
  auto ups = updates(m, have);
  auto updated = [&](const std::string &id) {
    for (auto &u : ups)
      if (u.id == id) return true;
    return false;
  };
  if (cmd == "list" || cmd == "status") {
    for (auto &c : m.components) {
      bool in = have.contains(c.id);
      if (cmd == "status" && !in) continue;
      auto state = !in ? "" : updated(c.id) ? "update" : "installed";
      std::cout << std::format("{:<40} {:<7} {:>10}  {}\n", c.id, c.kind, mb(c.size), state);
    }
    if (cmd == "status")
      for (auto &[id, sha] : have)
        if (!std::any_of(m.components.begin(), m.components.end(), [&](auto &c) { return c.id == id; })) std::cout << id << "  (not in the list)\n";
    if (cmd == "list")
      for (auto &pr : pairs(m))
        if (auto l = level(m, pr.from, pr.to); l != Level::none) std::cout << std::format("{}-{}: {}\n", pr.from, pr.to, level_name(l));
    return 0;
  }
  if (cmd == "install") {
    int rc = 0;
    for (size_t i = 1; i < a.rest.size(); i++) {
      auto c = std::find_if(m.components.begin(), m.components.end(), [&](auto &x) { return x.id == a.rest[i]; });
      if (c == m.components.end()) {
        std::cerr << a.rest[i] << ": not in the list\n";
        rc = 1;
      } else rc |= install(p, *c);
    }
    return rc;
  }
  if (cmd == "update") {
    int rc = 0;
    for (auto &c : ups) rc |= install(p, c);
    if (ups.empty()) std::cerr << "Up to date\n";
    return rc;
  }
  std::cerr << "packs: unknown command " << cmd << "\n";
  return 2;
}

const Register reg{{"packs", "list | status | install <id>... | remove <id>... | update [--base URL]", {}, run}};

}  // namespace

}  // namespace lexipeek::cli
