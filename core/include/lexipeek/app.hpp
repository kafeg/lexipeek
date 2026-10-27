// The facade for shells that cannot take the rest as is (Swift): the data dir as a string ("" = the default), plain
// structs, copyable handles, progress by polling — no std::function, std::filesystem::path or std::optional here.
#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "lexipeek/core.hpp"
#include "lexipeek/dicts.hpp"
#include "lexipeek/l10n.hpp"
#include "lexipeek/packs.hpp"
#include "lexipeek/settings.hpp"
#include "lexipeek/views.hpp"

namespace lexipeek::app {

std::string data_dir();
// This machine for the packs' requirements: memory and the int8 dot product (Apple Silicon: "asimddp").
Device device(int ram_mb, bool dotprod);

Settings settings(const std::string &data);
std::string save(const std::string &data, const Settings &s);
// The method's id ("fast", "direct", "llm"); "" for none.
std::string method_id(const Settings &s);
std::string method_id(Method m);
void set_method(Settings &s, const std::string &id);
std::string language(const std::string &code);
// The app's language (the setting, else the system's when it is a UI language, else English) for every text from now on;
// a new install (no settings.json yet) also answers in it.
std::string set_language(const std::string &data, const std::string &system);
// The shell's texts (l10n.hpp's tr): by key, {1} and {2} filled in.
std::string text(const std::string &key);
std::string text(const std::string &key, const std::string &a);
std::string text(const std::string &key, const std::string &a, const std::string &b);
// "Check for updates": GitHub's latest release of LexiPeek (network, blocking).
UpdateCheck check_updates(const std::string &version, const std::string &url = "https://api.github.com/repos/kafeg/lexipeek/releases/latest");

// One Core shared by the shell's queues: a copyable handle.
struct Engine {
  std::shared_ptr<Core> core;

  // With the settings of the data dir.
  static Engine make(const std::string &data, const Device &d);
  // The settings changed on disk: the next cards use them (no reload of the library or the store).
  void configure(const std::string &data) const;
  Card lookup(const std::string &text, Source source) const;
  // Again with the text's language or the answer language picked in the card ("" = as usual).
  Card lookup_as(const std::string &text, Source source, const std::string &lang, const std::string &answer) const;
  Translation translate(const Card &card, Method m) const;
  SectionPage section(const Card &card, int entry, const std::string &key, int page) const;
  std::vector<Seen> recents(int n) const;
  // ☆ in the card → whether it is saved now.
  bool toggle_saved(const Card &card, const std::string &translation) const;
  // Blocking, only on the person's action: the card's AI answer (the saved one offline), its error as a text; key: the
  // provider's API key from the Keychain, "" for none.
  AiState ask(const Card &card, ai::Ask a, const std::string &key) const;
  // Notes, in the settings' answer language.
  NotesView notes(NotesTab tab) const;
  void remove_note(NotesTab tab, const std::string &text) const;
  void clear_history() const;
  void prepare() const;
  void reload() const;
  void release() const;
  // The held model for the menu bar's menu.
  MemoryView memory() const;
  void cancel() const;
};

// Settings: a pane's rows by the data dir's settings; a row's change written there at once ("" or the error).
SettingsPage settings_page(const std::string &data, SettingsSection section, int tab, const SettingsState &state);
std::string toggle(const std::string &data, const std::string &key, bool on);
std::string choose(const std::string &data, const std::string &key, int index, const SettingsState &state);

// Settings → AI. The provider's id ("" = off; the shell keeps its key under it) and whether it needs a key.
std::string ai_provider(const std::string &data);
bool ai_needs_key(const std::string &data);
std::string ai_provider_title(const std::string &data);
// A text row's change (ai_url, ai_command, ai_model), the prompt editor and its save, every prompt back to the original.
std::string set_text(const std::string &data, const std::string &key, const std::string &value);
PromptEditor prompt_editor(const std::string &data, const std::string &id);
std::string set_prompts(const std::string &data, const std::vector<std::string> &ids, const std::vector<std::string> &texts);
std::string reset_prompts(const std::string &data);
// The provider's models (network, blocking): the list, or the error's text.
struct AiModels {
  std::vector<ai::Model> list;
  std::string error;
};
AiModels ai_models(const std::string &data, const std::string &key);
std::vector<std::string> model_titles(const std::vector<ai::Model> &list);
std::vector<ai::Model> filter_models(const std::vector<ai::Model> &list, const std::string &filter, bool free_only);
// AI → Connection's test (network, blocking): one short request, its answer's line or the error's text.
std::string ai_test(const std::string &data, const std::string &key);

// Settings → Packs.
std::vector<PairRow> rows(const std::string &data, const Device &d);
std::vector<std::string> answers(const std::string &data);
std::vector<std::string> missing_ids(const std::string &data, const std::string &from, const std::string &to);
std::vector<std::string> freed_ids(const std::string &data, const std::string &from, const std::string &to);
// The release's list (network): "" or why it is not fresh.
std::string refresh(const std::string &data);

// The first-run window (views.hpp): by the data dir's packs list and installed parts.
FirstRunView first_run(const std::string &data, const FirstRunState &s);
// It opens at launch (no pair yet, or Accessibility not granted and the window never finished).
bool first_run_at_launch(const std::string &data, bool trusted);
bool has_pair(const std::string &data);

// A download's progress and cancel, shared between the worker and the UI (polled).
struct Download {
  std::shared_ptr<std::atomic<std::int64_t>> done = std::make_shared<std::atomic<std::int64_t>>(0);
  std::shared_ptr<std::atomic<std::int64_t>> total = std::make_shared<std::atomic<std::int64_t>>(0);
  std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false);
  std::int64_t get_done() const { return *done; }
  std::int64_t get_total() const { return *total; }
  void cancel() const { *stop = true; }
};

// Blocking: the components one after another, progress over all of them.
Status install(const std::string &data, const std::vector<std::string> &ids, const Download &dl);
Status remove(const std::string &data, const std::vector<std::string> &ids);

// Settings → Other dictionaries (dicts.hpp). Each edit saves the settings; the shell then calls Engine::configure.
DictsView dictionaries(const std::string &data);
void set_dict_on(const std::string &data, const std::string &id, bool on);
// One step (-1 up, +1 down) or to an index (a drag) in the list as dictionaries() shows it.
void move_dict(const std::string &data, const std::string &id, int by);
void move_dict_to(const std::string &data, const std::string &id, int index);
// What to say: the error, a note ("No StarDict dictionaries in … yet") or "".
std::string add_folder(const std::string &data, const std::string &path);
void remove_folder(const std::string &data, const std::string &path);
std::string remove_dict(const std::string &data, const std::string &id);
// Blocking: files or archives into dicts-own.
Imported import_dicts(const std::string &data, const std::vector<std::string> &files);
// "Added: X. Already there: Y." or the error.
std::string imported_text(const Imported &r);
// The catalog: the switched-on sources' lists (network, blocking), its rows by a filter, one item's download.
CatalogList catalog(const std::string &data);
CatalogView catalog_view(const std::string &data, const CatalogList &list, const std::string &filter);
void set_source_on(const std::string &data, const std::string &id, bool on);
Imported download_dict(const std::string &data, const CatalogList &list, int item, const Download &dl);

}  // namespace lexipeek::app
