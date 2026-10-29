// The notification area's icon (Shell_NotifyIcon): its glyph, tooltip and clicks; back after Explorer restarts.
#pragma once
#include <functional>
#include <string>

namespace lexipeek::win {

class Tray {
 public:
  Tray();
  std::function<void()> activated;          // a click or a double click
  std::function<void(POINT)> menu;          // the context menu asked for at this screen point (physical pixels)
  std::function<void()> theme_changed;      // light or dark switched (the taskbar's glyph follows)
  void show(bool warn, const std::wstring &tip);  // the glyph in the taskbar's colour, ⚠ when warn
  void remove();
  RECT rect() const;  // the icon on the screen; empty when it is in the overflow and not shown
  HWND window() const { return hwnd_; }

 private:
  static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l);
  void add();
  HICON icon(bool warn);
  HWND hwnd_ = nullptr;
  bool added_ = false, warn_ = false;
  std::wstring tip_;
  HICON icon_ = nullptr;
};

}  // namespace lexipeek::win
