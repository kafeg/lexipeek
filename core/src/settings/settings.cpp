#include "lexipeek/settings.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace lexipeek {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

json read(const fs::path &data) {
  std::ifstream f(data / "settings.json");
  if (!f) return json::object();
  try {
    auto j = json::parse(f);
    return j.is_object() ? j : json::object();
  } catch (const std::exception &) {
    return json::object();
  }
}

Hotkey hotkey(const json &j, Hotkey def) {
  if (!j.is_object()) return def;
  def.key = j.value("key", def.key);
  def.option = j.value("option", def.option);
  def.command = j.value("command", def.command);
  def.control = j.value("control", def.control);
  def.shift = j.value("shift", def.shift);
  return def;
}

json to_json(const Hotkey &h) {
  return {{"key", h.key}, {"option", h.option}, {"command", h.command}, {"control", h.control}, {"shift", h.shift}};
}

}  // namespace

Settings load_settings(const fs::path &data) {
  auto j = read(data);
  Settings s;
  try {
    s.answer = j.value("answer", s.answer);
    if (j.contains("method") && j["method"].is_string()) s.method = method_from(j["method"].get<std::string>());
    if (j.contains("hotkeys")) {
      s.lookup = hotkey(j["hotkeys"].value("lookup", json()), s.lookup);
      s.input = hotkey(j["hotkeys"].value("input", json()), s.input);
    }
    s.open_at_login = j.value("open_at_login", s.open_at_login);
    s.selection_icon = j.value("selection_icon", s.selection_icon);
    s.pin_moved = j.value("pin_moved", s.pin_moved);
    s.mt_english = j.value("mt_english", s.mt_english);
    s.first_run = j.value("first_run", s.first_run);
    s.ignore_apps = j.value("ignore_apps", s.ignore_apps);
    s.copy_apps = j.value("copy_apps", s.copy_apps);
    if (auto d = j.value("dicts", json::object()); d.is_object()) {
      s.dicts.order = d.value("order", s.dicts.order);
      s.dicts.off = d.value("off", s.dicts.off);
      s.dicts.folders = d.value("folders", s.dicts.folders);
      s.dicts.sources_off = d.value("sources_off", s.dicts.sources_off);
      s.dicts.got = d.value("got", s.dicts.got);
    }
  } catch (const std::exception &) {
    return Settings{};
  }
  return s;
}

std::string save_settings(const fs::path &data, const Settings &s) {
  auto j = read(data);
  j["answer"] = s.answer;
  if (s.method) j["method"] = std::string(method_name(*s.method));
  else j.erase("method");
  j["hotkeys"] = {{"lookup", to_json(s.lookup)}, {"input", to_json(s.input)}};
  j["open_at_login"] = s.open_at_login;
  j["selection_icon"] = s.selection_icon;
  j["pin_moved"] = s.pin_moved;
  j["mt_english"] = s.mt_english;
  j["first_run"] = s.first_run;
  j["ignore_apps"] = s.ignore_apps;
  j["copy_apps"] = s.copy_apps;
  j["dicts"] = {{"order", s.dicts.order}, {"off", s.dicts.off}, {"folders", s.dicts.folders}, {"sources_off", s.dicts.sources_off}, {"got", s.dicts.got}};
  std::error_code ec;
  fs::create_directories(data, ec);
  auto tmp = data / "settings.json.tmp";
  {
    std::ofstream f(tmp, std::ios::binary);
    if (!f) return "Cannot write " + tmp.string();
    f << j.dump(2) << "\n";
  }
  fs::rename(tmp, data / "settings.json", ec);
  return ec ? ec.message() : "";
}

}  // namespace lexipeek
