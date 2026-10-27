#ifndef _WIN32
#include "platform.hpp"
#include <unistd.h>

// Not Windows: nothing yet (Linux later; on macOS the Qt shell is only built to work on it).
namespace lexipeek::qt::platform {

bool register_hotkey(int, const Hotkey &, std::function<void()>) { return false; }
void unregister_hotkey(int) {}
std::string hotkey_label(const Hotkey &h) {
  std::string s;
  if (h.control) s += "Ctrl+";
  if (h.option) s += "Alt+";
  if (h.shift) s += "Shift+";
  if (h.command) s += "Meta+";
  return s + h.key;
}
std::optional<Selected> selection() { return std::nullopt; }
std::string front_app() { return ""; }
bool mouse_down() { return false; }
void watch_input(std::function<void(Input)>) {}
bool front_is_mine() { return false; }
int ram_mb() { return static_cast<int>(sysconf(_SC_PHYS_PAGES) / 1024 * sysconf(_SC_PAGESIZE) / 1024); }
bool login() { return false; }
void set_login(bool) {}
std::string secret(const std::string &) { return ""; }
bool set_secret(const std::string &, const std::string &) { return false; }
void round(QWidget *) {}

}  // namespace lexipeek::qt::platform
#endif
