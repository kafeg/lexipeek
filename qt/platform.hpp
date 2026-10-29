// What the OS does for the Qt shell: the shared layer (lexipeek/os.hpp, platform/) with Qt's types where they help.
#pragma once
#include <QImage>
#include <QRect>
#include <QStringList>
#include "lexipeek/os.hpp"

class QWidget;

namespace lexipeek::qt::platform {

using os::cli_runner;
using os::front_app;
using os::front_is_mine;
using os::Input;
using os::install;
using os::installed_here;
using os::installed_program;
using os::last_error;
using os::login;
using os::memory_low;
using os::mouse_down;
using os::probe_selection;
using os::quit_running;
using os::ram_mb;
using os::register_hotkey;
using os::register_scheme;
using os::remove_old;
using os::secret;
using os::set_login;
using os::set_secret;
using os::taskbar_light;
using os::uninstall;
using os::unregister_hotkey;
using os::watch_input;

struct Selected {
  std::string text;
  QRect bounds;  // on the screen, logical pixels; empty when unknown
};
// The focused app's selected text; nullopt when none or unreadable. Any thread; its bounds in physical pixels:
// logical() on the main thread.
std::optional<Selected> selection();
QRect logical(const QRect &px);

// One app per user: another one running gets these arguments (true: this launch quits); receive hands them over.
bool hand_over(const QStringList &args);
void receive(std::function<void(const QStringList &)> got);

// Brings a window to the front with the keyboard focus (Windows otherwise only flashes its taskbar button).
void activate(QWidget *w);
// A borderless window's rounded corners and shadow (Windows 11's own).
void round(QWidget *w);

// One glyph of the system's icon font drawn by the system, as an alpha mask of px × px device pixels; null where there
// is none (Qt does not draw that font's private-use code points).
QImage glyph_mask(const QString &family, char16_t code, int px);

}  // namespace lexipeek::qt::platform
