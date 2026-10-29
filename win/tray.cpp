#include "pch.h"
#include "tray.hpp"
#include <windowsx.h>
#include "lexipeek/os.hpp"
#include "resource.h"

namespace lexipeek::win {

namespace {
constexpr UINT callback = WM_APP + 10;
const UINT taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

// The system's warning glyph in a colour as an icon of px × px.
HICON glyph_icon(char16_t code, int px, COLORREF color) {
  auto mask = os::glyph_mask(os::icon_font(), code, px);
  if (mask.empty()) return nullptr;
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof bi;
  bi.bV5Width = px, bi.bV5Height = -px, bi.bV5Planes = 1, bi.bV5BitCount = 32, bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000, bi.bV5GreenMask = 0x0000FF00, bi.bV5BlueMask = 0x000000FF, bi.bV5AlphaMask = 0xFF000000;
  void *bits = nullptr;
  HDC dc = GetDC(nullptr);
  HBITMAP color_bmp = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  if (!color_bmp) return nullptr;
  auto *p = static_cast<DWORD *>(bits);
  DWORD rgb = (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
  for (size_t i = 0; i < mask.size(); ++i) p[i] = (static_cast<DWORD>(mask[i]) << 24) | rgb;
  HBITMAP mono = CreateBitmap(px, px, 1, 1, nullptr);
  ICONINFO ii{TRUE, 0, 0, mono, color_bmp};
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(mono), DeleteObject(color_bmp);
  return icon;
}
}  // namespace

Tray::Tray() {
  WNDCLASSW c{};
  c.lpfnWndProc = proc;
  c.hInstance = GetModuleHandleW(nullptr);
  c.lpszClassName = L"LexiPeekTray";
  RegisterClassW(&c);
  // Top-level (a message-only window misses TaskbarCreated and WM_SETTINGCHANGE), never shown.
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, c.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, c.hInstance, nullptr);
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

HICON Tray::icon(bool warn) {
  int px = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(hwnd_));
  bool light = os::taskbar_light();
  if (warn)
    if (auto i = glyph_icon(0xE7BA, px, light ? RGB(0, 0, 0) : RGB(255, 255, 255))) return i;
  return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(light ? IDI_TRAY_BLACK : IDI_TRAY_WHITE), IMAGE_ICON, px, px, 0));
}

void Tray::show(bool warn, const std::wstring &tip) {
  warn_ = warn, tip_ = tip;
  auto old = icon_;
  icon_ = icon(warn);
  NOTIFYICONDATAW d{};
  d.cbSize = sizeof d;
  d.hWnd = hwnd_;
  d.uID = 1;
  d.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
  d.uCallbackMessage = callback;
  d.hIcon = icon_;
  wcsncpy_s(d.szTip, tip.c_str(), _TRUNCATE);
  if (!added_ || !Shell_NotifyIconW(NIM_MODIFY, &d)) add();
  if (old) DestroyIcon(old);
}

void Tray::add() {
  NOTIFYICONDATAW d{};
  d.cbSize = sizeof d;
  d.hWnd = hwnd_;
  d.uID = 1;
  d.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
  d.uCallbackMessage = callback;
  d.hIcon = icon_;
  wcsncpy_s(d.szTip, tip_.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_DELETE, &d);
  added_ = Shell_NotifyIconW(NIM_ADD, &d);
  d.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &d);
}

void Tray::remove() {
  if (!added_) return;
  NOTIFYICONDATAW d{};
  d.cbSize = sizeof d;
  d.hWnd = hwnd_;
  d.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &d);
  added_ = false;
}

RECT Tray::rect() const {
  NOTIFYICONIDENTIFIER id{sizeof id, hwnd_, 1, {}};
  RECT r{};
  if (FAILED(Shell_NotifyIconGetRect(&id, &r))) return {};
  return r;
}

LRESULT CALLBACK Tray::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto *t = reinterpret_cast<Tray *>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (!t) return DefWindowProcW(h, m, w, l);
  if (m == callback) {
    switch (LOWORD(l)) {
      case NIN_SELECT:
      case NIN_KEYSELECT:
        if (t->activated) t->activated();
        break;
      case WM_CONTEXTMENU:
        if (t->menu) t->menu({GET_X_LPARAM(w), GET_Y_LPARAM(w)});
        break;
    }
    return 0;
  }
  if (m == taskbar_created) {
    t->added_ = false;
    t->show(t->warn_, t->tip_);
    return 0;
  }
  if (m == WM_SETTINGCHANGE && l && wcscmp(reinterpret_cast<const wchar_t *>(l), L"ImmersiveColorSet") == 0) {
    if (t->theme_changed) t->theme_changed();
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

}  // namespace lexipeek::win
