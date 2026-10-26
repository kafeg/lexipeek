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
  return {std::make_shared<Core>(std::move(o))};
}

void Engine::configure(const std::string &data) const { core->configure(load_settings(dir(data))); }
Card Engine::lookup(const std::string &text, Source source) const { return core->lookup({text, source, ""}); }
Translation Engine::translate(const Card &card, Method m) const { return core->translate(card, m); }
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

}  // namespace lexipeek::app
