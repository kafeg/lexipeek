// What the OS does for a shell, shared by win/ and qt/ (Windows: platform/win/*.cpp; elsewhere platform/none.cpp).
// No UI toolkit here: UTF-8 strings, physical pixels, window handles as void *. Callbacks run on the thread that
// registered them (its message loop: WinUI's or Qt's).
#pragma once
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>
#include "lexipeek/ai.hpp"
#include "lexipeek/settings.hpp"

namespace lexipeek::os {

// A system-wide hotkey (the core's Hotkey: option = Alt, control = Ctrl, command = Win); false when another app has it.
// A key code (key "Esc") for the card's own keys while it is open.
bool register_hotkey(int id, const Hotkey &h, std::function<void()> fire);
void unregister_hotkey(int id);

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
};
struct Selected {
  std::string text;
  Rect bounds;  // on the screen, physical pixels; empty when unknown
};
// The focused app's selected text (UI Automation, else a classic Edit's EM_GETSEL; never the clipboard); nullopt when
// none or unreadable within 1.5 s. Any thread.
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
// The system says memory is low now (its low-memory notification): the held model is freed.
bool memory_low();

// Opens at login (the Run key).
bool login();
void set_login(bool on);

// The AI key per provider in the OS's store (Credential Manager); "" none.
std::string secret(const std::string &provider);
bool set_secret(const std::string &provider, const std::string &value);  // "" removes it

// One app per user: another one running gets these arguments (true: this launch quits); receive hands them over.
bool hand_over(const std::vector<std::string> &args);
void receive(std::function<void(const std::vector<std::string> &)> got);
void quit_running();  // a running one quits (an install or uninstall replaces it); waits for it a few seconds

// Per-user install: %LOCALAPPDATA%\Programs\LexiPeek, a Start menu shortcut, an entry in Settings → Apps.
bool installed_here();  // this copy runs from there (or the OS has no install: true)
// Copies this program there (folder: the whole folder it runs from, else the exe alone); "" or the error.
std::string install(const std::string &version, bool folder);
std::string installed_program();  // the installed copy's path
void uninstall();                 // shortcut, entries (Apps, Run, lexipeek://), the folder once this ends
void remove_old();                // what an update left in the installed folder (files in use were renamed)
bool start(const std::string &program);  // a program started on its own

// lexipeek:// links open this program (HKCU\Software\Classes, per user).
void register_scheme();
std::string scheme_command();  // what lexipeek:// runs now; "" none

// How CLI AI providers run here (a process in a job object, the prompt on stdin); none: the core's POSIX shell.
std::function<ai::CliRan(const ai::CliCall &)> cli_runner();

// The OS's last error as text ("" none): what a failed call above said.
std::string last_error();

// Brings a window to the front with the keyboard focus (Windows otherwise only flashes its taskbar button).
void activate(void *hwnd);
// A borderless window's rounded corners and shadow (Windows 11's own).
void round(void *hwnd);

// One glyph of an icon font (Segoe Fluent Icons / Segoe MDL2 Assets) drawn by the system: px × px alpha bytes, row by
// row; empty where there is none.
std::vector<unsigned char> glyph_mask(const std::string &family, char16_t code, int px);
// The installed icon font: "Segoe Fluent Icons" (Windows 11), else "Segoe MDL2 Assets" (10); "" none.
std::string icon_font();

// The taskbar is light (its tray icons black); Windows can set it apart from the apps' light or dark.
bool taskbar_light();

// The system's UI language ("ru" of "ru-RU").
std::string ui_language();

// `--self-test`: the OS parts that need no window (Credential Manager, the Run key put back, hotkeys, memory, CLI
// runs, lexipeek://), one line each "ok|FAIL what" into out; the failures' count.
int self_test(std::ostream &out);

}  // namespace lexipeek::os
