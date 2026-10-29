// What the Windows parts share: text conversions, the last error, the hidden window that gets hotkeys and launches.
#pragma once
// clang-format off
#include <windows.h>
// clang-format on
#include <functional>
#include <string>
#include <vector>

namespace lexipeek::os::win {

std::wstring wide(const std::string &s);
std::string utf8(const wchar_t *s, int n = -1);
inline std::string utf8(const std::wstring &s) { return utf8(s.c_str(), static_cast<int>(s.size())); }
std::wstring lower(std::wstring s);

// Notes the error of a failed call (before anything else changes GetLastError); returns ok.
bool noted(bool ok);

// This program's file and folder; an environment variable ("" none).
std::wstring program_file();
std::wstring program_dir();
std::wstring env(const wchar_t *name);

// A hidden top-level window on the thread that first asks for it (the UI thread): WM_HOTKEY, WM_COPYDATA, posted work.
HWND hidden();
// Runs f on the hidden window's thread after the current message (any thread).
void post(std::function<void()> f);
// The hidden window's handlers: a hotkey by its id, a second launch's arguments.
void on_hotkey(std::function<void(int)> f);
void on_copydata(std::function<void(const std::string &)> f);

// HKCU values (path under HKEY_CURRENT_USER; name "" = the default value).
std::wstring reg_get(const std::wstring &path, const std::wstring &name);
bool reg_has(const std::wstring &path, const std::wstring &name);
void reg_set(const std::wstring &path, const std::wstring &name, const std::wstring &value);
void reg_set(const std::wstring &path, const std::wstring &name, DWORD value);
void reg_remove(const std::wstring &path, const std::wstring &name);
void reg_remove_key(const std::wstring &path);

}  // namespace lexipeek::os::win
