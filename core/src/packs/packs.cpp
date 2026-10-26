#include <archive.h>
#include <archive_entry.h>
#include <sha256.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include "lexipeek/packs.hpp"
#include "mt/text.hpp"
#include "packs/http.hpp"

namespace lexipeek {

using nlohmann::json;

namespace {

std::string str(const json &j, const char *k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }

template <class T>
T get(const json &j, const char *k, T def) {
  return j.contains(k) && !j[k].is_null() ? j[k].get<T>() : def;
}

namespace fs = std::filesystem;

std::string read(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

// A temporary file, then a rename: a crash leaves the old file, never half of one.
bool write(const fs::path &p, const std::string &text) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  auto tmp = p;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!(f << text) || !f.flush()) return false;
  }
  fs::rename(tmp, p, ec);
  return !ec;
}

std::string sha256_of(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  SHA256 h;
  std::vector<char> buf(1 << 16);
  while (f.read(buf.data(), static_cast<std::streamsize>(buf.size())) || f.gcount() > 0) h.add(buf.data(), static_cast<size_t>(f.gcount()));
  return h.getHash();
}

// Ids and languages become paths: plain names only (a broken list must not reach outside its folder).
bool plain(const std::string &s) {
  return !s.empty() && s.find("..") == std::string::npos &&
         std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '+' || c == '-'; });
}

std::string check(const Component &c, bool install) {
  if (!plain(c.id)) return "Bad pack id " + c.id;
  // A model without its languages is removed as nothing (an old entry); installed, it would be the whole models folder.
  if (c.kind == "model" && (install || !c.from.empty() || !c.to.empty()) && !(plain(c.from) && plain(c.to))) return "Bad model " + c.id;
  return "";
}

// Where a component lives in the data dir; empty for an unknown kind.
fs::path target(const Layout &l, const Component &c) {
  if (c.kind == "dict") return l.dict(c.id);
  if (c.kind == "model") return c.from.empty() || c.to.empty() ? fs::path() : l.model(c.from, c.to);
  if (c.kind == "opus") return l.opus(c.id);
  if (c.kind == "langid") return l.langid();
  if (c.kind == "llm") return l.llm(c.id);
  return {};
}

bool archived(const std::string &kind) { return kind == "dict" || kind == "model" || kind == "opus"; }

// A .tar.zst or .zip into [dir], flat: file names only, folders dropped, only regular files.
std::string unpack(const fs::path &file, const fs::path &dir) {
  std::unique_ptr<archive, decltype(&archive_read_free)> a(archive_read_new(), archive_read_free);
  archive_read_support_filter_zstd(a.get());
  archive_read_support_format_tar(a.get());
  archive_read_support_format_zip(a.get());
  if (archive_read_open_filename(a.get(), file.string().c_str(), 1 << 16) != ARCHIVE_OK) return std::string("Cannot open the archive: ") + archive_error_string(a.get());
  archive_entry *e = nullptr;
  int r = 0;
  while ((r = archive_read_next_header(a.get(), &e)) == ARCHIVE_OK) {
    if (archive_entry_filetype(e) != AE_IFREG) continue;
    std::string name = archive_entry_pathname(e) ? archive_entry_pathname(e) : "";
    name = name.substr(name.find_last_of("/\\") == std::string::npos ? 0 : name.find_last_of("/\\") + 1);
    if (name.empty() || name == "." || name == "..") continue;
    std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
    const void *buf = nullptr;
    size_t n = 0;
    la_int64_t off = 0;
    while ((r = archive_read_data_block(a.get(), &buf, &n, &off)) == ARCHIVE_OK) out.write(static_cast<const char *>(buf), static_cast<std::streamsize>(n));
    if (r != ARCHIVE_EOF) return std::string("Broken archive: ") + archive_error_string(a.get());
    if (!out.flush()) return "Cannot write " + name;
  }
  if (r != ARCHIVE_EOF) return std::string("Broken archive: ") + archive_error_string(a.get());
  return "";
}

// [from] in place of [dest]: the old one goes aside (into tmp/) first and away last.
std::string swap(const fs::path &from, const fs::path &dest, const fs::path &tmp) {
  std::error_code ec;
  fs::create_directories(dest.parent_path(), ec);
  auto old = tmp / ("old-" + dest.filename().string());
  fs::remove_all(old, ec);
  if (fs::exists(dest) && (fs::rename(dest, old, ec), ec)) return "Cannot move " + dest.filename().string();
  if (fs::rename(from, dest, ec); ec) {
    fs::rename(old, dest, ec);
    return "Cannot move " + dest.filename().string();
  }
  fs::remove_all(old, ec);
  return "";
}

// An installed component no longer in the list, from its id ("model-de-en") for removal.
Component stale(const std::string &id) {
  Component c;
  c.id = id;
  std::vector<std::string> parts;
  std::stringstream s(id);
  for (std::string p; std::getline(s, p, '-');) parts.push_back(p);
  c.kind = parts.empty() ? "" : parts[0];
  if (c.kind == "model" || c.kind == "dict") {
    c.from = parts.size() > 1 ? parts[1] : "";
    c.to = parts.size() > 2 ? parts[2] : "";
  }
  return c;
}

const Component *find(const Manifest &m, const std::string &id) {
  for (auto &c : m.components)
    if (c.id == id) return &c;
  return nullptr;
}

}  // namespace

Manifest parse_manifest(const std::string &text) {
  Manifest m;
  try {
    auto j = json::parse(text);
    if (get<int>(j, "format", 0) != 1) return {{}, "Unknown packs.json format"};
    for (auto &c : j.at("components")) {
      Component x;
      x.id = str(c, "id");
      x.kind = str(c, "kind");
      x.from = str(c, "from");
      x.to = str(c, "to");
      auto &f = c.at("file");
      x.file = str(f, "name");
      x.sha256 = str(f, "sha256");
      x.size = get<std::int64_t>(f, "size", 0);
      x.covers = get(c, "covers", std::vector<std::string>{});
      x.tags = get(c, "tags", std::map<std::string, std::string>{});
      x.names = get(c, "names", std::map<std::string, std::string>{});
      x.ram_mb = get(c, "ram_mb", 0);
      x.cpu = get(c, "cpu", std::vector<std::string>{});
      x.name = str(c, "name");
      x.prompt = str(c, "template");
      x.mirror = str(c, "mirror");
      x.license = str(c, "license");
      x.attribution = str(c, "attribution");
      if (c.contains("stats") && c["stats"].is_object()) x.coverage = get(c["stats"], "coverage", 0.0);
      m.components.push_back(std::move(x));
    }
  } catch (const std::exception &e) {
    return {{}, std::string("Broken packs.json: ") + e.what()};
  }
  return m;
}

std::optional<Component> covering(const Manifest &m, const std::string &kind, const std::string &from, const std::string &to) {
  const auto pair = from + "-" + to;
  for (auto &c : m.components)
    if (c.kind == kind)
      for (auto &p : c.covers)
        if (p == pair) return c;
  return std::nullopt;
}

std::filesystem::path default_data_dir() {
  auto env = [](const char *k) -> std::filesystem::path {
    const char *v = std::getenv(k);  // NOLINT(concurrency-mt-unsafe)
    return v && *v ? std::filesystem::path(reinterpret_cast<const char8_t *>(v)) : std::filesystem::path();
  };
  if (auto d = env("LEXIPEEK_DATA"); !d.empty()) return d;
#if defined(_WIN32)
  return env("APPDATA") / "LexiPeek";
#elif defined(__APPLE__)
  return env("HOME") / "Library" / "Application Support" / "LexiPeek";
#else
  if (auto x = env("XDG_DATA_HOME"); !x.empty()) return x / "LexiPeek";
  return env("HOME") / ".local" / "share" / "LexiPeek";
#endif
}

std::string_view level_name(Level l) {
  switch (l) {
    case Level::full: return "full";
    case Level::basic: return "basic";
    case Level::experimental: return "experimental";
    case Level::none: break;
  }
  return "";
}

Level level(const Manifest &m, const std::string &from, const std::string &to) {
  auto d = std::find_if(m.components.begin(), m.components.end(), [&](auto &c) { return c.kind == "dict" && c.from == from && c.to == to; });
  if (d == m.components.end() || d->coverage <= 0) return Level::none;
  bool mt = true;
  for (auto &[a, b] : mt::steps(from, to)) mt = mt && find(m, "model-" + a + "-" + b);
  if (d->coverage >= 85 && mt) return Level::full;
  return d->coverage >= 60 ? Level::basic : Level::experimental;
}

std::vector<LangPair> pairs(const Manifest &m) {
  std::vector<LangPair> r;
  for (auto &c : m.components)
    if (c.kind == "dict") r.push_back({c.from, c.to});
  return r;
}

std::vector<Component> needed(const Manifest &m, const std::vector<LangPair> &ps, Method method) {
  std::set<std::string> ids;
  const char *kind = method == Method::direct ? "opus" : method == Method::llm ? "llm" : nullptr;
  for (auto &p : ps) {
    ids.insert("dict-" + p.from + "-" + p.to);
    for (auto &[a, b] : mt::steps(p.from, p.to)) ids.insert("model-" + a + "-" + b);
    ids.insert("langid");
    if (kind)
      if (auto c = covering(m, kind, p.from, p.to)) ids.insert(c->id);
  }
  std::vector<Component> r;
  for (auto &c : m.components)
    if (ids.contains(c.id)) r.push_back(c);
  return r;
}

std::vector<Component> freed(const Manifest &m, const std::vector<LangPair> &installed, const LangPair &pair, Method method) {
  std::vector<LangPair> rest;
  for (auto &p : installed)
    if (!(p == pair)) rest.push_back(p);
  std::set<std::string> keep;
  for (auto &c : needed(m, rest, method)) keep.insert(c.id);
  std::vector<Component> r;
  for (auto &c : needed(m, {pair}, method))
    if (!keep.contains(c.id)) r.push_back(c);
  return r;
}

std::vector<Component> updates(const Manifest &m, const std::map<std::string, std::string> &installed) {
  std::vector<Component> r;
  for (auto &c : m.components)
    if (auto i = installed.find(c.id); i != installed.end() && i->second != c.sha256) r.push_back(c);
  return r;
}

std::string cannot(const Component &c, const Device &d) {
  if (d.ram_mb < c.ram_mb) return std::format("Needs {:.0f} GB of memory; this machine has {:.1f} GB.", c.ram_mb / 1000.0, d.ram_mb / 1024.0);
  std::vector<std::string> missing;
  for (auto &f : c.cpu)
    if (std::find(d.features.begin(), d.features.end(), f) == d.features.end()) missing.push_back(f);
  if (missing.empty()) return "";
  if (missing == std::vector<std::string>{"asimddp"}) return "Needs a processor with int8 acceleration (dot product).";
  std::string list;
  for (auto &f : missing) list += (list.empty() ? "" : ", ") + f;
  return "Needs processor features: " + list + ".";
}

Packs::Packs(std::filesystem::path data, std::string base) : data_(std::move(data)), base_(std::move(base)) {
  if (!base_.empty() && base_.back() != '/') base_ += '/';
}

Fetched Packs::fetch() const {
  std::string body;
  auto err = http::get(base_ + "packs.json", body);
  if (err.empty()) {
    auto m = parse_manifest(body);
    // Kept for offline use; not being able to keep it must not hide the list.
    if (m.error.empty()) {
      write(layout().manifest(), body);
      return {std::move(m), ""};
    }
    err = m.error;
  }
  return {cached(), "Cannot get the packs list: " + err};
}

Manifest Packs::cached() const {
  if (!fs::exists(layout().manifest())) return {{}, "No packs list yet"};
  return parse_manifest(read(layout().manifest()));
}

std::map<std::string, std::string> Packs::installed() const {
  try {
    return json::parse(read(layout().installed())).get<std::map<std::string, std::string>>();
  } catch (const std::exception &) {
    return {};
  }
}

namespace {

std::string mark(const Layout &l, const std::map<std::string, std::string> &now) {
  return write(l.installed(), json(now).dump(1)) ? "" : "Cannot write installed.json";
}

}  // namespace

Status Packs::install(const Component &c, const Progress &progress, const Cancel &cancel) const {
  if (auto e = check(c, true); !e.empty()) return {e};
  auto l = layout();
  auto dest = target(l, c);
  if (dest.empty()) return {"Unknown pack kind " + c.kind};
  if (c.file.empty() || c.file.find_first_of("/\\") != std::string::npos) return {"Bad file name " + c.file};
  std::error_code ec;
  fs::create_directories(data_, ec);
  auto tmp = data_ / "tmp";
  // Left by an unpack that did not finish (one install at a time: none is running).
  fs::remove_all(tmp, ec);
  auto part = data_ / (c.file + ".part");
  auto have = fs::exists(part, ec) ? static_cast<std::int64_t>(fs::file_size(part, ec)) : 0;
  if (have > c.size) {
    fs::remove(part, ec);
    have = 0;
  }
  // The download, the unpacked files and the old version side by side at the peak.
  auto need = (c.kind == "llm" || c.kind == "langid" ? c.size + c.size / 10 : c.size * 3) - have;
  auto space = fs::space(data_, ec);
  if (!ec && static_cast<std::int64_t>(space.available) < need)
    return {std::format("Not enough free space: {} MB needed, {} MB free", need / 1'000'000, space.available / 1'000'000)};

  std::vector<std::string> urls;
  if (c.kind == "llm" && !c.mirror.empty()) urls.push_back(c.mirror);
  urls.push_back(base_ + c.file);
  std::string err;
  bool good = false;
  for (auto &url : urls) {
    if (!fs::exists(part, ec) || static_cast<std::int64_t>(fs::file_size(part, ec)) != c.size) {
      auto s = http::download(url, part, c.size, progress, cancel);
      if (s.cancelled) return s;
      if (!s.ok()) {
        err = s.error;
        continue;
      }
    }
    if (sha256_of(part) == c.sha256) {
      good = true;
      break;
    }
    err = "Checksum mismatch: " + c.file;
    fs::remove(part, ec);
  }
  if (!good) return {err};

  fs::create_directories(tmp, ec);
  if (archived(c.kind)) {
    auto staging = tmp / dest.filename();
    fs::create_directories(staging, ec);
    err = unpack(part, staging);
    if (err.empty()) err = swap(staging, dest, tmp);
  } else {
    err = swap(part, dest, tmp);
  }
  fs::remove(part, ec);
  fs::remove_all(tmp, ec);
  if (!err.empty()) return {err};
  auto now = installed();
  now[c.id] = c.sha256;
  return {mark(l, now)};
}

Status Packs::install(const std::string &id, const Progress &progress, const Cancel &cancel) const {
  auto m = cached();
  if (!m.error.empty()) return {m.error};
  auto c = find(m, id);
  if (!c) return {"No pack " + id + " in the list"};
  return install(*c, progress, cancel);
}

Status Packs::remove(const std::string &id) const {
  auto m = cached();
  auto f = find(m, id);
  auto c = f ? *f : stale(id);
  if (auto e = check(c, false); !e.empty()) return {e};
  std::error_code ec;
  if (auto dest = target(layout(), c); !dest.empty()) fs::remove_all(dest, ec);
  if (ec) return {"Cannot remove " + id + ": " + ec.message()};
  auto now = installed();
  if (now.erase(id) == 0) return {};
  return {mark(layout(), now)};
}

}  // namespace lexipeek
