// What the OS does for the shell (Windows: platform_win.cpp; elsewhere platform_none.cpp, nothing yet).
#pragma once
#include <QRect>
#include <QStringList>
#include <functional>
#include <optional>
#include <string>
#include "lexipeek/ai.hpp"
#include "lexipeek/settings.hpp"

class QWidget;

namespace lexipeek::qt::platform {

// A system-wide hotkey (the core's Hotkey: option = Alt, control = Ctrl, command = Win); false when another app has it.
// fire runs on the main thread. A key code (key "Esc") for the card's own keys while it is open.
bool register_hotkey(int id, const Hotkey &h, std::function<void()> fire);
void unregister_hotkey(int id);
std::string hotkey_label(const Hotkey &h);  // "Alt+D"

struct Selected {
  std::string text;
  QRect bounds;  // on the screen, logical pixels; empty when unknown
};
// The focused app's selected text (Windows: UI Automation, never the clipboard); nullopt when none or unreadable.
std::optional<Selected> selection();
// What UI Automation sees at the focus now (the element, its patterns, the selection's text): a diagnostic.
std::string probe_selection();
// The focused app's program ("chrome.exe"): Settings → General's apps where the hotkey does nothing.
std::string front_app();
// The mouse's left or right button is down now (anywhere on the screen).
bool mouse_down();
// Input anywhere (the icon at a selection): the left button down or up, or another key or the wheel; nullptr stops it.
enum class Input { down, up, other };
void watch_input(std::function<void(Input)> on);
// This process owns the foreground window.
bool front_is_mine();

// This machine's memory, MB.
int ram_mb();

// Opens at login (Windows: the Run key).
bool login();
void set_login(bool on);

// The AI key per provider in the OS's store (Windows: Credential Manager); "" none.
std::string secret(const std::string &provider);
bool set_secret(const std::string &provider, const std::string &value);  // "" removes it

// One app per user: another one running gets these arguments (true: this launch quits); receive hands them over.
bool hand_over(const QStringList &args);
void receive(std::function<void(const QStringList &)> got);

// lexipeek:// links open this program (Windows: HKCU\Software\Classes, per user).
void register_scheme();

// How CLI AI providers run here (Windows: a process in a job object, the prompt on stdin); none: the core's POSIX shell.
std::function<ai::CliRan(const ai::CliCall &)> cli_runner();

// The OS's last error as text ("" none): what a failed call above said.
std::string last_error();

// Brings a window to the front with the keyboard focus (Windows otherwise only flashes its taskbar button).
void activate(QWidget *w);

// A borderless window's rounded corners and shadow (Windows 11's own).
void round(QWidget *w);

}  // namespace lexipeek::qt::platform
