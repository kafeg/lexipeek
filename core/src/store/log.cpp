// The sync log: parse, format, read, append, merge, compact.
#include "store/log.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <tuple>
#include "text/text.hpp"

namespace lexipeek::store {

namespace fs = std::filesystem;
using nlohmann::json;
using ojson = nlohmann::ordered_json;

namespace {

const std::map<std::string, Op> OPS = {{"saved.put", Op::saved_put}, {"saved.del", Op::saved_del},
                                       {"history.seen", Op::history_seen}, {"history.del", Op::history_del},
                                       {"ai.put", Op::ai_put}};

std::string op_name(Op op) {
  for (auto &[k, v] : OPS)
    if (v == op) return k;
  return "";
}

std::string str(const json &j, const char *k) {
  auto i = j.find(k);
  return i != j.end() && i->is_string() ? i->get<std::string>() : "";
}

std::int64_t num(const json &j, const char *k) {
  auto i = j.find(k);
  return i != j.end() && i->is_number_integer() ? i->get<std::int64_t>() : 0;
}

std::string dump(const ojson &j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

}  // namespace

std::int64_t now() {
  if (auto f = fixed_now.load()) return f;
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

Time Clock::tick(std::int64_t now) {
  last = now > last.ms ? Time{now, 0} : Time{last.ms, last.n + 1};
  return last;
}

bool before(const Line &a, const Line &b) { return std::tie(a.t, a.d, a.raw) < std::tie(b.t, b.d, b.raw); }

std::optional<Line> parse(const std::string &s) {
  // An object: nlohmann would also take a UTF-8 BOM before it, which is not JSON's whitespace.
  if (auto at = s.find_first_not_of(" \t\r"); at == std::string::npos || s[at] != '{') return std::nullopt;
  auto j = json::parse(s, nullptr, false);
  if (!j.is_object()) return std::nullopt;
  auto t = j.find("t"), op = j.find("op");
  if (t == j.end() || !t->is_array() || t->size() != 2 || !(*t)[0].is_number_integer() || !(*t)[1].is_number_integer())
    return std::nullopt;
  if (op == j.end() || !op->is_string() || !OPS.contains(op->get<std::string>())) return std::nullopt;
  if (!j.contains("d") || !j["d"].is_string() || !j.contains("text") || !j["text"].is_string()) return std::nullopt;
  Line l;
  l.t = {(*t)[0].get<std::int64_t>(), (*t)[1].get<std::int64_t>()};
  l.d = j["d"].get<std::string>();
  l.op = OPS.at(op->get<std::string>());
  l.text = j["text"].get<std::string>();
  l.kind = str(j, "kind"), l.lang = str(j, "lang"), l.meaning = str(j, "meaning"), l.base = str(j, "base");
  l.c = num(j, "c"), l.ver = num(j, "ver");
  l.section = str(j, "section"), l.model = str(j, "model"), l.answer = str(j, "answer");
  l.raw = s;
  return l;
}

std::string format(const Line &l) {
  ojson j = {{"t", {l.t.ms, l.t.n}}, {"d", l.d}, {"op", op_name(l.op)}, {"text", l.text}};
  switch (l.op) {
    case Op::saved_put:
      j["kind"] = l.kind, j["lang"] = l.lang, j["meaning"] = l.meaning;
      break;
    case Op::history_seen:
      j["kind"] = l.kind, j["lang"] = l.lang, j["meaning"] = l.meaning;
      if (!l.base.empty()) j["base"] = l.base;
      j["c"] = l.c;
      break;
    case Op::ai_put:
      j["lang"] = l.lang, j["section"] = l.section, j["ver"] = l.ver, j["model"] = l.model, j["answer"] = l.answer;
      break;
    default:
      break;
  }
  return dump(j);
}

// The UTC month by days since 1970 (H. Hinnant's civil_from_days): any year, the same on every OS (Windows' gmtime_s
// stops at 3000).
std::string month_file(std::int64_t ms) {
  auto days = (ms >= 0 ? ms : ms - 86'399'999) / 86'400'000 + 719'468;
  auto era = (days >= 0 ? days : days - 146'096) / 146'097;
  auto doe = days - era * 146'097;
  auto yoe = (doe - doe / 1460 + doe / 36'524 - doe / 146'096) / 365;
  auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  auto mp = (5 * doy + 2) / 153;
  auto month = mp < 10 ? mp + 3 : mp - 9;
  auto year = yoe + era * 400 + (month <= 2 ? 1 : 0);
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04lld-%02lld.jsonl", static_cast<long long>(year), static_cast<long long>(month));
  return buf;
}

std::vector<std::string> lines_of(const fs::path &file) {
  auto s = text::read_file(file);
  std::vector<std::string> out;
  for (size_t at = 0, nl; (nl = s.find('\n', at)) != std::string::npos; at = nl + 1) out.push_back(s.substr(at, nl - at));
  return out;
}

std::vector<fs::path> log_files(const fs::path &folder) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(folder, ec)) {
    auto name = text::utf8_of(e.path().filename());
    if (name.ends_with(".jsonl") && !name.starts_with(".") && e.is_regular_file(ec)) out.push_back(e.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<fs::path> device_folders(const fs::path &root) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(root / "inkpeek-log", ec))
    if (e.is_directory(ec)) out.push_back(e.path());
  std::sort(out.begin(), out.end());
  return out;
}

namespace {

std::vector<Line> parsed(const std::set<std::string> &raw) {
  std::vector<Line> out;
  for (auto &s : raw)
    if (auto l = parse(s)) out.push_back(std::move(*l));
  std::sort(out.begin(), out.end(), before);
  return out;
}

void collect(const fs::path &folder, std::set<std::string> &raw) {
  for (auto &f : log_files(folder))
    for (auto &s : lines_of(f)) raw.insert(std::move(s));
}

}  // namespace

std::vector<Line> read_roots(const std::vector<fs::path> &roots) {
  std::set<std::string> raw;
  for (auto &r : roots)
    for (auto &d : device_folders(r)) collect(d, raw);
  return parsed(raw);
}

std::vector<Line> read_device(const fs::path &folder) {
  std::set<std::string> raw;
  collect(folder, raw);
  return parsed(raw);
}

std::string append(const fs::path &file, const std::string &line) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  if (auto size = fs::file_size(file, ec); !ec && size > 0) {
    std::ifstream in(file, std::ios::binary);
    in.seekg(-1, std::ios::end);
    if (in.get() != '\n') {
      in.seekg(0);
      std::string s((std::istreambuf_iterator<char>(in)), {});
      auto nl = s.rfind('\n');
      in.close();
      fs::resize_file(file, nl == std::string::npos ? 0 : nl + 1, ec);
      if (ec) return "cannot cut " + text::utf8_of(file) + ": " + ec.message();
    }
  }
  std::ofstream out(file, std::ios::binary | std::ios::app);
  out << line << '\n';
  out.flush();
  return out ? "" : "cannot write " + text::utf8_of(file);
}

std::string write_whole(const fs::path &file, const std::string &bytes, bool into_existing) {
  std::error_code ec;
  if (!into_existing) fs::create_directories(file.parent_path(), ec);
  auto tmp = fs::path(file).concat(".tmp");
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << bytes;
    f.flush();
    if (!f) return "cannot write " + text::utf8_of(tmp);
  }
  fs::rename(tmp, file, ec);
  if (ec) fs::remove(tmp, ec);
  return ec ? "cannot write " + text::utf8_of(file) : "";
}

std::optional<DeviceFile> read_device_file(const fs::path &file) {
  auto j = json::parse(text::read_file(file), nullptr, false);
  if (!j.is_object()) return std::nullopt;
  DeviceFile d{str(j, "device"), str(j, "app"), str(j, "name"), num(j, "created")};
  if (d.device.empty()) return std::nullopt;
  return d;
}

std::string format(const DeviceFile &d) {
  return dump(ojson{{"device", d.device}, {"app", d.app}, {"name", d.name}, {"created", d.created}}) + "\n";
}

bool keep_id(const std::vector<Line> &own, const std::set<std::string> &known) {
  if (own.empty()) return true;
  auto top = own.back().t;
  for (auto i = own.rbegin(); i != own.rend() && i->t == top; ++i)
    if (!known.contains(i->raw)) return false;
  return true;
}

std::vector<Line> compact(const std::vector<Line> &lines) {
  std::map<std::string, std::vector<size_t>> by_text;
  for (size_t i = 0; i < lines.size(); i++) by_text[lines[i].text].push_back(i);
  std::vector<size_t> keep;
  for (auto &[text, is] : by_text) {
    std::optional<size_t> sdel, hdel, seen;
    std::vector<size_t> puts;
    std::map<std::tuple<std::string, std::string, std::int64_t>, size_t> ai;
    for (auto i : is) {
      auto &l = lines[i];
      if (l.op == Op::saved_del) sdel = i, puts.clear();
      else if (l.op == Op::saved_put) puts.push_back(i);
      else if (l.op == Op::history_del) hdel = i, seen.reset();
      else if (l.op == Op::history_seen) seen = i;
      else ai[{l.lang, l.section, l.ver}] = i;
    }
    for (auto i : {sdel, hdel, seen})
      if (i) keep.push_back(*i);
    keep.insert(keep.end(), puts.begin(), puts.end());
    for (auto &[k, i] : ai) keep.push_back(i);
  }
  std::sort(keep.begin(), keep.end());
  std::vector<Line> out;
  for (auto i : keep) out.push_back(lines[i]);
  return out;
}

std::string compact_folder(const fs::path &folder, size_t min) {
  auto files = log_files(folder);
  std::set<std::string> raw;
  size_t total = 0;
  for (auto &f : files)
    for (auto &s : lines_of(f)) total++, raw.insert(std::move(s));
  if (total < min) return "";
  auto kept = compact(parsed(raw));
  if (kept.size() * 2 > total) return "";
  std::set<std::string> keep;
  for (auto &l : kept) keep.insert(l.raw);
  for (auto &f : files) {
    auto lines = lines_of(f);
    std::string out;
    std::set<std::string> once;
    for (auto &s : lines)
      if (keep.contains(s) && once.insert(s).second) out += s + "\n";
    std::error_code ec;
    if (out.empty()) fs::remove(f, ec);
    else if (out.size() != fs::file_size(f, ec))
      if (auto e = write_whole(f, out); !e.empty()) return e;
  }
  return "";
}

Records merge(const std::vector<Line> &lines) {
  std::map<std::string, std::vector<const Line *>> by_text;
  for (auto &l : lines) by_text[l.text].push_back(&l);
  Records r;
  for (auto &[text, ls] : by_text) {
    const Line *saved = nullptr, *origin = nullptr, *del = nullptr, *seen = nullptr;
    std::map<std::tuple<std::string, std::string, std::int64_t>, const Line *> ai;
    for (auto *l : ls) {
      if (l->op == Op::saved_put) {
        saved = l;
        if (!origin) origin = l;
      } else if (l->op == Op::saved_del) {
        saved = l, origin = nullptr;
      } else if (l->op == Op::history_del) {
        del = l;
      } else if (l->op == Op::ai_put) {
        ai[{l->lang, l->section, l->ver}] = l;
      }
    }
    std::map<std::string, std::int64_t> counts;
    for (auto *l : ls)
      if (l->op == Op::history_seen && (!del || before(*del, *l))) {
        counts[l->d] = std::max(counts[l->d], l->c);
        seen = l;
      }
    bool kept = false;
    if (saved && saved->op == Op::saved_put) {
      auto id = origin->d + ":" + std::to_string(origin->t.ms) + ":" + std::to_string(origin->t.n);
      r.saved.push_back({text, saved->kind, saved->lang, saved->meaning, saved->t.ms, id, origin->d, saved->d});
      kept = true;
    }
    if (seen) {
      std::int64_t count = 0;
      std::vector<std::string> devices;
      for (auto &[d, c] : counts) count += c, devices.push_back(d);
      r.history.push_back({text, seen->kind, seen->lang, seen->meaning, seen->base, seen->t.ms, count, devices});
      kept = true;
    }
    if (kept)
      for (auto &[k, l] : ai) r.ai.push_back({text, l->lang, l->section, l->ver, l->model, l->answer, l->t.ms});
  }
  return r;
}

std::int64_t own_count(const std::vector<Line> &lines, const std::string &device) {
  std::int64_t c = 0;
  for (auto &l : lines)
    if (l.op == Op::history_del) c = 0;
    else if (l.op == Op::history_seen && l.d == device) c = std::max(c, l.c);
  return c;
}

}  // namespace lexipeek::store

namespace lexipeek {

Records merge_folder(const std::filesystem::path &folder) { return store::merge(store::read_roots({folder})); }

std::string records_json(const Records &r) {
  using nlohmann::ordered_json;
  ordered_json j = {{"saved", ordered_json::array()}, {"history", ordered_json::array()}, {"ai", ordered_json::array()}};
  for (auto &s : r.saved)
    j["saved"].push_back({{"text", s.text}, {"kind", s.kind}, {"lang", s.lang}, {"meaning", s.meaning}, {"created", s.created},
                          {"id", s.id}, {"origin", s.origin}, {"updated_by", s.updated_by}});
  for (auto &h : r.history)
    j["history"].push_back({{"text", h.text}, {"kind", h.kind}, {"lang", h.lang},
                            {"base", h.base.empty() ? ordered_json() : ordered_json(h.base)}, {"meaning", h.meaning},
                            {"seen", h.seen}, {"count", h.count}, {"devices", h.devices}});
  for (auto &a : r.ai)
    j["ai"].push_back({{"text", a.text}, {"lang", a.lang}, {"section", a.section}, {"ver", a.ver}, {"model", a.model},
                       {"answer", a.answer}, {"created", a.created}});
  return j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace lexipeek
