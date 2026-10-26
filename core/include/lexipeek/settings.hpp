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

// The card's settings (InkPeek's Card section): Opening, Article, Appearance.
struct CardPrefs {
  std::string phrase_open = "offline";  // a phrase opens on: "offline" (Translate) or "dict" (word by word)
  bool full = false;                    // a word's dictionaries as full entries, not short lines
  bool grammar = true;                  // the header's grammar from the packs: "das Haus (Hauses, pl. Häuser)"
  std::vector<std::string> sections{"rel", "syn", "expr", "forms"};  // .rel sections shown (InkPeek: origin off)
  int width = 400;      // points (a passage: 60 more)
  int text = 13;        // points
  std::string font;     // "" the system's, "serif", "rounded", "mono"
  int height = 45;      // % of the screen at most
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
  CardPrefs card;
  std::string ui_lang;                  // the app's language, "" = the system's
};

// Missing or broken file: the defaults.
Settings load_settings(const std::filesystem::path &data);
// "" or the error. Written whole, then renamed.
std::string save_settings(const std::filesystem::path &data, const Settings &s);

}  // namespace lexipeek
