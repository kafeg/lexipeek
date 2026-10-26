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

struct Settings {
  std::string answer = "ru";     // the answer language
  std::optional<Method> method;  // none: the first available
  Hotkey lookup{"D"}, input{"A"};
  bool open_at_login = false;
  std::vector<std::string> ignore_apps;  // bundle ids where the hotkey does nothing
  std::vector<std::string> copy_apps;    // bundle ids where Cmd+C may read the selection (the clipboard is restored)
};

// Missing or broken file: the defaults.
Settings load_settings(const std::filesystem::path &data);
// "" or the error. Written whole, then renamed.
std::string save_settings(const std::filesystem::path &data, const Settings &s);

}  // namespace lexipeek
