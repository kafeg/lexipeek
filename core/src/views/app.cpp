#include "lexipeek/app.hpp"
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include "lexipeek/l10n.hpp"
#include "ai/ai.hpp"
#include "packs/http.hpp"
#include "text/text.hpp"

namespace lexipeek::app {

namespace {
std::filesystem::path dir(const std::string &data) { return data.empty() ? default_data_dir() : text::path_of(data); }
}  // namespace

std::string data_dir() { return text::utf8_of(default_data_dir()); }
Device device(int ram_mb, const std::vector<std::string> &features, const std::string &arch) { return {ram_mb, features, arch}; }

Settings settings(const std::string &data) { return load_settings(dir(data)); }
std::string save(const std::string &data, const Settings &s) { return save_settings(dir(data), s); }
std::string method_id(const Settings &s) { return s.mt_off ? "off" : s.method ? method_id(*s.method) : ""; }
std::string method_id(Method m) { return std::string(method_name(m)); }
void set_method(Settings &s, const std::string &id) {
  s.mt_off = id == "off";
  s.method = method_from(id);
}
std::string language(const std::string &code) { return language_name(code); }

std::string set_language(const std::string &data, const std::string &system) {
  auto s = load_settings(dir(data));
  auto lang = ui_language(s.ui_lang, system);
  set_ui_language(lang);
  // A new install answers in the app's language; an existing settings.json keeps its answer.
  if (!std::filesystem::exists(dir(data) / "settings.json")) {
    s.answer = lang;
    save_settings(dir(data), s);
  }
  return lang;
}

std::string text(const std::string &key) { return tr(key); }
std::string text(const std::string &key, const std::string &a) { return tr(key, {a}); }
std::string text(const std::string &key, const std::string &a, const std::string &b) { return tr(key, {a, b}); }

UpdateCheck check_updates(const std::string &version, const std::string &url) {
  std::string body;
  long status = 0;
  auto err = http::get(url, body, &status);
  return update_check(version, status, body, err);
}

Engine Engine::make(const std::string &data, const Device &d) {
  auto s = load_settings(dir(data));
  CoreOptions o;
  o.data = dir(data);
  o.answer = s.answer;
  o.method = s.method;
  o.device = d;
  o.mt_english = s.mt_english;
  o.mt_off = s.mt_off;
  o.dicts = s.dicts;
  o.card = s.card;
  o.ai = s.ai;
  o.translator.idle_bergamot = o.translator.idle_llm = std::chrono::minutes(s.keep_model);
  if (auto *r = std::getenv("LEXIPEEK_AI_REPLIES")) o.ai_replies = r;
  return {std::make_shared<Core>(std::move(o))};
}

void Engine::configure(const std::string &data) const { core->configure(load_settings(dir(data))); }
Card Engine::lookup(const std::string &text, Source source) const { return core->lookup({text, source, ""}); }
Card Engine::lookup_as(const std::string &text, Source source, const std::string &lang, const std::string &answer) const {
  return core->lookup({text, source, answer, lang});
}
Translation Engine::translate(const Card &card, Method m) const { return core->translate(card, m); }
SectionPage Engine::section(const Card &card, int entry, const std::string &key, int page) const { return core->section(card, entry, key, page); }
std::vector<Seen> Engine::recents(int n) const { return core->recents(n); }
bool Engine::toggle_saved(const Card &card, const std::string &translation) const { return core->toggle_saved(card, translation); }

AiState Engine::ask(const Card &card, ai::Ask a, const std::string &key) const {
  auto r = core->ask(card, a, key);
  AiState st;
  st.answered = true, st.ok = r.ok(), st.saved = r.saved;
  if (r.ok()) st.json = r.json;
  else st.error = ai::error_text(r, load_settings(core->data()).ai);
  return st;
}
NotesView Engine::notes(NotesTab tab, int page) const { return notes_view(core->store(), tab, core->answer(), page); }
bool Engine::saved(const Card &card) const { return core->store().is_saved(card.save_text) || core->store().is_saved(card.text); }
void Engine::remove_note(NotesTab tab, const std::string &text) const { lexipeek::remove_note(core->store(), tab, text); }
void Engine::clear_history() const { core->store().clear_history(); }

std::string Engine::backup(const std::string &file) const {
  auto e = core->store().backup(text::path_of(file));
  return e.empty() ? tr("backup_done", {file}) : tr("failed", {e});
}

std::string Engine::restore(const std::string &file) const {
  auto r = core->store().restore(text::path_of(file));
  if (r.error == "not a backup") return tr("restore_bad");
  return r.error.empty() ? tr("restore_done", {digits(r.added)}) : tr("failed", {r.error});
}

std::string Engine::export_tsv(NotesTab tab, const std::string &file) const {
  auto tsv = notes_tsv(core->store(), tab, core->answer());
  std::ofstream f(text::path_of(file), std::ios::binary);
  f << tsv;
  if (!f) return tr("failed", {file});
  return tr("exported", {file, digits(static_cast<long long>(std::count(tsv.begin(), tsv.end(), '\n')))});
}
// At launch: the dictionaries' indexes, langid, and the day's copy of the notes (<data>/backup, the last five).
void Engine::prepare() const {
  core->prepare();
  core->store().daily_backup(core->data() / "backup");
}
void Engine::reload() const { core->reload(); }
void Engine::release() const { core->release(); }
MemoryView Engine::memory() const { return memory_view(core->memory()); }
void Engine::cancel() const { core->cancel(); }

SettingsPage settings_page(const std::string &data, SettingsSection section, int tab, const SettingsState &state) {
  auto st = state;
  if (section == SettingsSection::languages && tab == 1 && st.methods.empty()) {
    Packs p(dir(data), "");
    st.methods = method_infos(p.cached(), p.installed(), st.device);
  }
  return settings_view(section, tab, load_settings(dir(data)), st);
}

std::string toggle(const std::string &data, const std::string &key, bool on) {
  auto s = load_settings(dir(data));
  settings_toggle(s, key, on);
  return save_settings(dir(data), s);
}

std::string choose(const std::string &data, const std::string &key, int index, const SettingsState &state) {
  auto s = load_settings(dir(data));
  settings_choose(s, key, index, state);
  return save_settings(dir(data), s);
}

std::string ai_provider(const std::string &data) { return load_settings(dir(data)).ai.provider; }

std::string ai_provider_title(const std::string &data) {
  auto *p = ai::provider(ai_provider(data));
  return !p ? "" : p->id == "custom" ? tr("ai_custom") : p->title;
}

std::string set_text(const std::string &data, const std::string &key, const std::string &value) {
  auto s = load_settings(dir(data));
  settings_text(s, key, value);
  return save_settings(dir(data), s);
}

PromptEditor prompt_editor(const std::string &data, const std::string &id) { return lexipeek::prompt_editor(load_settings(dir(data)), id); }

std::string set_prompts(const std::string &data, const std::vector<std::string> &ids, const std::vector<std::string> &texts) {
  auto s = load_settings(dir(data));
  settings_prompts(s, ids, texts);
  return save_settings(dir(data), s);
}

std::string reset_prompts(const std::string &data) {
  auto s = load_settings(dir(data));
  s.ai.prompts.clear();
  return save_settings(dir(data), s);
}

namespace {
ai::Transport transport() {
  auto *r = std::getenv("LEXIPEEK_AI_REPLIES");
  return r ? ai::file_transport(r) : ai::default_transport();
}
}  // namespace

AiModels ai_models(const std::string &data, const std::string &key) {
  auto s = load_settings(dir(data));
  auto m = ai::models(s.ai, key, transport());
  return {m.list, m.error.ok() ? "" : ai::error_text(m.error, s.ai)};
}

std::vector<std::string> model_titles(const std::vector<ai::Model> &list) { return ai::model_titles(list); }
std::vector<ai::Model> filter_models(const std::vector<ai::Model> &list, const std::string &filter, bool free_only) {
  return ai::filter_models(list, filter, free_only);
}

std::string ai_test(const std::string &data, const std::string &key) {
  auto s = load_settings(dir(data));
  auto lang = catalog_language(s.answer);
  ai::Query q{ai::Ask::quick, key, s.answer == "en" ? "Haus" : "house", 1, false, lang, s.answer == "en" ? "German" : "English"};
  auto recall = s.ai.recall;
  auto r = ai::ask(q, s.ai, recall, transport());
  if (recall != s.ai.recall) {
    s.ai.recall = recall;
    save_settings(dir(data), s);
  }
  return r.ok() ? tr("ai_test_ok", {ai::brief(r.json)}) : ai::error_text(r, s.ai);
}

std::vector<PairRow> rows(const std::string &data, const Device &d) {
  Packs p(dir(data));
  return pack_rows(p.cached(), p.installed(), d);
}

std::vector<std::string> answers(const std::string &data) { return answer_languages(Packs(dir(data)).cached()); }

PacksView packs(const std::string &data, const Device &d) {
  Packs p(dir(data), "");
  return packs_view(p.cached(), p.installed(), d, load_settings(dir(data)).method.value_or(Method::fast));
}

std::vector<std::string> missing_ids(const std::string &data, const std::string &from, const std::string &to) {
  Packs p(dir(data));
  return lexipeek::missing_ids(p.cached(), p.installed(), {from, to}, load_settings(dir(data)).method.value_or(Method::fast));
}

std::vector<std::string> freed_ids(const std::string &data, const std::string &from, const std::string &to) {
  Packs p(dir(data));
  return lexipeek::freed_ids(p.cached(), p.installed(), {from, to});
}

std::string refresh(const std::string &data) { return Packs(dir(data)).fetch().error; }

FirstRunView first_run(const std::string &data, const FirstRunState &s) {
  Packs p(dir(data));
  return first_run_view(p.cached(), p.installed(), s);
}

bool has_pair(const std::string &data) {
  Packs p(dir(data));
  return lexipeek::has_pair(p.cached(), p.installed());
}

bool first_run_at_launch(const std::string &data, bool trusted) {
  return lexipeek::first_run_at_launch(has_pair(data), trusted, load_settings(dir(data)).first_run);
}

Status install(const std::string &data, const std::vector<std::string> &ids, const Download &dl) {
  Packs p(dir(data));
  auto m = p.cached();
  auto size = [&](const std::string &id) {
    for (auto &c : m.components)
      if (c.id == id) return c.size;
    return std::int64_t{0};
  };
  std::int64_t total = 0, base = 0;
  for (auto &id : ids) total += size(id);
  *dl.total = total;
  for (auto &id : ids) {
    auto s = p.install(id, [&](std::int64_t done, std::int64_t) { *dl.done = base + done; }, [&] { return dl.stop->load(); });
    if (!s.ok()) return s;
    base += size(id);
  }
  return {};
}

Status remove(const std::string &data, const std::vector<std::string> &ids) {
  Packs p(dir(data));
  for (auto &id : ids)
    if (auto s = p.remove(id); !s.ok()) return s;
  return {};
}

DictsView dictionaries(const std::string &data) { return dicts_view(dir(data)); }

namespace {
template <class F>
void edit(const std::string &data, F f) {
  auto s = load_settings(dir(data));
  f(s.dicts);
  save_settings(dir(data), s);
}

std::vector<std::string> shown(const std::string &data) {
  std::vector<std::string> ids;
  for (auto &r : dicts_view(dir(data)).rows) ids.push_back(r.id);
  return ids;
}
}  // namespace

void set_dict_on(const std::string &data, const std::string &id, bool on) {
  edit(data, [&](DictSettings &d) { lexipeek::set_dict_on(d, id, on); });
}

void move_dict(const std::string &data, const std::string &id, int by) {
  auto ids = shown(data);
  edit(data, [&](DictSettings &d) { lexipeek::move_dict(d, ids, id, by); });
}

void move_dict_to(const std::string &data, const std::string &id, int index) {
  auto ids = shown(data);
  edit(data, [&](DictSettings &d) { lexipeek::move_dict_to(d, ids, id, index); });
}

std::string add_folder(const std::string &data, const std::string &path) {
  std::string err, note;
  edit(data, [&](DictSettings &d) { err = lexipeek::add_folder(d, path, note); });
  return err.empty() ? note : err;
}

void remove_folder(const std::string &data, const std::string &path) {
  edit(data, [&](DictSettings &d) { lexipeek::remove_folder(d, path); });
}

std::string remove_dict(const std::string &data, const std::string &id) {
  std::string err;
  edit(data, [&](DictSettings &d) { err = lexipeek::remove_dict(dir(data), d, id); });
  return err;
}

Imported import_dicts(const std::string &data, const std::vector<std::string> &files) {
  std::vector<std::filesystem::path> paths;
  for (auto &f : files) paths.push_back(text::path_of(f));
  return lexipeek::import_dicts(paths, own_dir(dir(data)));
}

std::string imported_text(const Imported &r) {
  if (!r.error.empty()) return r.error;
  auto list = [](const std::vector<std::string> &v) { return text::join(v); };
  std::string out;
  if (!r.added.empty()) out += tr("added_x", {list(r.added)});
  if (!r.already.empty()) out += std::string(out.empty() ? "" : " ") + tr("already_x", {list(r.already)});
  return out;
}

CatalogList catalog(const std::string &data) { return fetch_catalog(load_settings(dir(data)).dicts); }

CatalogView catalog_view(const std::string &data, const CatalogList &list, const std::string &filter) {
  auto s = load_settings(dir(data));
  return lexipeek::catalog_view(list, s.dicts, s.answer, filter, present_names(dir(data)));
}

void set_source_on(const std::string &data, const std::string &id, bool on) {
  edit(data, [&](DictSettings &d) { lexipeek::set_source_on(d, id, on); });
}

Imported download_dict(const std::string &data, const CatalogList &list, int item, const Download &dl) {
  if (item < 0 || item >= static_cast<int>(list.items.size())) return {{}, {}, tr("no_dict")};
  auto &it = list.items[static_cast<size_t>(item)];
  *dl.total = it.size;
  return lexipeek::download_dict(
      dir(data), it, [&](std::int64_t done, std::int64_t total) { *dl.done = done, *dl.total = total; }, [&] { return dl.stop->load(); });
}

}  // namespace lexipeek::app
