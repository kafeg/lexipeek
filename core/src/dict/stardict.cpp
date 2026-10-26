#include "dict/stardict.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <hash-library/sha1.h>
#include <zlib.h>
#include "text/text.hpp"

namespace lexipeek::dict {

namespace fs = std::filesystem;

namespace {

constexpr char MAGIC[4] = {'I', 'P', 'K', '3'};

std::string slurp(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

// A whole gzip file inflated (an .idx.gz, a .dict.dz without dictzip chunks).
std::string gunzip(const fs::path &p) {
  std::string out;
  gzFile g = gzopen(p.string().c_str(), "rb");
  if (!g) return out;
  char buf[1 << 16];
  for (int n; (n = gzread(g, buf, sizeof buf)) > 0;) out.append(buf, static_cast<size_t>(n));
  gzclose(g);
  return out;
}

std::string maybe_gz(const std::string &base) {
  if (fs::exists(base)) return slurp(base);
  for (auto ext : {".gz", ".dz"})
    if (fs::exists(base + ext)) return gunzip(base + ext);
  return {};
}

uint32_t be32(const std::string &s, size_t p) {
  auto u = reinterpret_cast<const unsigned char *>(s.data() + p);
  return (uint32_t(u[0]) << 24) | (uint32_t(u[1]) << 16) | (uint32_t(u[2]) << 8) | u[3];
}

template <class T>
void put(std::string &out, T v) {
  out.append(reinterpret_cast<const char *>(&v), sizeof v);
}

template <class T>
T get(const std::string &s, size_t p) {
  T v;
  std::memcpy(&v, s.data() + p, sizeof v);
  return v;
}

// The article's fields as HTML (as InkPeek's Article.toHtml + tidy).
std::string to_html(const std::string &data, const std::string &types) {
  std::string out;
  size_t p = 0;
  auto field = [&](char t, bool last) -> std::string {
    if (t >= 'A' && t <= 'Z') {
      if (p + 4 > data.size()) return p = data.size(), std::string();
      auto n = be32(data, p);
      p += 4;
      auto r = data.substr(p, n);
      p = std::min(data.size(), p + n);
      return r;
    }
    if (last) {
      auto r = data.substr(std::min(p, data.size()));
      p = data.size();
      return r;
    }
    auto e = data.find('\0', p);
    if (e == std::string::npos) e = data.size();
    auto r = data.substr(p, e - p);
    p = e + 1;
    return r;
  };
  auto add = [&](char t, const std::string &s) {
    static const text::Regex xtags("</?(c|co|dtrn|ar|def|gr|tr)[^>]*>");
    auto rep = [](std::string x, const std::string &a, const std::string &b) {
      for (size_t at = 0; (at = x.find(a, at)) != std::string::npos; at += b.size()) x.replace(at, a.size(), b);
      return x;
    };
    if (t == 'h' || t == 'g') out += s;
    else if (t == 'x') {
      auto x = rep(rep(rep(rep(rep(rep(rep(rep(s, "<k>", "<b>"), "</k>", "</b><br>"), "<ex>", "<i>"), "</ex>", "</i>"), "<abr>", "<i>"),
                       "</abr>", "</i>"), "<kref>", "<u>"), "</kref>", "</u>");
      out += rep(xtags.replace(x, ""), "\n", "<br>");
    } else if (t == 'm' || t == 'l' || t == 't' || t == 'y') out += rep(text::escape(s), "\n", "<br>");
    out += "<br>";
  };
  if (!types.empty()) {
    for (size_t i = 0; i < types.size() && p < data.size(); i++) add(types[i], field(types[i], i == types.size() - 1));
  } else {
    while (p < data.size()) {
      char t = data[p++];
      add(t, field(t, false));
    }
  }
  if (out.ends_with("<br>")) out.resize(out.size() - 4);
  static const text::Regex script("<(script|style)[^>]*>.*?</\\1>", true, true), h_open("<h[1-6][^>]*>", true), h_close("</h[1-6]>", true),
      links("</?a\\b[^>]*>", true), img("<img[^>]*>", true), brs("(<br\\s*/?>\\s*){3,}", true);
  auto h = brs.replace(img.replace(links.replace(h_close.replace(h_open.replace(script.replace(out, ""), "<br><b>"), "</b><br>"), ""), ""),
                       "<br><br>");
  if (h.starts_with("<br>")) h = h.substr(4);
  return h;
}

}  // namespace

// Article bytes from a plain .dict, or from a .dict.dz by dictzip chunks (else inflated whole).
struct StarDict::Data {
  std::ifstream f;
  bool plain = false;
  uint32_t chunk = 0;
  std::vector<uint64_t> starts;
  std::string whole;

  explicit Data(const std::string &base) {
    if (fs::exists(base + ".dict")) {
      f.open(base + ".dict", std::ios::binary);
      plain = true;
      return;
    }
    auto dz = base + ".dict.dz";
    f.open(dz, std::ios::binary);
    if (!dictzip()) {
      f.close();
      whole = gunzip(dz);
    }
  }

  bool dictzip() {
    unsigned char h[10];
    if (!f.read(reinterpret_cast<char *>(h), 10) || h[0] != 0x1f || h[1] != 0x8b || !(h[3] & 4)) return false;
    unsigned flags = h[3];
    unsigned char x[2];
    f.read(reinterpret_cast<char *>(x), 2);
    std::string extra(x[0] | (x[1] << 8), '\0');
    f.read(extra.data(), static_cast<std::streamsize>(extra.size()));
    auto u16 = [&](size_t p) { return unsigned(static_cast<unsigned char>(extra[p])) | (unsigned(static_cast<unsigned char>(extra[p + 1])) << 8); };
    std::vector<uint32_t> sizes;
    for (size_t p = 0; p + 4 <= extra.size();) {
      auto len = u16(p + 2);
      if (extra[p] == 'R' && extra[p + 1] == 'A' && p + 10 <= extra.size()) {
        chunk = u16(p + 6);
        auto cnt = u16(p + 8);
        for (unsigned i = 0; i < cnt && p + 10 + 2 * i + 1 < extra.size(); i++) sizes.push_back(u16(p + 10 + 2 * i));
      }
      p += 4 + len;
    }
    if (sizes.empty() || !chunk) return false;
    auto skipz = [&] { for (char c; f.get(c) && c;) {} };
    if (flags & 8) skipz();
    if (flags & 16) skipz();
    if (flags & 2) f.ignore(2);
    starts.push_back(static_cast<uint64_t>(f.tellg()));
    for (auto s : sizes) starts.push_back(starts.back() + s);
    return true;
  }

  std::string read(uint64_t off, uint32_t size) {
    if (plain) {
      std::string out(size, '\0');
      f.clear();
      f.seekg(static_cast<std::streamoff>(off));
      f.read(out.data(), size);
      return out;
    }
    if (starts.empty()) return off < whole.size() ? whole.substr(off, size) : std::string();
    std::string out;
    auto end = off + size;
    for (auto c = off / chunk; c + 1 < starts.size() && c * chunk < end; c++) {
      std::string comp(starts[c + 1] - starts[c], '\0');
      f.clear();
      f.seekg(static_cast<std::streamoff>(starts[c]));
      f.read(comp.data(), static_cast<std::streamsize>(comp.size()));
      std::string buf(chunk, '\0');
      z_stream z{};
      inflateInit2(&z, -15);
      z.next_in = reinterpret_cast<Bytef *>(comp.data());
      z.avail_in = static_cast<uInt>(comp.size());
      z.next_out = reinterpret_cast<Bytef *>(buf.data());
      z.avail_out = chunk;
      inflate(&z, Z_SYNC_FLUSH);
      out.append(buf.data(), chunk - z.avail_out);
      inflateEnd(&z);
    }
    auto from = off % chunk;
    return from < out.size() ? out.substr(from, size) : std::string();
  }
};

StarDict::StarDict(const fs::path &ifo, const fs::path &work, bool pack) : ifo_(fs::weakly_canonical(ifo)), pack_(pack) {
  auto s = ifo_.string();
  base_ = s.substr(0, s.size() - 4);
  std::ifstream f(ifo_);
  for (std::string l; std::getline(f, l);) {
    auto i = l.find('=');
    if (i == std::string::npos) continue;
    auto k = l.substr(0, i), v = l.substr(i + 1);
    while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
    if (k == "bookname") name_ = v;
    else if (k == "sametypesequence") types_ = v;
    else if (k == "idxoffsetbits") offset64_ = v == "64";
    else if (k == "wordcount") words_ = std::strtol(v.c_str(), nullptr, 10);
  }
  if (name_.empty()) name_ = ifo_.stem().string();
  SHA1 sha;
  id_ = sha(text::lower(s)).substr(0, 16);
  work_ = work / id_;
  static const text::Regex pack_dir("dict-([a-z]{2,3})-([a-z]{2,3})");
  text::Regex::Match m;
  auto dir = ifo_.parent_path().filename().string();
  if (pack_ && pack_dir.find(dir, m) && m.begin == 0 && m.end == dir.size()) book_ = m.groups[1], answer_ = m.groups[2];
}

StarDict::~StarDict() = default;

std::string StarDict::stamp() const {
  std::string s;
  for (auto ext : {".ifo", ".idx", ".idx.gz", ".dict", ".dict.dz", ".syn", ".syn.dz"}) {
    fs::path p = base_.string() + ext;
    std::error_code ec;
    if (!fs::exists(p, ec)) continue;
    s += p.filename().string() + " " + std::to_string(fs::file_size(p, ec)) + " " +
         std::to_string(static_cast<long long>(fs::last_write_time(p, ec).time_since_epoch().count())) + "\n";
  }
  return s;
}

bool StarDict::ready() {
  std::lock_guard g(mu_);
  return !index_.empty() || (slurp(work_ / "stamp") == stamp() && fs::exists(work_ / "index.ipk"));
}

void StarDict::prepare() {
  std::lock_guard g(mu_);
  if (!index_.empty()) return;
  if (slurp(work_ / "stamp") != stamp() || !fs::exists(work_ / "index.ipk")) build();
  load();
}

void StarDict::build() {
  auto idx = maybe_gz(base_.string() + ".idx");
  if (idx.empty()) throw std::runtime_error("No .idx: " + base_.string());
  struct W {
    size_t at, len;
    uint64_t off;
    uint32_t size;
  };
  std::vector<W> words;
  for (size_t p = 0; p < idx.size();) {
    auto e = idx.find('\0', p);
    if (e == std::string::npos) break;
    W w{p, e - p, 0, 0};
    p = e + 1;
    if (offset64_) {
      w.off = (uint64_t(be32(idx, p)) << 32) | be32(idx, p + 4);
      p += 8;
    } else {
      w.off = be32(idx, p);
      p += 4;
    }
    w.size = be32(idx, p);
    p += 4;
    words.push_back(w);
  }
  auto word = [&](size_t i) { return std::string_view(idx).substr(words[i].at, words[i].len); };
  std::vector<std::pair<std::string, uint32_t>> keys;
  keys.reserve(words.size());
  for (size_t i = 0; i < words.size(); i++) keys.emplace_back(text::norm_key(word(i)), static_cast<uint32_t>(i));
  auto syn = maybe_gz(base_.string() + ".syn");
  for (size_t p = 0; p < syn.size();) {
    auto e = syn.find('\0', p);
    if (e == std::string::npos || e + 5 > syn.size()) break;
    auto w = be32(syn, e + 1);
    if (w < words.size()) keys.emplace_back(text::norm_key(std::string_view(syn).substr(p, e - p)), w);
    p = e + 5;
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  std::string blob;
  std::vector<uint32_t> pos;
  for (auto &[k, w] : keys) {
    if (k.empty()) continue;
    pos.push_back(static_cast<uint32_t>(blob.size()));
    auto wd = word(w);
    put<uint16_t>(blob, static_cast<uint16_t>(k.size()));
    blob += k;
    put<uint16_t>(blob, static_cast<uint16_t>(wd.size()));
    blob += wd;
    put<uint64_t>(blob, words[w].off);
    put<uint32_t>(blob, words[w].size);
  }
  std::string out(MAGIC, 4);
  put<uint32_t>(out, static_cast<uint32_t>(pos.size()));
  for (auto p : pos) put<uint32_t>(out, p);
  out += blob;
  fs::create_directories(work_);
  {
    std::ofstream o(work_ / "index.tmp", std::ios::binary);
    o << out;
  }
  fs::rename(work_ / "index.tmp", work_ / "index.ipk");
  std::ofstream(work_ / "stamp", std::ios::binary) << stamp();
  std::ofstream(work_ / "source", std::ios::binary) << ifo_.string();
}

void StarDict::load() {
  index_ = slurp(work_ / "index.ipk");
  if (index_.size() < 8 || index_.compare(0, 4, MAGIC, 4) != 0) throw std::runtime_error("Broken index: " + name_);
  count_ = get<uint32_t>(index_, 4);
  blob_ = 8 + 4 * count_;
  data_ = std::make_unique<Data>(base_.string());
}

std::string StarDict::key_at(size_t i) const {
  size_t p = blob_ + get<uint32_t>(index_, 8 + 4 * i);
  return index_.substr(p + 2, get<uint16_t>(index_, p));
}

StarDict::Entry StarDict::entry_at(size_t i) const {
  size_t p = blob_ + get<uint32_t>(index_, 8 + 4 * i);
  p += 2 + get<uint16_t>(index_, p);
  auto wl = get<uint16_t>(index_, p);
  Entry e{index_.substr(p + 2, wl), 0, 0};
  p += 2 + wl;
  e.off = get<uint64_t>(index_, p);
  e.size = get<uint32_t>(index_, p + 8);
  return e;
}

std::string StarDict::read(uint64_t off, uint32_t size) { return data_->read(off, size); }

std::vector<Hit> StarDict::lookup(const std::string &query) {
  prepare();
  auto key = text::norm_key(query);
  if (key.empty()) return {};
  std::lock_guard g(mu_);
  size_t lo = 0, hi = count_;
  while (lo < hi) {
    auto mid = (lo + hi) / 2;
    if (key_at(mid) < key) lo = mid + 1;
    else hi = mid;
  }
  std::vector<Entry> all;
  for (auto i = lo; i < count_ && key_at(i) == key; i++) all.push_back(entry_at(i));
  if (all.empty()) return {};
  // As InkPeek: the word as written > a capitalised word as its lower-case headword > a form of another word > another case.
  static const text::Regex edge("^\\s+|\\s+$");
  auto q = edge.replace(query, "");
  auto lq = text::lower(q);
  auto rank = [&](const Entry &e) {
    if (e.word == q) return 0;
    if (lq != q && e.word == lq) return 1;
    return text::lower(e.word) != lq ? 2 : 3;
  };
  std::vector<Entry> uniq;
  for (auto &e : all)
    if (std::none_of(uniq.begin(), uniq.end(), [&](const Entry &u) { return u.off == e.off; })) uniq.push_back(e);
  std::stable_sort(uniq.begin(), uniq.end(), [&](const Entry &a, const Entry &b) { return rank(a) < rank(b); });
  if (uniq.size() > 8) uniq.resize(8);
  std::vector<Hit> out;
  for (auto &e : uniq) {
    auto html = to_html(read(e.off, e.size), types_);
    if (std::none_of(out.begin(), out.end(), [&](const Hit &h) { return h.html == html; })) out.push_back({e.word, html});
  }
  return out;
}

std::optional<Related> StarDict::related(const std::string &word) {
  fs::path rel = base_.string() + ".rel";
  std::ifstream f(rel, std::ios::binary);
  if (!f) return std::nullopt;
  f.seekg(0, std::ios::end);
  auto len = static_cast<uint64_t>(f.tellg());
  // The first whole line starting at or after at: (its start, its text).
  auto line_at = [&](uint64_t at) -> std::optional<std::pair<uint64_t, std::string>> {
    f.clear();
    uint64_t start = at;
    if (at > 0) {
      f.seekg(static_cast<std::streamoff>(at - 1));
      for (char c; f.get(c) && c != '\n';) {}
      if (!f) return std::nullopt;
      start = static_cast<uint64_t>(f.tellg());
    } else {
      f.seekg(0);
    }
    if (start >= len) return std::nullopt;
    std::string l;
    std::getline(f, l);
    return std::make_pair(start, l);
  };
  auto cmp = [&](const std::string &l) {
    auto w = std::string_view(l).substr(0, l.find('\t'));
    return w.compare(word);
  };
  uint64_t lo = 0, hi = len;
  std::optional<std::string> found;
  while (lo < hi) {
    auto mid = (lo + hi) / 2;
    auto l = line_at(mid);
    if (!l) { hi = mid; continue; }
    auto c = cmp(l->second);
    if (c == 0) { found = l->second; break; }
    if (c < 0) lo = l->first + l->second.size() + 1;
    else hi = mid;
  }
  if (!found)
    if (auto l = line_at(lo); l && cmp(l->second) == 0) found = l->second;
  if (!found) return std::nullopt;
  std::vector<std::string> c;
  std::stringstream ss(*found);
  for (std::string x; std::getline(ss, x, '\t');) c.push_back(x);
  auto list = [&](size_t i) {
    std::vector<std::string> out;
    if (i >= c.size()) return out;
    std::stringstream s(c[i]);
    for (std::string x; std::getline(s, x, '|');)
      if (x.find_first_not_of(' ') != std::string::npos) out.push_back(x);
    return out;
  };
  Related r{list(1), list(2), list(3), list(4), list(5), c.size() > 6 ? c[6] : "", {}, 0, c.size() > 9 ? c[9] : ""};
  for (auto &f7 : list(7)) {
    auto i = f7.rfind(':');
    r.forms.emplace_back(i == std::string::npos ? f7 : f7.substr(0, i), i == std::string::npos ? "" : f7.substr(i + 1));
  }
  if (c.size() > 8 && !c[8].empty()) r.rank = std::atoi(c[8].c_str());
  return r;
}

std::vector<fs::path> scan(const fs::path &root) {
  std::vector<fs::path> out;
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return out;
  for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it.depth() > 6) it.disable_recursion_pending();
    if (it->is_regular_file(ec) && it->path().extension() == ".ifo") out.push_back(it->path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace lexipeek::dict
