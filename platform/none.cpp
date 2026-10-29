// Not Windows: nothing yet (Linux later; on macOS the Qt shell is only built to work on it).
#include <unistd.h>
#include "lexipeek/os.hpp"

namespace lexipeek::os {

bool register_hotkey(int, const Hotkey &, std::function<void()>) { return false; }
void unregister_hotkey(int) {}
std::optional<Selected> selection() { return std::nullopt; }
std::string probe_selection() { return ""; }
std::string front_app() { return ""; }
bool mouse_down() { return false; }
void watch_input(std::function<void(Input)>) {}
bool front_is_mine() { return false; }
int ram_mb() { return static_cast<int>(sysconf(_SC_PHYS_PAGES) / 1024 * sysconf(_SC_PAGESIZE) / 1024); }
bool memory_low() { return false; }
bool login() { return false; }
void set_login(bool) {}
std::string secret(const std::string &) { return ""; }
bool set_secret(const std::string &, const std::string &) { return false; }
bool hand_over(const std::vector<std::string> &) { return false; }
void receive(std::function<void(const std::vector<std::string> &)>) {}
void quit_running() {}
bool installed_here() { return true; }
std::string install(const std::string &, bool) { return ""; }
std::string installed_program() { return ""; }
void uninstall() {}
void remove_old() {}
bool start(const std::string &) { return false; }
void register_scheme() {}
std::string scheme_command() { return ""; }
std::function<ai::CliRan(const ai::CliCall &)> cli_runner() { return nullptr; }
std::string last_error() { return ""; }
void activate(void *) {}
void round(void *) {}
std::vector<unsigned char> glyph_mask(const std::string &, char16_t, int) { return {}; }
std::string icon_font() { return ""; }
bool taskbar_light() { return false; }
std::string ui_language() { return "en"; }
int self_test(std::ostream &) { return 0; }

}  // namespace lexipeek::os
