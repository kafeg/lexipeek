// Store: own lines go to <logs>/<device>.jsonl, every text's rows in the SQLite view are its lines merged.
#include "lexipeek/store.hpp"
#include <sqlite3.h>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <set>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include "store/log.hpp"
#include "text/text.hpp"

namespace lexipeek {

namespace fs = std::filesystem;
using store::Line;
using store::Op;

namespace {

const char *SCHEMA =
    "DROP TABLE IF EXISTS saved; DROP TABLE IF EXISTS history; DROP TABLE IF EXISTS ai;"
    "CREATE TABLE saved(text TEXT PRIMARY KEY, kind TEXT, meaning TEXT, created INTEGER, lang TEXT);"
    "CREATE TABLE history(text TEXT PRIMARY KEY, kind TEXT, seen INTEGER, count INTEGER, meaning TEXT, lang TEXT, base TEXT);"
    "CREATE TABLE ai(text TEXT, lang TEXT, section TEXT, ver INTEGER, model TEXT, answer TEXT, created INTEGER, "
    "PRIMARY KEY(text, lang, section, ver));";

#if defined(__APPLE__)
const char *PLATFORM = "mac";
#elif defined(_WIN32)
const char *PLATFORM = "win";
#else
const char *PLATFORM = "linux";
#endif

std::int64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
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

Seen seen_row(Stmt &q) { return {q.text(0), q.text(1), q.text(2), q.text(3), q.text(4), q.num(5), q.num(6)}; }

const char *SEEN_COLUMNS = "SELECT text, kind, lang, meaning, base, seen, count FROM history";

}  // namespace

struct Store::Impl {
  StoreOptions o;
  std::string error, device;
  fs::path own;
  sqlite3 *db = nullptr;
  store::Clock clock;
  std::map<std::string, std::vector<Line>> lines;  // per text, in order
  mutable std::mutex m;

  ~Impl() { sqlite3_close(db); }

  bool exec(const char *sql) { return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
  std::string db_error() { return std::string("sqlite: ") + sqlite3_errmsg(db); }

  std::string open() {
    std::error_code ec;
    fs::create_directories(o.data, ec);
    if (o.logs.empty()) o.logs = o.data / "log";
    fs::create_directories(o.logs, ec);
    if (ec) return "cannot make " + o.logs.string() + ": " + ec.message();
    device = o.device.empty() ? device_id() : o.device;
    if (device.empty()) return "cannot write " + (o.data / "device").string();
    own = o.logs / (device + ".jsonl");
    store::compact_file(own);  // on open only: a failure leaves the file as it was
    auto file = o.data / "lexipeek.db";
    for (int attempt = 0; attempt < 2; attempt++) {
      if (sqlite3_open(text::utf8_of(file).c_str(), &db) == SQLITE_OK) {
        if (auto e = reread(); e.empty()) return "";
      }
      sqlite3_close(db);
      db = nullptr;
      fs::remove(file, ec);  // a view only: a damaged file is made again from the logs
    }
    return "cannot open " + file.string();
  }

  std::string device_id() {
    auto f = o.data / "device";
    std::string id;
    std::getline(std::ifstream(f), id);
    while (!id.empty() && (id.back() == '\r' || id.back() == ' ')) id.pop_back();
    if (!id.empty()) return id;
    std::random_device rd;
    char hex[5];
    std::snprintf(hex, sizeof hex, "%04x", unsigned(rd() & 0xffff));
    id = std::string(PLATFORM) + "-" + hex;
    std::ofstream(f) << id << "\n";
    return fs::exists(f) ? id : "";
  }

  // All logs again, the whole view made again.
  std::string reread() {
    lines.clear();
    for (auto &l : store::read_folder(o.logs)) {
      clock.see(l.t);
      lines[l.text].push_back(std::move(l));
    }
    if (!exec("BEGIN") || !exec(SCHEMA)) return exec("ROLLBACK"), db_error();
    for (auto &[text, ls] : lines)
      if (auto e = rows(text); !e.empty()) return exec("ROLLBACK"), e;
    return commit();
  }

  // The rows of [text]: its lines merged.
  std::string rows(const std::string &text) {
    for (auto sql : {"DELETE FROM saved WHERE text=?", "DELETE FROM history WHERE text=?", "DELETE FROM ai WHERE text=?"})
      if (!Stmt(db, sql).bind(text).run()) return db_error();
    auto r = store::merge(lines[text]);
    for (auto &s : r.saved)
      if (!Stmt(db, "INSERT INTO saved(text, kind, meaning, created, lang) VALUES(?, ?, ?, ?, ?)")
               .bind(s.text).bind(s.kind).bind(s.meaning).bind(s.created).bind(s.lang).run())
        return db_error();
    for (auto &h : r.history)
      if (!Stmt(db, "INSERT INTO history(text, kind, seen, count, meaning, lang, base) VALUES(?, ?, ?, ?, ?, ?, ?)")
               .bind(h.text).bind(h.kind).bind(h.seen).bind(h.count).bind(h.meaning).bind(h.lang).bind_base(h.base).run())
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

  // Own lines: written to the log, then the view of their texts.
  std::string write(std::vector<Line> ls) {
    if (!db) return error;
    for (auto &l : ls) {
      // A restored line keeps its time (clock.see: own lines after it still grow).
      if (l.t.ms) clock.see(l.t);
      else l.t = clock.tick(now_ms());
      l.d = device;
      if (auto e = store::append(own, store::format(l)); !e.empty()) return e;
      lines[l.text].push_back(l);
    }
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
std::string Store::device() const { return impl_->device; }

std::string Store::seen(const std::string &text, const std::string &kind, const std::string &lang, const std::string &meaning,
                        const std::string &base) {
  std::lock_guard g(impl_->m);
  Line l;
  l.op = Op::history_seen, l.text = text, l.kind = kind, l.lang = lang, l.meaning = meaning;
  l.base = base == text ? "" : base;
  l.c = store::own_count(impl_->lines[text], impl_->device) + 1;
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

std::string Store::merge() {
  std::lock_guard g(impl_->m);
  return impl_->db ? impl_->reread() : impl_->error;
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
  Stmt q(impl_->db, "SELECT text, kind, lang, meaning, created FROM saved ORDER BY created DESC, text");
  while (q.row()) out.push_back({q.text(0), q.text(1), q.text(2), q.text(3), q.num(4)});
  return out;
}

std::vector<Seen> Store::recents(int n) const {
  std::lock_guard g(impl_->m);
  std::vector<Seen> out;
  if (!impl_->db) return out;
  // Ties of seen (the same ms): the row written last.
  Stmt q(impl_->db,
         "SELECT text, kind, lang, meaning, base, seen, count FROM (SELECT *, rowid AS id, ROW_NUMBER() OVER "
         "(PARTITION BY COALESCE(base, text) ORDER BY seen DESC, rowid DESC) AS r FROM history) WHERE r=1 "
         "ORDER BY seen DESC, id DESC LIMIT ?");
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
  Stmt s(impl_->db, "SELECT text, kind, lang, meaning, created FROM saved ORDER BY text");
  while (s.row()) r.saved.push_back({s.text(0), s.text(1), s.text(2), s.text(3), s.num(4)});
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
      l.op = Op::saved_put, l.text = q.text(0), l.kind = q.text(1), l.meaning = q.text(2), l.lang = q.text(4);
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
      l.meaning = q.text(4), l.lang = q.text(5), l.base = q.text(6) == l.text ? "" : q.text(6);
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
