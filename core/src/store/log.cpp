// The sync log: parse, format, read, append, merge.
#include "store/log.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <tuple>

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

}  // namespace

Time Clock::tick(std::int64_t now) {
  last = now > last.ms ? Time{now, 0} : Time{last.ms, last.n + 1};
  return last;
}

bool before(const Line &a, const Line &b) { return std::tie(a.t, a.d) < std::tie(b.t, b.d); }

std::optional<Line> parse(const std::string &s) {
  auto j = json::parse(s, nullptr, false);
  if (!j.is_object()) return std::nullopt;
  auto v = j.find("v"), t = j.find("t"), op = j.find("op");
  if (v == j.end() || !v->is_number_integer() || v->get<std::int64_t>() != 1) return std::nullopt;
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
  return l;
}

std::string format(const Line &l) {
  ojson j = {{"v", 1}, {"t", {l.t.ms, l.t.n}}, {"d", l.d}, {"op", op_name(l.op)}, {"text", l.text}};
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
  return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::vector<std::string> lines_of(const fs::path &file) {
  std::ifstream in(file, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  auto s = ss.str();
  std::vector<std::string> out;
  for (size_t at = 0, nl; (nl = s.find('\n', at)) != std::string::npos; at = nl + 1) out.push_back(s.substr(at, nl - at));
  return out;
}

std::vector<Line> read_folder(const fs::path &folder) {
  std::vector<fs::path> files;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(folder, ec))
    if (e.path().extension() == ".jsonl" && e.is_regular_file(ec)) files.push_back(e.path());
  std::sort(files.begin(), files.end());
  std::set<std::string> raw;
  std::vector<Line> out;
  for (auto &f : files)
    for (auto &s : lines_of(f))
      if (raw.insert(s).second)
        if (auto l = parse(s)) out.push_back(std::move(*l));
  std::stable_sort(out.begin(), out.end(), before);
  return out;
}

std::string append(const fs::path &file, const std::string &line) {
  std::error_code ec;
  if (auto size = fs::file_size(file, ec); !ec && size > 0) {
    std::ifstream in(file, std::ios::binary);
    in.seekg(-1, std::ios::end);
    if (in.get() != '\n') {
      in.seekg(0);
      std::string s((std::istreambuf_iterator<char>(in)), {});
      auto nl = s.rfind('\n');
      in.close();
      fs::resize_file(file, nl == std::string::npos ? 0 : nl + 1, ec);
      if (ec) return "cannot cut " + file.string() + ": " + ec.message();
    }
  }
  std::ofstream out(file, std::ios::binary | std::ios::app);
  out << line << '\n';
  out.flush();
  return out ? "" : "cannot write " + file.string();
}

Records merge(const std::vector<Line> &lines) {
  std::map<std::string, std::vector<const Line *>> by_text;
  for (auto &l : lines) by_text[l.text].push_back(&l);
  Records r;
  for (auto &[text, ls] : by_text) {
    const Line *saved = nullptr, *del = nullptr, *seen = nullptr;
    std::map<std::tuple<std::string, std::string, std::int64_t>, const Line *> ai;
    for (auto *l : ls) {
      if (l->op == Op::saved_put || l->op == Op::saved_del) saved = l;
      else if (l->op == Op::history_del) del = l;
      else if (l->op == Op::ai_put) ai[{l->lang, l->section, l->ver}] = l;
    }
    std::map<std::string, std::int64_t> counts;
    for (auto *l : ls)
      if (l->op == Op::history_seen && (!del || before(*del, *l))) {
        counts[l->d] = std::max(counts[l->d], l->c);
        seen = l;
      }
    bool kept = false;
    if (saved && saved->op == Op::saved_put) {
      r.saved.push_back({text, saved->kind, saved->lang, saved->meaning, saved->t.ms});
      kept = true;
    }
    if (seen) {
      std::int64_t count = 0;
      for (auto &[d, c] : counts) count += c;
      r.history.push_back({text, seen->kind, seen->lang, seen->meaning, seen->base, seen->t.ms, count});
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

Records merge_folder(const std::filesystem::path &folder) { return store::merge(store::read_folder(folder)); }

std::string records_json(const Records &r) {
  using nlohmann::ordered_json;
  ordered_json j = {{"saved", ordered_json::array()}, {"history", ordered_json::array()}, {"ai", ordered_json::array()}};
  for (auto &s : r.saved)
    j["saved"].push_back({{"text", s.text}, {"kind", s.kind}, {"lang", s.lang}, {"meaning", s.meaning}, {"created", s.created}});
  for (auto &h : r.history)
    j["history"].push_back({{"text", h.text}, {"kind", h.kind}, {"lang", h.lang},
                            {"base", h.base.empty() ? ordered_json() : ordered_json(h.base)}, {"meaning", h.meaning},
                            {"seen", h.seen}, {"count", h.count}});
  for (auto &a : r.ai)
    j["ai"].push_back({{"text", a.text}, {"lang", a.lang}, {"section", a.section}, {"ver", a.ver}, {"model", a.model},
                       {"answer", a.answer}, {"created", a.created}});
  return j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace lexipeek
