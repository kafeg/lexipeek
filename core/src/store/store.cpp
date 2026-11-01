// Store: own lines go to <data>/inkpeek-log/<device>/ and from there to the sync folder; every text's rows in the
// SQLite view are its lines merged (the data dir's logs, the sync folder's, the copies of other devices' kept from it).
#include "lexipeek/store.hpp"
#include <sqlite3.h>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <set>
#include <fstream>
#include <map>
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <thread>
#include <regex>
#include <sstream>
#include "lexipeek/dicts.hpp"
#include "store/log.hpp"
#include "text/text.hpp"

namespace lexipeek {

namespace fs = std::filesystem;
using store::Line;
using store::Op;

namespace {

const char *SCHEMA =
    "DROP TABLE IF EXISTS saved; DROP TABLE IF EXISTS history; DROP TABLE IF EXISTS ai;"
    "CREATE TABLE saved(text TEXT PRIMARY KEY, kind TEXT, meaning TEXT, created INTEGER, lang TEXT, id TEXT, origin TEXT, "
    "updated_by TEXT);"
    "CREATE TABLE history(text TEXT PRIMARY KEY, kind TEXT, seen INTEGER, count INTEGER, meaning TEXT, lang TEXT, base TEXT, "
    "devices TEXT);"
    "CREATE TABLE ai(text TEXT, lang TEXT, section TEXT, ver INTEGER, model TEXT, answer TEXT, created INTEGER, "
    "PRIMARY KEY(text, lang, section, ver));";

#if defined(__APPLE__)
const char *PLATFORM = "mac", *PLATFORM_NAME = "Mac";
#elif defined(_WIN32)
const char *PLATFORM = "win", *PLATFORM_NAME = "Windows";
#else
const char *PLATFORM = "linux", *PLATFORM_NAME = "Linux";
#endif
const char *APP = "lexipeek";

std::int64_t now_ms() { return store::now(); }

std::string slurp(const fs::path &f) {
  std::ifstream in(f, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// false: the file could not be read (a share gone meanwhile).
bool read_file(const fs::path &f, std::string &out) {
  std::ifstream in(f, std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return !in.bad();
}

// <platform>-<8 of lower-case base32>.
std::string new_id() {
  static const char *ABC = "abcdefghijklmnopqrstuvwxyz234567";
  std::random_device rd;
  std::string id = std::string(PLATFORM) + "-";
  for (int i = 0; i < 8; i++) id += ABC[rd() % 32];
  return id;
}

// A prepared statement: bind, step, read columns.
struct Stmt {
  sqlite3_stmt *s = nullptr;
  int at = 0;
  Stmt(sqlite3 *db, const char *sql) { sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
  ~Stmt() { sqlite3_finalize(s); }
  Stmt(const Stmt &) = delete;
  Stmt &operator=(const Stmt &) = delete;
  Stmt &bind(const std::string &v) { return sqlite3_bind_text(s, ++at, v.data(), int(v.size()), SQLITE_TRANSIENT), *this; }
  Stmt &bind(std::int64_t v) { return sqlite3_bind_int64(s, ++at, v), *this; }
  Stmt &bind_base(const std::string &v) { return v.empty() ? (sqlite3_bind_null(s, ++at), *this) : bind(v); }
  bool row() { return s && sqlite3_step(s) == SQLITE_ROW; }
  bool run() { return s && sqlite3_step(s) == SQLITE_DONE; }
  std::string text(int i) {
    auto p = sqlite3_column_text(s, i);
    return p ? std::string(reinterpret_cast<const char *>(p), size_t(sqlite3_column_bytes(s, i))) : "";
  }
  std::int64_t num(int i) { return sqlite3_column_int64(s, i); }
};

std::string join_ids(const std::vector<std::string> &v) {
  std::string out;
  for (auto &s : v) out += (out.empty() ? "" : ",") + s;
  return out;
}

std::vector<std::string> split(const std::string &s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  for (std::string x; std::getline(ss, x, ',');)
    if (!x.empty()) out.push_back(x);
  return out;
}

Seen seen_row(Stmt &q) { return {q.text(0), q.text(1), q.text(2), q.text(3), q.text(4), q.num(5), q.num(6), split(q.text(7))}; }

const char *SEEN_COLUMNS = "SELECT text, kind, lang, meaning, base, seen, count, devices FROM history";
const char *SAVED_COLUMNS = "SELECT text, kind, lang, meaning, created, id, origin, updated_by FROM saved";

Saved saved_row(Stmt &q) { return {q.text(0), q.text(1), q.text(2), q.text(3), q.num(4), q.text(5), q.text(6), q.text(7)}; }

bool is_month(const std::string &name) {
  static const std::regex re(R"(\d{4}-\d{2}\.jsonl)");
  return std::regex_match(name, re);
}

// [local]'s month files made the same in [remote]: a longer one by appending, another one written whole; a month gone
// from [local] (compaction) deleted. "" or the error.
std::string push_folder(const fs::path &local, const fs::path &remote) {
  std::error_code ec;
  fs::create_directory(remote, ec);  // only the device's own: a share gone meanwhile is not made again here
  std::set<std::string> names;
  for (auto &f : store::log_files(local)) {
    auto name = text::utf8_of(f.filename());
    names.insert(name);
    auto to = remote / f.filename();
    std::string l = slurp(f), r;
    if (fs::exists(to, ec) && !read_file(to, r)) return "cannot read " + text::utf8_of(to);
    if (l == r) continue;
    auto rc = r.substr(0, r.rfind('\n') == std::string::npos ? 0 : r.rfind('\n') + 1);
    if (!rc.empty() && l.starts_with(rc)) {
      if (rc.size() != r.size()) fs::resize_file(to, rc.size(), ec);
      std::ofstream out(to, std::ios::binary | std::ios::app);
      out << l.substr(rc.size());
      out.flush();
      if (!out) return "cannot write " + text::utf8_of(to);
    } else if (auto e = store::write_whole(to, l, true); !e.empty()) {
      return e;
    }
  }
  for (auto &f : store::log_files(remote))
    if (auto name = text::utf8_of(f.filename()); is_month(name) && !names.contains(name)) fs::remove(f, ec);
  return "";
}

// [src]'s logs and device.json copied to [dst] where they differ, files gone from [src] gone from [dst]. A folder that
// cannot be listed or read (the share gone meanwhile) leaves [dst] as it is.
std::string mirror_folder(const fs::path &src, const fs::path &dst) {
  std::error_code ec;
  (void)fs::directory_iterator(src, ec);
  if (ec) return "cannot read " + text::utf8_of(src) + ": " + ec.message();
  auto files = store::log_files(src);
  if (fs::exists(src / "device.json", ec)) files.push_back(src / "device.json");
  std::set<fs::path> names;
  for (auto &f : files) {
    names.insert(f.filename());
    std::string bytes;
    if (!read_file(f, bytes)) return "cannot read " + text::utf8_of(f);
    if (fs::exists(dst / f.filename(), ec) && slurp(dst / f.filename()) == bytes) continue;
    if (auto e = store::write_whole(dst / f.filename(), bytes); !e.empty()) return e;
  }
  for (auto &e : fs::directory_iterator(dst, ec))
    if (!names.contains(e.path().filename())) fs::remove(e.path(), ec);
  return "";
}

// What other devices changed in a sync folder shows here: every file's name, size and time but [self]'s.
std::string signature(const fs::path &root, const std::string &self) {
  std::string out;
  std::error_code ec;
  for (auto &d : store::device_folders(root)) {
    if (d.filename() == text::path_of(self)) continue;
    for (auto &e : fs::directory_iterator(d, ec)) {
      auto size = e.is_regular_file(ec) ? fs::file_size(e.path(), ec) : 0;
      auto time = fs::last_write_time(e.path(), ec).time_since_epoch().count();
      out += text::utf8_of(e.path().lexically_relative(root)) + "\t" + std::to_string(static_cast<unsigned long long>(size)) + "\t" + std::to_string(static_cast<long long>(time)) + "\n";
    }
  }
  return out;
}

}  // namespace

struct Store::Impl {
  StoreOptions o;
  std::string error, sync_error;
  store::DeviceFile self;
  bool checked = false;   // the two-installs check passed for the folder: own lines go there
  bool pending = false;   // own lines not written to the folder yet (sync writes them)
  bool reachable = false;
  std::int64_t read_ms = 0, wrote_ms = 0;
  std::string sig;
  sqlite3 *db = nullptr;
  store::Clock clock;
  std::map<std::string, std::vector<Line>> lines;  // per text, in order
  mutable std::mutex m;
  // One sync at a time; taken before m. The folder's files are read and written without m: a share that stalls keeps
  // no look-up, ☆ or Notes waiting.
  std::mutex sync_m;
  // Own lines go to the folder at once, from this thread (started by the first write after the folder's check).
  std::thread pusher;
  std::mutex kick_m;
  std::condition_variable kicked;
  bool kick = false, stop = false;

  ~Impl() {
    {
      std::lock_guard k(kick_m);
      stop = true;
    }
    kicked.notify_all();
    if (pusher.joinable()) pusher.join();
    if (pending) {
      std::lock_guard s(sync_m);
      push_files();  // the last lines before closing
    }
    sqlite3_close(db);
  }

  bool exec(const char *sql) { return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
  std::string db_error() { return std::string("sqlite: ") + sqlite3_errmsg(db); }
  fs::path logs() const { return o.data / "inkpeek-log"; }
  fs::path own() const { return logs() / text::path_of(self.device); }
  fs::path cache() const { return o.data / "sync-cache"; }
  fs::path remote() const { return o.sync / "inkpeek-log" / text::path_of(self.device); }
  std::string self_name() const { return self.name.empty() ? PLATFORM_NAME : self.name; }

  std::string open() {
    std::error_code ec;
    fs::create_directories(o.data, ec);
    if (auto e = load_self(); !e.empty()) return e;
    fs::create_directories(own(), ec);
    if (ec) return "cannot make " + text::utf8_of(own()) + ": " + ec.message();
    if (auto e = import_old(); !e.empty()) return e;
    store::compact_folder(own());  // on open only: a failure leaves the files as they were
    auto file = o.data / "lexipeek.db";
    for (int attempt = 0; attempt < 2; attempt++) {
      if (sqlite3_open(text::utf8_of(file).c_str(), &db) == SQLITE_OK) {
        if (auto e = reread({o.data, cache()}); e.empty()) return "";
      }
      sqlite3_close(db);
      db = nullptr;
      fs::remove(file, ec);  // a view only: a damaged file is made again from the logs
    }
    return "cannot open " + text::utf8_of(file);
  }

  // The local log before the sync format (<data>/log/<device>.jsonl, lines with "v":1): once, as this device's own
  // lines with their times, into its month files (so to the sync folder too); then the folder is renamed log.imported.
  // A line already there is not written again: a crash before the rename imports nothing twice.
  std::string import_old() {
    std::error_code ec;
    auto old = o.data / "log";
    if (!fs::is_directory(old, ec)) return "";
    std::set<std::string> have;
    for (auto &l : store::read_device(own())) have.insert(l.raw);
    std::vector<Line> ls;
    for (auto &f : store::log_files(old))
      for (auto &raw : store::lines_of(f))
        if (auto l = store::parse(raw)) {
          l->d = self.device;
          l->raw = store::format(*l);
          if (have.insert(l->raw).second) ls.push_back(std::move(*l));
        }
    std::sort(ls.begin(), ls.end(), store::before);
    for (auto &l : ls)
      if (auto e = store::append(own() / store::month_file(l.t.ms), l.raw); !e.empty()) return e;
    auto to = o.data / "log.imported";
    for (int i = 2; fs::exists(to, ec); i++) to = o.data / ("log.imported-" + std::to_string(i));
    fs::rename(old, to, ec);
    return ec ? "cannot rename " + text::utf8_of(old) + ": " + ec.message() : "";
  }

  // <data>/device.json: this device, made once (no file: a new id; its earlier logs are read as any other device's).
  std::string load_self() {
    auto j = nlohmann::json::parse(slurp(o.data / "device.json"), nullptr, false);
    if (j.is_object() && j.value("device", "") != "" && (o.device.empty() || j.value("device", "") == o.device)) {
      self = {j.value("device", ""), APP, j.value("name", ""), j.value("created", std::int64_t(0))};
      return "";
    }
    self = {o.device.empty() ? new_id() : o.device, APP, o.name, now_ms()};
    return save_self();
  }

  std::string save_self() {
    nlohmann::ordered_json j = {{"device", self.device}, {"app", APP}, {"name", self.name}, {"created", self.created}};
    return store::write_whole(o.data / "device.json", j.dump(1) + "\n");
  }

  // The device file as the folder shows it.
  store::DeviceFile shared() const { return {self.device, APP, self_name(), self.created}; }

  // The roots' lines again, the whole view made again.
  std::string reread(const std::vector<fs::path> &roots) {
    lines.clear();
    for (auto &l : store::read_roots(roots)) {
      clock.see(l.t);
      lines[l.text].push_back(std::move(l));
    }
    if (!exec("BEGIN") || !exec(SCHEMA)) return exec("ROLLBACK"), db_error();
    for (auto &[text, ls] : lines)
      if (auto e = rows(text); !e.empty()) return exec("ROLLBACK"), e;
    return commit();
  }

  // The view's logs: the data dir's and the other devices' copied from the folder (never the folder itself).
  std::vector<fs::path> roots() const { return {o.data, cache()}; }

  // The folder (sync_m held, m not): the two-installs check, own lines written there, other devices' kept in the cache,
  // the view again.
  bool sync(bool force) {
    std::unique_lock g(m);
    if (!db) return false;
    auto folder = o.sync;
    bool was_checked = checked, waits = pending;
    auto old_sig = sig, id = self.device;
    g.unlock();
    std::error_code ec;
    bool there = !folder.empty() && fs::is_directory(folder / "inkpeek-log", ec);
    if (!folder.empty() && !there && fs::is_directory(folder, ec)) {
      fs::create_directories(folder / "inkpeek-log", ec);
      there = !ec;
    }
    if (folder.empty() || !there) {
      g.lock();
      reachable = false;
      sync_error = folder.empty() ? "" : "unreachable";
      if (!force) return false;
      if (auto e = reread(roots()); !e.empty()) sync_error = e;
      return true;
    }
    auto now_sig = signature(folder, id);
    if (!force && was_checked && !waits && now_sig == old_sig) {
      g.lock();
      reachable = true;
      return false;
    }
    g.lock();
    reachable = true;
    sync_error.clear();
    auto remote_dir = remote();
    auto own_dir = own();
    g.unlock();
    if (!was_checked) {
      std::vector<Line> own_there;
      for (auto &l : store::read_device(remote_dir))
        if (l.d == id) own_there.push_back(l);
      std::set<std::string> known;
      for (auto &l : store::read_device(own_dir)) known.insert(l.raw);
      bool keep = store::keep_id(own_there, known);
      g.lock();
      if (!keep) {
        // Another install writes as this device (copied data, a cloned machine): a new one, the same name.
        self = {new_id(), APP, self.name, now_ms()};
        fs::create_directories(own(), ec);
      }
      checked = true;
      g.unlock();
    }
    g.lock();
    save_self();
    auto self_id = self.device;
    g.unlock();
    push_files();
    bool others = force || !was_checked || now_sig != old_sig;
    std::string mirrored;
    if (others)
      for (auto &d : store::device_folders(folder))
        if (d.filename() != text::path_of(self_id))
          if (auto me = mirror_folder(d, cache() / "inkpeek-log" / d.filename()); !me.empty()) mirrored = me;
    g.lock();
    if (!mirrored.empty()) sync_error = mirrored;
    if (others)
      if (auto re = reread(roots()); !re.empty()) sync_error = re;
    // As it was before the copies (a change meanwhile is copied next time); a failed read is tried again.
    if (others) sig = mirrored.empty() ? now_sig : "\x01";
    read_ms = now_ms();
    return others;
  }

  // Own lines and device.json into the folder (sync_m held, m not), once it was checked; a failure waits for the next.
  void push_files(bool waiting_only = false) {
    std::unique_lock g(m);
    if (o.sync.empty() || !checked || (waiting_only && !pending)) return;
    pending = false;  // lines written from now on go with the next push
    auto folder = o.sync;
    auto own_dir = own(), remote_dir = remote();
    auto dev = store::format(shared());
    g.unlock();
    std::error_code ec;
    std::string e = fs::is_directory(folder / "inkpeek-log", ec) ? "" : "unreachable";
    if (e.empty()) e = push_folder(own_dir, remote_dir);
    if (e.empty() && slurp(remote_dir / "device.json") != dev) e = store::write_whole(remote_dir / "device.json", dev, true);
    g.lock();
    if (!e.empty()) pending = true;
    if (e == "unreachable") reachable = false;
    if (!e.empty()) sync_error = e;
    else wrote_ms = own_newest();
  }

  // Own lines written (m held): to the folder at once, by the pusher; before the folder's check, by the next sync.
  void push() {
    if (o.sync.empty()) return;
    pending = true;
    if (!checked) return;
    {
      std::lock_guard k(kick_m);
      kick = true;
      if (!pusher.joinable()) pusher = std::thread([this] { push_loop(); });
    }
    kicked.notify_one();
  }

  void push_loop() {
    std::unique_lock k(kick_m);
    while (!stop) {
      kicked.wait(k, [this] { return stop || kick; });
      if (stop) break;
      kick = false;
      k.unlock();
      {
        std::lock_guard s(sync_m);
        push_files(true);
      }
      k.lock();
    }
  }

  std::int64_t own_newest() const {
    std::int64_t ms = 0;
    for (auto &[text, ls] : lines)
      for (auto &l : ls)
        if (l.d == self.device) ms = std::max(ms, l.t.ms);
    return ms;
  }

  // The rows of [text]: its lines merged.
  std::string rows(const std::string &text) {
    for (auto sql : {"DELETE FROM saved WHERE text=?", "DELETE FROM history WHERE text=?", "DELETE FROM ai WHERE text=?"})
      if (!Stmt(db, sql).bind(text).run()) return db_error();
    auto r = store::merge(lines[text]);
    for (auto &s : r.saved)
      if (!Stmt(db, "INSERT INTO saved(text, kind, meaning, created, lang, id, origin, updated_by) VALUES(?, ?, ?, ?, ?, ?, ?, ?)")
               .bind(s.text).bind(s.kind).bind(s.meaning).bind(s.created).bind(s.lang).bind(s.id).bind(s.origin).bind(s.updated_by).run())
        return db_error();
    for (auto &h : r.history)
      if (!Stmt(db, "INSERT INTO history(text, kind, seen, count, meaning, lang, base, devices) VALUES(?, ?, ?, ?, ?, ?, ?, ?)")
               .bind(h.text).bind(h.kind).bind(h.seen).bind(h.count).bind(h.meaning).bind(h.lang).bind_base(h.base).bind(join_ids(h.devices)).run())
        return db_error();
    for (auto &a : r.ai)
      if (!Stmt(db, "INSERT INTO ai(text, lang, section, ver, model, answer, created) VALUES(?, ?, ?, ?, ?, ?, ?)")
               .bind(a.text).bind(a.lang).bind(a.section).bind(a.ver).bind(a.model).bind(a.answer).bind(a.created).run())
        return db_error();
    return "";
  }

  // The newest history_limit looks stay, with their answers (this store only, not the log).
  std::string commit() {
    Stmt q(db, "SELECT COUNT(*) FROM history");
    if (q.row() && q.num(0) > history_limit) {
      Stmt trim(db, "DELETE FROM history WHERE text NOT IN (SELECT text FROM history ORDER BY seen DESC, rowid DESC LIMIT ?)");
      if (!trim.bind(std::int64_t(history_limit)).run() ||
          !exec("DELETE FROM ai WHERE text NOT IN (SELECT text FROM history) AND text NOT IN (SELECT text FROM saved)"))
        return exec("ROLLBACK"), db_error();
    }
    return exec("COMMIT") ? "" : (exec("ROLLBACK"), db_error());
  }

  // Own lines: written to the log (and the folder), then the view of their texts.
  std::string write(std::vector<Line> ls) {
    if (!db) return error;
    for (auto &l : ls) {
      // A restored line keeps its time (clock.see: own lines after it still grow).
      if (l.t.ms) clock.see(l.t);
      else l.t = clock.tick(now_ms());
      l.d = self.device;
      l.raw = store::format(l);
      if (auto e = store::append(own() / store::month_file(l.t.ms), l.raw); !e.empty()) return e;
      lines[l.text].push_back(l);
    }
    push();
    if (!exec("BEGIN")) return db_error();
    for (auto &l : ls)
      if (auto e = rows(l.text); !e.empty()) return exec("ROLLBACK"), e;
    return commit();
  }

  std::vector<Answer> answers(const std::string &text) {
    std::vector<Answer> out;
    Stmt q(db, "SELECT lang, section, ver, model, answer, created FROM ai WHERE text=?");
    q.bind(text);
    while (q.row()) out.push_back({text, q.text(0), q.text(1), q.num(2), q.text(3), q.text(4), q.num(5)});
    return out;
  }

  static Line answer_line(const Answer &a) {
    Line l;
    l.op = Op::ai_put, l.text = a.text, l.lang = a.lang, l.section = a.section, l.ver = a.ver, l.model = a.model, l.answer = a.answer;
    return l;
  }
};

Store::Store(StoreOptions o) : impl_(std::make_unique<Impl>()) {
  impl_->o = std::move(o);
  impl_->error = impl_->open();
}

Store::~Store() = default;

std::string Store::error() const { return impl_->error; }
std::string Store::device() const {
  std::lock_guard g(impl_->m);
  return impl_->self.device;
}

bool Store::sync(bool force) {
  std::lock_guard s(impl_->sync_m);
  return impl_->sync(force);
}

std::string Store::set_folder(const std::filesystem::path &folder) {
  std::lock_guard s(impl_->sync_m);
  auto &im = *impl_;
  {
    std::lock_guard g(im.m);
    if (!im.db) return im.error;
    im.o.sync = folder;
    im.checked = false, im.sig.clear(), im.wrote_ms = 0;
    im.pending = !folder.empty();
  }
  im.sync(true);
  return "";  // the folder's own problems: sync_info().error
}

std::string Store::set_name(const std::string &name) {
  std::lock_guard g(impl_->m);
  impl_->self.name = text::trim(name);
  auto e = impl_->save_self();
  impl_->push();
  return e;
}

std::string Store::own_name() const {
  std::lock_guard g(impl_->m);
  return impl_->self.name;
}

SyncInfo Store::sync_info() const {
  std::lock_guard g(impl_->m);
  auto &im = *impl_;
  return {text::utf8_of(im.o.sync), im.reachable, im.read_ms, im.wrote_ms, im.sync_error == "unreachable" ? "" : im.sync_error};
}


std::string Store::seen(const std::string &text, const std::string &kind, const std::string &lang, const std::string &meaning,
                        const std::string &base) {
  std::lock_guard g(impl_->m);
  Line l;
  l.op = Op::history_seen, l.text = text, l.kind = kind, l.lang = lang, l.meaning = meaning;
  l.base = base == text ? "" : base;
  l.c = store::own_count(impl_->lines[text], impl_->self.device) + 1;
  return impl_->write({l});
}

std::string Store::save(const std::string &text, const std::string &kind, const std::string &lang, const std::string &meaning,
                        const std::string &from) {
  std::lock_guard g(impl_->m);
  Line l;
  l.op = Op::saved_put, l.text = text, l.kind = kind, l.lang = lang, l.meaning = meaning;
  std::vector<Line> ls{l};
  if (!from.empty() && from != text && impl_->db)
    for (auto a : impl_->answers(from)) {
      a.text = text;
      ls.push_back(Impl::answer_line(a));
    }
  return impl_->write(ls);
}

std::string Store::unsave(const std::string &text) {
  std::lock_guard g(impl_->m);
  Line l;
  l.op = Op::saved_del, l.text = text;
  return impl_->write({l});
}

std::string Store::put_answer(const std::string &text, const std::string &lang, const std::string &section, std::int64_t ver,
                              const std::string &model, const std::string &answer) {
  std::lock_guard g(impl_->m);
  Answer a{text, lang, section, ver, model, answer, 0};
  std::vector<Line> ls{Impl::answer_line(a)};
  if (impl_->db) {
    // An answer that comes after the word was saved as its base form goes with the saved word too.
    Stmt q(impl_->db, "SELECT h.base FROM history h JOIN saved s ON s.text=h.base WHERE h.text=? AND h.base<>h.text");
    if (q.bind(text).row()) {
      a.text = q.text(0);
      ls.push_back(Impl::answer_line(a));
    }
  }
  return impl_->write(ls);
}

std::string Store::forget(const std::string &text) {
  std::lock_guard g(impl_->m);
  Line l;
  l.op = Op::history_del, l.text = text;
  return impl_->write({l});
}

std::string Store::clear_history() {
  std::lock_guard g(impl_->m);
  std::vector<Line> ls;
  for (auto &[text, lines] : impl_->lines)
    if (!store::merge(lines).history.empty()) {
      Line l;
      l.op = Op::history_del, l.text = text;
      ls.push_back(l);
    }
  return ls.empty() ? "" : impl_->write(ls);
}


std::optional<std::string> Store::answer(const std::string &text, const std::string &lang, const std::string &section,
                                         std::int64_t ver) const {
  std::lock_guard g(impl_->m);
  if (!impl_->db) return std::nullopt;
  Stmt q(impl_->db, "SELECT answer FROM ai WHERE text=? AND lang=? AND section=? AND ver=?");
  if (q.bind(text).bind(lang).bind(section).bind(ver).row()) return q.text(0);
  return std::nullopt;
}

std::string Store::newest_answer(const std::string &text, const std::string &lang, const std::string &section) const {
  std::lock_guard g(impl_->m);
  if (!impl_->db) return "";
  Stmt q(impl_->db, "SELECT answer FROM ai WHERE text=? AND lang=? AND (?='' OR section=?) ORDER BY created DESC, rowid DESC LIMIT 1");
  return q.bind(text).bind(lang).bind(section).bind(section).row() ? q.text(0) : "";
}

bool Store::is_saved(const std::string &text) const {
  std::lock_guard g(impl_->m);
  return impl_->db && Stmt(impl_->db, "SELECT 1 FROM saved WHERE text=?").bind(text).row();
}

std::vector<Seen> Store::history(int limit) const {
  std::lock_guard g(impl_->m);
  std::vector<Seen> out;
  if (!impl_->db) return out;
  Stmt q(impl_->db, (std::string(SEEN_COLUMNS) + " ORDER BY seen DESC, rowid DESC LIMIT ?").c_str());
  q.bind(std::int64_t(limit));
  while (q.row()) out.push_back(seen_row(q));
  return out;
}

std::vector<Saved> Store::saved() const {
  std::lock_guard g(impl_->m);
  std::vector<Saved> out;
  if (!impl_->db) return out;
  Stmt q(impl_->db, (std::string(SAVED_COLUMNS) + " ORDER BY created DESC, text").c_str());
  while (q.row()) out.push_back(saved_row(q));
  return out;
}

std::vector<Seen> Store::recents(int n) const {
  std::lock_guard g(impl_->m);
  std::vector<Seen> out;
  if (!impl_->db) return out;
  // Ties of seen (the same ms): the row written last.
  Stmt q(impl_->db,
         "SELECT text, kind, lang, meaning, base, seen, count, devices FROM (SELECT *, rowid AS rid, ROW_NUMBER() OVER "
         "(PARTITION BY COALESCE(base, text) ORDER BY seen DESC, rowid DESC) AS r FROM history) WHERE r=1 "
         "ORDER BY seen DESC, rid DESC LIMIT ?");
  q.bind(std::int64_t(n));
  while (q.row()) out.push_back(seen_row(q));
  return out;
}

std::int64_t Store::looks(const std::string &word) const {
  std::lock_guard g(impl_->m);
  if (!impl_->db) return 0;
  Stmt q(impl_->db, "SELECT COALESCE(SUM(count), 0) FROM history WHERE COALESCE(base, text)=?");
  return q.bind(word).row() ? q.num(0) : 0;
}

Records Store::records() const {
  std::lock_guard g(impl_->m);
  Records r;
  if (!impl_->db) return r;
  Stmt s(impl_->db, (std::string(SAVED_COLUMNS) + " ORDER BY text").c_str());
  while (s.row()) r.saved.push_back(saved_row(s));
  Stmt h(impl_->db, (std::string(SEEN_COLUMNS) + " ORDER BY text").c_str());
  while (h.row()) r.history.push_back(seen_row(h));
  Stmt a(impl_->db, "SELECT text, lang, section, ver, model, answer, created FROM ai ORDER BY text, lang, section, ver");
  while (a.row()) r.ai.push_back({a.text(0), a.text(1), a.text(2), a.num(3), a.text(4), a.text(5), a.num(6)});
  return r;
}

std::string Store::backup(const std::filesystem::path &file) const {
  std::lock_guard g(impl_->m);
  if (!impl_->db) return impl_->error;
  auto tmp = file;
  tmp += ".tmp";
  std::error_code ec;
  fs::remove(tmp, ec);
  if (!Stmt(impl_->db, "VACUUM INTO ?").bind(tmp.string()).run()) return fs::remove(tmp, ec), impl_->db_error();
  fs::rename(tmp, file, ec);
  return ec ? (fs::remove(tmp, ec), "cannot write " + file.string()) : "";
}

Store::Restored Store::restore(const std::filesystem::path &file) {
  std::lock_guard g(impl_->m);
  Restored r;
  if (!impl_->db) return r.error = impl_->error, r;
  sqlite3 *src = nullptr;
  if (sqlite3_open_v2(text::utf8_of(file).c_str(), &src, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    sqlite3_close(src);
    return r.error = "not a backup", r;
  }
  // A table's columns; a missing one reads as "" / 0 (an older backup).
  auto columns = [&](const char *table) {
    std::set<std::string> out;
    Stmt q(src, (std::string("PRAGMA table_info(") + table + ")").c_str());
    while (q.row()) out.insert(q.text(1));
    return out;
  };
  auto select = [&](const char *table, std::vector<std::string> want) {
    auto have = columns(table);
    std::string sql;
    for (auto &c : want) sql += (sql.empty() ? "" : ", ") + (have.contains(c) ? c : std::string("NULL"));
    return "SELECT " + sql + " FROM " + table;
  };
  if (columns("saved").empty() || columns("history").empty() || columns("ai").empty()) {
    sqlite3_close(src);
    return r.error = "not a backup", r;
  }
  auto &im = *impl_;
  // A text the logs know gets now (its later lines would win over an old time); an unknown one keeps its date.
  auto when = [&](const std::string &text, std::int64_t ms) { return im.lines[text].empty() ? store::Time{ms, 0} : store::Time{}; };
  std::vector<Line> ls;
  {
    Stmt q(src, select("saved", {"text", "kind", "meaning", "created", "lang"}).c_str());
    while (q.row()) {
      if (Stmt(im.db, "SELECT 1 FROM saved WHERE text=?").bind(q.text(0)).row() || q.text(0).empty()) continue;
      Line l;
      // InkPeek's backup names the language ("Russian"); this store and the log keep its code.
      l.op = Op::saved_put, l.text = q.text(0), l.kind = q.text(1), l.meaning = q.text(2), l.lang = language_code(q.text(4));
      l.t = when(l.text, q.num(3));
      ls.push_back(l);
    }
  }
  {
    Stmt q(src, select("history", {"text", "kind", "seen", "count", "meaning", "lang", "base"}).c_str());
    while (q.row()) {
      if (Stmt(im.db, "SELECT 1 FROM history WHERE text=?").bind(q.text(0)).row() || q.text(0).empty()) continue;
      Line l;
      l.op = Op::history_seen, l.text = q.text(0), l.kind = q.text(1), l.c = std::max<std::int64_t>(1, q.num(3));
      l.meaning = q.text(4), l.lang = language_code(q.text(5)), l.base = q.text(6) == l.text ? "" : q.text(6);
      l.t = when(l.text, q.num(2));
      ls.push_back(l);
    }
  }
  {
    Stmt q(src, select("ai", {"text", "lang", "section", "ver", "model", "answer", "created"}).c_str());
    while (q.row()) {
      if (Stmt(im.db, "SELECT 1 FROM ai WHERE text=? AND lang=? AND section=? AND ver=?")
              .bind(q.text(0)).bind(q.text(1)).bind(q.text(2)).bind(q.num(3)).row() || q.text(0).empty())
        continue;
      Answer a{q.text(0), q.text(1), q.text(2), q.num(3), q.text(4), q.text(5), 0};
      auto l = Impl::answer_line(a);
      l.t = when(l.text, q.num(6));
      ls.push_back(l);
    }
  }
  sqlite3_close(src);
  // Old times first: the log stays in order.
  std::stable_sort(ls.begin(), ls.end(), [](const Line &a, const Line &b) { return a.t.ms < b.t.ms; });
  r.added = static_cast<int>(ls.size());
  if (!ls.empty()) r.error = im.write(ls);
  return r;
}

std::string Store::daily_backup(const std::filesystem::path &dir, int keep) const {
  {
    std::lock_guard g(impl_->m);
    if (!impl_->db) return impl_->error;
    Stmt q(impl_->db, "SELECT (SELECT COUNT(*) FROM saved) + (SELECT COUNT(*) FROM history)");
    if (!q.row() || q.num(0) == 0) return "";
  }
  auto day = text::local_day(now_ms());
  std::error_code ec;
  fs::create_directories(dir, ec);
  auto file = dir / ("lexipeek-" + day + ".db");
  if (fs::exists(file)) return "";
  if (auto e = backup(file); !e.empty()) return e;
  std::vector<fs::path> all;
  for (auto &f : fs::directory_iterator(dir, ec)) {
    auto n = f.path().filename().string();
    if (n.size() == 22 && n.starts_with("lexipeek-") && n.ends_with(".db")) all.push_back(f.path());
  }
  std::sort(all.rbegin(), all.rend());
  for (size_t i = static_cast<size_t>(std::max(keep, 0)); i < all.size(); i++) fs::remove(all[i], ec);
  return "";
}

}  // namespace lexipeek
