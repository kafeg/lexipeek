#ifndef _WIN32
#include "platform.hpp"
#include <unistd.h>
#include <QWidget>

// Not Windows: nothing yet (Linux later; on macOS the Qt shell is only built to work on it).
namespace lexipeek::qt::platform {

bool register_hotkey(int, const Hotkey &, std::function<void()>) { return false; }
void unregister_hotkey(int) {}
std::optional<Selected> selection() { return std::nullopt; }
std::string probe_selection() { return ""; }
std::string front_app() { return ""; }
bool mouse_down() { return false; }
void watch_input(std::function<void(Input)>) {}
bool front_is_mine() { return false; }
int ram_mb() { return static_cast<int>(sysconf(_SC_PHYS_PAGES) / 1024 * sysconf(_SC_PAGESIZE) / 1024); }
bool login() { return false; }
void set_login(bool) {}
std::string secret(const std::string &) { return ""; }
bool set_secret(const std::string &, const std::string &) { return false; }
bool hand_over(const QStringList &) { return false; }
void receive(std::function<void(const QStringList &)>) {}
void register_scheme() {}
std::function<ai::CliRan(const ai::CliCall &)> cli_runner() { return nullptr; }
std::string last_error() { return ""; }
void activate(QWidget *w) {
  w->raise();
  w->activateWindow();
}
QImage glyph_mask(const QString &, char16_t, int) { return {}; }
bool taskbar_light() { return false; }
bool memory_low() { return false; }
QRect logical(const QRect &px) { return px; }
void round(QWidget *) {}

}  // namespace lexipeek::qt::platform
#endif
