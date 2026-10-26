// The facade for Swift: plain functions and copyable handles (Swift cannot build std::function or std::filesystem::path).
#pragma once
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "lexipeek/core.hpp"
#include "lexipeek/packs.hpp"
#include "lexipeek/settings.hpp"
#include "lexipeek/views.hpp"

namespace lexipeek::mac {

using Strings = std::vector<std::string>;
using Rows = std::vector<PairRow>;
using Recents = std::vector<Seen>;

inline std::filesystem::path dir(const std::string &data) { return data.empty() ? default_data_dir() : std::filesystem::path(data); }
inline std::string data_dir() { return default_data_dir().string(); }
inline Device device(int ram_mb, bool dotprod) { return {ram_mb, dotprod ? Strings{"asimddp"} : Strings{}}; }

inline Settings settings(const std::string &data) { return load_settings(dir(data)); }
inline std::string save(const std::string &data, const Settings &s) { return save_settings(dir(data), s); }
inline std::string method_of(const Settings &s) { return s.method ? std::string(method_name(*s.method)) : ""; }
inline void set_method(Settings &s, const std::string &m) { s.method = method_from(m); }

// One Core with the settings' answer language and method.
struct Engine {
  std::shared_ptr<Core> core;

  static Engine make(const std::string &data, const Device &d) {
    auto s = load_settings(dir(data));
    CoreOptions o;
    o.data = dir(data);
    o.answer = s.answer;
    o.method = s.method;
    o.device = d;
    return {std::make_shared<Core>(std::move(o))};
  }
  Card lookup(const std::string &text, Source source) const { return core->lookup({text, source, ""}); }
  Translation translate(const Card &card, Method m) const { return core->translate(card, m); }
  Recents recents(int n) const { return core->recents(n); }
  void prepare() const { core->prepare(); }
  void reload() const { core->reload(); }
  void release() const { core->release(); }
  void cancel() const { core->cancel(); }
};

inline bool has_method(const Card &c) { return c.method.has_value(); }
inline Method method_of(const Card &c) { return c.method.value_or(Method::fast); }
inline bool has_part(const std::optional<PackPart> &p) { return p.has_value(); }
inline PackPart part(const std::optional<PackPart> &p) { return p.value_or(PackPart{}); }

// A download's progress and cancel, shared between the worker and the UI.
struct Download {
  std::shared_ptr<std::atomic<std::int64_t>> done = std::make_shared<std::atomic<std::int64_t>>(0);
  std::shared_ptr<std::atomic<std::int64_t>> total = std::make_shared<std::atomic<std::int64_t>>(0);
  std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false);
  std::int64_t get_done() const { return *done; }
  std::int64_t get_total() const { return *total; }
  void cancel() const { *stop = true; }
};

inline Rows rows(const std::string &data, const Device &d) {
  Packs p(dir(data));
  return pack_rows(p.cached(), p.installed(), d);
}

// The release's list (network); "" or why it is not fresh.
// Answer languages the packs offer (codes), and a language's English name.
inline Strings answers(const std::string &data) {
  Strings out;
  for (auto &p : pairs(Packs(dir(data)).cached()))
    if (std::find(out.begin(), out.end(), p.to) == out.end()) out.push_back(p.to);
  std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return language_name(a) < language_name(b); });
  return out;
}
inline std::string name_of(const std::string &code) { return language_name(code); }

inline std::string refresh(const std::string &data) { return Packs(dir(data)).fetch().error; }

// What a pair needs that is not installed yet.
inline Strings pair_ids(const std::string &data, const std::string &from, const std::string &to) {
  Packs p(dir(data));
  auto have = p.installed();
  Strings ids;
  for (auto &c : needed(p.cached(), {{from, to}}))
    if (!have.count(c.id)) ids.push_back(c.id);
  return ids;
}

// What removing a pair frees: its parts no other installed pair needs.
inline Strings freed_ids(const std::string &data, const std::string &from, const std::string &to, const Device &d) {
  Packs p(dir(data));
  auto m = p.cached();
  std::vector<LangPair> installed;
  for (auto &r : pack_rows(m, p.installed(), d))
    if (r.installed) installed.push_back({r.from, r.to});
  Strings ids;
  for (auto &c : freed(m, installed, {from, to})) ids.push_back(c.id);
  return ids;
}

// Blocking: the components one after another, progress over all of them.
inline Status install(const std::string &data, const Strings &ids, Download dl) {
  Packs p(dir(data));
  auto m = p.cached();
  std::int64_t total = 0, base = 0;
  for (auto &c : m.components)
    for (auto &id : ids)
      if (c.id == id) total += c.size;
  *dl.total = total;
  for (auto &id : ids) {
    std::int64_t size = 0;
    for (auto &c : m.components)
      if (c.id == id) size = c.size;
    auto s = p.install(id, [&](std::int64_t done, std::int64_t) { *dl.done = base + done; }, [&] { return dl.stop->load(); });
    if (!s.ok()) return s;
    base += size;
  }
  return {};
}

inline Status remove(const std::string &data, const Strings &ids) {
  Packs p(dir(data));
  for (auto &id : ids)
    if (auto s = p.remove(id); !s.ok()) return s;
  return {};
}

}  // namespace lexipeek::mac
