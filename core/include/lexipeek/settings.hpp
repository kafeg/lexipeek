// One JSON for the app and the CLI: <data>/settings.json. Unknown fields are kept as they are.
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "lexipeek/mt.hpp"

namespace lexipeek {

struct Hotkey {
  std::string key = "D";  // a letter or a digit
  bool option = true, command = false, control = false, shift = false;
};

// Other (own) dictionaries, as InkPeek's Prefs: their order with the packs', which are off, folders read in place.
struct DictSettings {
  std::vector<std::string> order;        // dictionary ids, packs and others together (Card → Article)
  std::vector<std::string> off;          // own dictionaries switched off
  std::vector<std::string> folders;      // folders added by the person (KOReader's, GoldenDict's), read in place
  std::vector<std::string> sources_off;  // catalog sources not listed
  std::vector<std::string> got;          // "<url>\t<name>\t…": what a catalog download brought (its ✓)
};

struct Settings {
  std::string answer = "ru";     // the answer language
  std::optional<Method> method;  // none: the first available
  Hotkey lookup{"D"}, input{"A"};
  bool open_at_login = false;
  bool selection_icon = false;  // a small icon next to a new selection opens the card
  bool pin_moved = false;       // a dragged card stays open, as pinned
  bool mt_english = false;      // Mozilla's English in between, under a translation through English
  bool first_run = false;       // the first-run window was finished once
  std::vector<std::string> ignore_apps;  // bundle ids where the hotkey does nothing
  std::vector<std::string> copy_apps;    // bundle ids where Cmd+C may read the selection (the clipboard is restored)
  DictSettings dicts;
};

// Missing or broken file: the defaults.
Settings load_settings(const std::filesystem::path &data);
// "" or the error. Written whole, then renamed.
std::string save_settings(const std::filesystem::path &data, const Settings &s);

}  // namespace lexipeek
