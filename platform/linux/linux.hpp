// Linux's OS parts: what input.cpp and system.cpp share. Qt's event loop (QtCore) and D-Bus (QtDBus) inside; X11 by Xlib.
#pragma once
#include <filesystem>
#include <string>

namespace lexipeek::os::linux_ {

// This program's path (/proc/self/exe).
std::string program();
// XDG's dirs: $XDG_CONFIG_HOME or ~/.config, $XDG_DATA_HOME or ~/.local/share.
std::filesystem::path config_home();
std::filesystem::path data_home();
// A Wayland session (XWayland may still give X11 to this process).
bool wayland();
// The desktop file's name and the portals' app id.
inline const char *app_id = "dev.kafeg.lexipeek";
// Writes a file only when its text differs (a launch does not touch what is already right); false on an error.
bool write_if_changed(const std::filesystem::path &file, const std::string &text);
// A value for a desktop file's Exec= ("…" with its specials escaped).
std::string exec_quote(const std::string &arg);
void set_error(const std::string &e);

}  // namespace lexipeek::os::linux_
