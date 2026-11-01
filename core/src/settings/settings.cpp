#include "lexipeek/settings.hpp"
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include "lexipeek/l10n.hpp"
#include "lexipeek/study.hpp"
#include "text/text.hpp"

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

// A field, or [def] when it is missing or of another type: one bad field (an older build's, a hand edit) keeps the rest.
template <class T>
T get(const json &j, const char *key, const T &def) {
  auto it = j.is_object() ? j.find(key) : j.end();
  if (!j.is_object() || it == j.end()) return def;
  try {
    return it->template get<T>();
  } catch (const json::exception &) {
    return def;
  }
}

Hotkey hotkey(const json &j, Hotkey def) {
  if (!j.is_object()) return def;
  def.key = get(j, "key", def.key);
  def.option = get(j, "option", def.option);
  def.command = get(j, "command", def.command);
  def.control = get(j, "control", def.control);
  def.shift = get(j, "shift", def.shift);
  return def;
}

json to_json(const Hotkey &h) {
  return {{"key", h.key}, {"option", h.option}, {"command", h.command}, {"control", h.control}, {"shift", h.shift}};
}

CardPrefs card(const json &j) {
  CardPrefs c;
  if (!j.is_object()) return c;
  c.phrase_open = get(j, "phrase_open", c.phrase_open);
  c.full = get(j, "full", c.full);
  c.grammar = get(j, "grammar", c.grammar);
  c.sections = get(j, "sections", c.sections);
  c.width = get(j, "width", c.width);
  c.text = get(j, "text", c.text);
  c.font = get(j, "font", c.font);
  c.height = get(j, "height", c.height);
  c.history = get(j, "history", c.history);
  return c;
}

json to_json(const CardPrefs &c) {
  return {{"phrase_open", c.phrase_open}, {"full", c.full}, {"grammar", c.grammar}, {"sections", c.sections},
          {"width", c.width}, {"text", c.text}, {"font", c.font}, {"height", c.height}, {"history", c.history}};
}

AiSettings ai(const json &j) {
  AiSettings a;
  if (!j.is_object()) return a;
  a.provider = get(j, "provider", a.provider);
  a.models = get(j, "models", a.models);
  a.url = get(j, "url", a.url);
  a.commands = get(j, "commands", a.commands);
  a.translate = get(j, "translate", a.translate);
  a.prefetch = get(j, "prefetch", a.prefetch);
  a.prompts = get(j, "prompts", a.prompts);
  a.picked = get(j, "picked", a.picked);
  a.recall = get(j, "recall", a.recall);
  return a;
}

json to_json(const AiSettings &a) {
  return {{"provider", a.provider}, {"models", a.models},       {"url", a.url},         {"commands", a.commands}, {"translate", a.translate},
          {"prefetch", a.prefetch}, {"prompts", a.prompts},     {"picked", a.picked},   {"recall", a.recall}};
}

}  // namespace

Settings load_settings(const fs::path &data) {
  auto j = read(data);
  Settings s;
  // Windows: Win+Shift+D and Win+Shift+A — the Mac's letters; Alt+D is the browsers' address bar, Ctrl+Alt+letter is
  // AltGr+letter (Polish ą), Win+Alt+D the clock's.
  if (ui_platform() == "windows")
    for (auto *h : {&s.lookup, &s.input}) h->option = false, h->command = h->shift = true;
  try {
    s.answer = get(j, "answer", s.answer);
    if (j.contains("method") && j["method"].is_string()) {
      s.mt_off = j["method"] == "off";
      s.method = method_from(j["method"].get<std::string>());
    }
    if (j.contains("hotkeys")) {
      s.lookup = hotkey(get(j["hotkeys"], "lookup", json()), s.lookup);
      s.input = hotkey(get(j["hotkeys"], "input", json()), s.input);
    }
    s.open_at_login = get(j, "open_at_login", s.open_at_login);
    s.selection_icon = get(j, "selection_icon", s.selection_icon);
    s.pin_moved = get(j, "pin_moved", s.pin_moved);
    s.mt_english = get(j, "mt_english", s.mt_english);
    s.first_run = get(j, "first_run", s.first_run);
    s.keep_model = get(j, "keep_model", s.keep_model);
    // One of Settings → General's choices (minutes; 0: until quit), else the default.
    if (s.keep_model != 0 && s.keep_model != 1 && s.keep_model != 15 && s.keep_model != 60) s.keep_model = 5;
    s.ignore_apps = get(j, "ignore_apps", s.ignore_apps);
    if (auto d = get(j, "dicts", json::object()); d.is_object()) {
      s.dicts.order = get(d, "order", s.dicts.order);
      s.dicts.off = get(d, "off", s.dicts.off);
      s.dicts.folders = get(d, "folders", s.dicts.folders);
      s.dicts.sources_off = get(d, "sources_off", s.dicts.sources_off);
      s.dicts.got = get(d, "got", s.dicts.got);
    }
    s.card = card(get(j, "card", json()));
    s.ui_lang = get(j, "ui_lang", s.ui_lang);
    s.theme = get(j, "theme", s.theme);
    s.sync = get(j, "sync", s.sync);
    s.ai = ai(get(j, "ai", json()));
    if (auto st = get(j, "study", json::object()); st.is_object())
      for (auto &[answer, langs] : st.items())
        if (langs.is_array())
          if (auto l = study_parse(langs.get<std::vector<std::string>>()); !l.empty()) s.study[answer] = l;
    if (auto sp = get(j, "speech", json::object()); sp.is_object()) {
      s.speech.on = get(sp, "on", s.speech.on);
      s.speech.male = get(sp, "male", s.speech.male);
      s.speech.gpu = get(sp, "gpu", s.speech.gpu);
      s.speech.keep = get(sp, "keep", s.speech.keep);
      s.speech.off = get(sp, "off", s.speech.off);
    }
    if (auto a = get(j, "anki", json::object()); a.is_object()) {
      auto &k = s.anki;
      k.on = get(a, "on", k.on), k.url = get(a, "url", k.url), k.sync = get(a, "sync", k.sync);
      k.words = get(a, "words", k.words), k.phrases = get(a, "phrases", k.phrases), k.history = get(a, "history", k.history);
      k.words_on = get(a, "words_on", k.words_on), k.phrases_on = get(a, "phrases_on", k.phrases_on), k.history_on = get(a, "history_on", k.history_on);
    }
  } catch (const std::exception &) {
    // A field of the wrong type: what was read before it stays (the platform's defaults too).
  }
  return s;
}

namespace {
// One writer of settings.json at a time (its .tmp is shared): saves and load-modify-saves.
std::recursive_mutex &settings_mu() {
  static std::recursive_mutex mu;
  return mu;
}
}  // namespace

std::string save_settings(const fs::path &data, const Settings &s) {
  std::lock_guard g(settings_mu());
  auto j = read(data);
  j["answer"] = s.answer;
  if (s.mt_off) j["method"] = "off";
  else if (s.method) j["method"] = std::string(method_name(*s.method));
  else j.erase("method");
  j["hotkeys"] = {{"lookup", to_json(s.lookup)}, {"input", to_json(s.input)}};
  j["open_at_login"] = s.open_at_login;
  j["selection_icon"] = s.selection_icon;
  j["pin_moved"] = s.pin_moved;
  j["mt_english"] = s.mt_english;
  j["first_run"] = s.first_run;
  j["keep_model"] = s.keep_model;
  j["ignore_apps"] = s.ignore_apps;
  j.erase("copy_apps");  // the ⌘C reading is gone
  j["dicts"] = {{"order", s.dicts.order}, {"off", s.dicts.off}, {"folders", s.dicts.folders}, {"sources_off", s.dicts.sources_off}, {"got", s.dicts.got}};
  j["card"] = to_json(s.card);
  j["ui_lang"] = s.ui_lang;
  j["theme"] = s.theme;
  j["sync"] = s.sync;
  j["ai"] = to_json(s.ai);
  j["study"] = json::object();
  for (auto &[answer, langs] : s.study)
    if (!langs.empty()) j["study"][answer] = langs;
  j["speech"] = {{"on", s.speech.on}, {"male", s.speech.male}, {"gpu", s.speech.gpu}, {"keep", s.speech.keep}, {"off", s.speech.off}};
  auto &k = s.anki;
  j["anki"] = {{"on", k.on},           {"url", k.url},           {"words", k.words},           {"phrases", k.phrases},
               {"history", k.history}, {"words_on", k.words_on}, {"phrases_on", k.phrases_on}, {"history_on", k.history_on},
               {"sync", k.sync}};
  std::error_code ec;
  fs::create_directories(data, ec);
  auto tmp = data / "settings.json.tmp";
  {
    std::ofstream f(tmp, std::ios::binary);
    if (!(f << j.dump(2) << "\n") || !f.flush()) return fs::remove(tmp, ec), tr("err_write", {text::utf8_of(tmp.filename())});
  }
  fs::rename(tmp, data / "settings.json", ec);
  return ec ? ec.message() : "";
}

std::string update_settings(const fs::path &data, const std::function<void(Settings &)> &change) {
  std::lock_guard g(settings_mu());
  auto s = load_settings(data);
  change(s);
  return save_settings(data, s);
}

}  // namespace lexipeek
