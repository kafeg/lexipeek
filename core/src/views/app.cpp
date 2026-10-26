#include "lexipeek/app.hpp"

namespace lexipeek::app {

namespace {
std::filesystem::path dir(const std::string &data) { return data.empty() ? default_data_dir() : std::filesystem::path(data); }
}  // namespace

std::string data_dir() { return default_data_dir().string(); }
Device device(int ram_mb, bool dotprod) { return {ram_mb, dotprod ? std::vector<std::string>{"asimddp"} : std::vector<std::string>{}}; }

Settings settings(const std::string &data) { return load_settings(dir(data)); }
std::string save(const std::string &data, const Settings &s) { return save_settings(dir(data), s); }
std::string method_id(const Settings &s) { return s.method ? method_id(*s.method) : ""; }
std::string method_id(Method m) { return std::string(method_name(m)); }
void set_method(Settings &s, const std::string &id) { s.method = method_from(id); }
std::string language(const std::string &code) { return language_name(code); }

Engine Engine::make(const std::string &data, const Device &d) {
  auto s = load_settings(dir(data));
  CoreOptions o;
  o.data = dir(data);
  o.answer = s.answer;
  o.method = s.method;
  o.device = d;
  o.mt_english = s.mt_english;
  o.dicts = s.dicts;
  return {std::make_shared<Core>(std::move(o))};
}

void Engine::configure(const std::string &data) const { core->configure(load_settings(dir(data))); }
Card Engine::lookup(const std::string &text, Source source) const { return core->lookup({text, source, ""}); }
Translation Engine::translate(const Card &card, Method m) const { return core->translate(card, m); }
SectionPage Engine::section(const Card &card, int entry, const std::string &key, int page) const { return core->section(card, entry, key, page); }
std::vector<Seen> Engine::recents(int n) const { return core->recents(n); }
bool Engine::toggle_saved(const Card &card, const std::string &translation) const { return core->toggle_saved(card, translation); }
NotesView Engine::notes(NotesTab tab) const { return notes_view(core->store(), tab, core->answer()); }
void Engine::remove_note(NotesTab tab, const std::string &text) const { lexipeek::remove_note(core->store(), tab, text); }
void Engine::clear_history() const { core->store().clear_history(); }
void Engine::prepare() const { core->prepare(); }
void Engine::reload() const { core->reload(); }
void Engine::release() const { core->release(); }
void Engine::cancel() const { core->cancel(); }

std::vector<PairRow> rows(const std::string &data, const Device &d) {
  Packs p(dir(data));
  return pack_rows(p.cached(), p.installed(), d);
}

std::vector<std::string> answers(const std::string &data) { return answer_languages(Packs(dir(data)).cached()); }

std::vector<std::string> missing_ids(const std::string &data, const std::string &from, const std::string &to) {
  Packs p(dir(data));
  return lexipeek::missing_ids(p.cached(), p.installed(), {from, to});
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
  std::vector<std::filesystem::path> paths(files.begin(), files.end());
  return lexipeek::import_dicts(paths, own_dir(dir(data)));
}

std::string imported_text(const Imported &r) {
  if (!r.error.empty()) return r.error;
  auto list = [](const std::vector<std::string> &v) {
    std::string s;
    for (auto &x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
  };
  std::string out;
  if (!r.added.empty()) out += "Added: " + list(r.added) + ".";
  if (!r.already.empty()) out += std::string(out.empty() ? "" : " ") + "Already there: " + list(r.already) + ".";
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
  if (item < 0 || item >= static_cast<int>(list.items.size())) return {{}, {}, "No such dictionary"};
  auto &it = list.items[static_cast<size_t>(item)];
  *dl.total = it.size;
  return lexipeek::download_dict(
      dir(data), it, [&](std::int64_t done, std::int64_t total) { *dl.done = done, *dl.total = total; }, [&] { return dl.stop->load(); });
}

}  // namespace lexipeek::app
