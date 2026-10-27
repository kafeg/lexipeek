// Archives or loose StarDict files → dicts-own/<dictionary>/ (InkPeek's Importer.kt), by libarchive.
#include <archive.h>
#include <archive_entry.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <set>
#include "dict/stardict.hpp"
#include "lexipeek/dicts.hpp"
#include "lexipeek/l10n.hpp"
#include "text/text.hpp"

namespace lexipeek {

namespace fs = std::filesystem;

namespace {

const std::vector<std::string> EXT{".ifo", ".idx", ".idx.gz", ".dict", ".dict.dz", ".syn", ".syn.dz"};

std::string slurp(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::string base_of(const fs::path &ifo) { return ifo.filename().string().substr(0, ifo.filename().string().size() - 4); }

std::int64_t idx_size(const fs::path &ifo) {
  std::error_code ec;
  for (auto ext : {".idx", ".idx.gz"})
    if (auto p = ifo.parent_path() / (base_of(ifo) + ext); fs::exists(p, ec)) return static_cast<std::int64_t>(fs::file_size(p, ec));
  return -1;
}

std::string bookname(const fs::path &ifo) {
  std::ifstream f(ifo);
  for (std::string l; std::getline(f, l);)
    if (l.starts_with("bookname=")) {
      auto v = l.substr(9);
      while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
      while (!v.empty() && v.front() == ' ') v.erase(0, 1);
      if (!v.empty()) return v;
    }
  return base_of(ifo);
}

std::string safe(const std::string &s) {
  static const text::Regex bad("[\\\\/:*?\"<>|]|^\\.+");
  auto r = bad.replace(s, "_");
  if (r.size() > 80) {
    size_t n = 80;
    while (n > 0 && (static_cast<unsigned char>(r[n]) & 0xC0) == 0x80) n--;  // not inside a code point
    r.resize(n);
  }
  return r.empty() || r.find_first_not_of(' ') == std::string::npos ? "dict" : r;
}

// The .ifo last: a dictionary is found by it, so one cut short is never seen half; on a failure the new folder goes.
fs::path place(const fs::path &ifo, const std::string &name, const fs::path &target) {
  auto dir = target / safe(name);
  for (int i = 2; fs::exists(dir); i++) dir = target / (safe(name) + "-" + std::to_string(i));
  fs::create_directories(dir);
  auto base = base_of(ifo);
  try {
    for (auto &ext : EXT) {
      if (ext == ".ifo") continue;
      if (auto f = ifo.parent_path() / (base + ext); fs::exists(f)) fs::copy_file(f, dir / f.filename());
    }
    fs::copy_file(ifo, dir / ifo.filename());
  } catch (...) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    throw;
  }
  return dir;
}

Imported collect(const fs::path &from, const fs::path &target) {
  std::set<std::pair<std::string, std::int64_t>> have;
  for (auto &ifo : dict::scan(target)) have.insert({slurp(ifo), idx_size(ifo)});
  Imported r;
  for (auto &ifo : dict::scan(from)) {
    auto name = bookname(ifo);
    if (have.contains({slurp(ifo), idx_size(ifo)})) r.already.push_back(name);
    else {
      place(ifo, name, target);
      r.added.push_back(name);
      have.insert({slurp(ifo), idx_size(ifo)});
    }
  }
  return r;
}

// Every regular file of the archive under dir, its folders kept; names leaving dir are skipped.
std::string unpack(const fs::path &file, const fs::path &dir) {
  std::unique_ptr<archive, decltype(&archive_read_free)> a(archive_read_new(), archive_read_free);
  archive_read_support_filter_all(a.get());
  archive_read_support_format_tar(a.get());
  archive_read_support_format_zip(a.get());
  if (archive_read_open_filename(a.get(), file.string().c_str(), 1 << 16) != ARCHIVE_OK)
    return std::string("Cannot open the archive: ") + archive_error_string(a.get());
  archive_entry *e = nullptr;
  int r = 0;
  while ((r = archive_read_next_header(a.get(), &e)) == ARCHIVE_OK) {
    if (archive_entry_filetype(e) != AE_IFREG || !archive_entry_pathname(e)) continue;
    auto rel = fs::path(archive_entry_pathname(e)).lexically_normal();
    if (rel.empty() || rel.is_absolute() || rel.has_root_name() || *rel.begin() == "..") continue;
    auto out_path = dir / rel;
    fs::create_directories(out_path.parent_path());
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    const void *buf = nullptr;
    size_t n = 0;
    la_int64_t off = 0;
    while ((r = archive_read_data_block(a.get(), &buf, &n, &off)) == ARCHIVE_OK) out.write(static_cast<const char *>(buf), static_cast<std::streamsize>(n));
    if (r != ARCHIVE_EOF) return std::string("Broken archive: ") + archive_error_string(a.get());
    if (!out.flush()) return "Cannot write " + rel.string();
  }
  if (r != ARCHIVE_EOF) return std::string("Broken archive: ") + archive_error_string(a.get());
  return "";
}

bool is_archive(const std::string &name) {
  auto n = text::lower(name);
  for (auto ext : {".zip", ".tar", ".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz", ".tar.zst"})
    if (n.ends_with(ext)) return true;
  return false;
}

}  // namespace

fs::path own_dir(const fs::path &data) { return data / "dicts-own"; }

bool is_dict_file(const std::string &name) {
  auto n = text::lower(name);
  for (auto &ext : EXT)
    if (n.ends_with(ext)) return true;
  return false;
}

Imported import_dicts(const std::vector<fs::path> &files, const fs::path &target) {
  static std::atomic<int> serial{0};
  auto tmp = target.parent_path() / "tmp" /
             ("import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(serial++));
  Imported r;
  std::error_code ec;
  try {
    fs::create_directories(tmp);
    fs::create_directories(target);
    std::string names;
    for (auto &f : files) {
      auto name = f.filename().string();
      names += (names.empty() ? "" : ", ") + name;
      if (is_archive(name)) {
        auto dir = tmp / std::to_string(&f - files.data());
        fs::create_directories(dir);
        if (auto e = unpack(f, dir); !e.empty()) throw std::runtime_error(name + ": " + e);
      } else if (is_dict_file(name)) {
        // A picked file brings the dictionary's other files from its folder.
        auto lower = text::lower(name);
        std::string base = name;
        for (auto &ext : EXT)
          if (lower.ends_with(ext)) base = name.substr(0, name.size() - ext.size());
        auto dir = tmp / "loose";
        fs::create_directories(dir);
        for (auto &ext : EXT)
          if (auto s = f.parent_path() / (base + ext); fs::exists(s) && !fs::exists(dir / s.filename())) fs::copy_file(s, dir / s.filename());
      } else {
        throw std::runtime_error(tr("not_stardict", {name}));
      }
    }
    r = collect(tmp, target);
    if (r.added.empty() && r.already.empty()) r.error = tr("no_ifo", {names});
  } catch (const std::exception &e) {
    r.error = e.what();
  }
  fs::remove_all(tmp, ec);
  return r;
}

}  // namespace lexipeek
