// The machine and the session: memory, the Run key, the Credential Manager, windows, the icon font, themes.
#include "lexipeek/os.hpp"
#include "win.hpp"
// clang-format off
#include <dwmapi.h>
#include <wincred.h>
// clang-format on

namespace lexipeek::os {

using namespace win;

namespace {
const std::wstring run_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::wstring target(const std::string &provider) { return wide("dev.kafeg.lexipeek.ai/" + provider); }
}  // namespace

int ram_mb() {
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof m;
  return GlobalMemoryStatusEx(&m) ? static_cast<int>(m.ullTotalPhys / (1024 * 1024)) : 0;
}

bool memory_low() {
  static HANDLE h = CreateMemoryResourceNotification(LowMemoryResourceNotification);
  BOOL low = FALSE;
  return h && QueryMemoryResourceNotification(h, &low) && low;
}

bool login() { return reg_has(run_key, L"LexiPeek"); }

void set_login(bool on) {
  if (on) reg_set(run_key, L"LexiPeek", L"\"" + program_file() + L"\"");
  else reg_remove(run_key, L"LexiPeek");
}

std::string secret(const std::string &provider) {
  PCREDENTIALW c = nullptr;
  if (!CredReadW(target(provider).c_str(), CRED_TYPE_GENERIC, 0, &c)) return "";
  std::string v(reinterpret_cast<const char *>(c->CredentialBlob), c->CredentialBlobSize);
  CredFree(c);
  return v;
}

bool set_secret(const std::string &provider, const std::string &value) {
  auto t = target(provider);
  if (value.empty()) return noted(CredDeleteW(t.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND);
  CREDENTIALW c{};
  c.Type = CRED_TYPE_GENERIC;
  c.TargetName = t.data();
  c.CredentialBlobSize = static_cast<DWORD>(value.size());
  c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(value.data()));
  c.Persist = CRED_PERSIST_LOCAL_MACHINE;
  std::wstring user = L"LexiPeek";
  c.UserName = user.data();
  return noted(CredWriteW(&c, 0));
}

void activate(void *hwnd) {
  auto h = static_cast<HWND>(hwnd);
  // The hotkey's process may take the foreground; AttachThreadInput lets it when another app holds it.
  auto front = GetForegroundWindow();
  auto theirs = GetWindowThreadProcessId(front, nullptr), mine = GetCurrentThreadId();
  if (theirs != mine) AttachThreadInput(mine, theirs, TRUE);
  SetForegroundWindow(h);
  SetFocus(h);
  if (theirs != mine) AttachThreadInput(mine, theirs, FALSE);
}

void round(void *hwnd) {
  auto pref = DWMWCP_ROUND;
  DwmSetWindowAttribute(static_cast<HWND>(hwnd), DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);
}

std::vector<unsigned char> glyph_mask(const std::string &family, char16_t code, int px) {
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = px, bi.bmiHeader.biHeight = -px, bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32;
  void *bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bmp) return DeleteDC(dc), std::vector<unsigned char>();
  auto old = SelectObject(dc, bmp);
  auto name = wide(family);
  HFONT font = CreateFontW(-px * 4 / 5, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, name.c_str());
  auto old_font = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  RECT r{0, 0, px, px};
  wchar_t ch[2] = {static_cast<wchar_t>(code), 0};
  DrawTextW(dc, ch, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  GdiFlush();
  // White on black: its brightness is the mask.
  std::vector<unsigned char> out(static_cast<size_t>(px) * px);
  auto *src = static_cast<const unsigned char *>(bits);
  for (size_t i = 0; i < out.size(); ++i) out[i] = src[i * 4 + 1];
  SelectObject(dc, old_font), DeleteObject(font);
  SelectObject(dc, old), DeleteObject(bmp), DeleteDC(dc);
  return out;
}

std::string icon_font() {
  static const std::string found = [] {
    HDC dc = GetDC(nullptr);
    std::string f;
    for (auto name : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
      LOGFONTW lf{};
      lf.lfCharSet = DEFAULT_CHARSET;
      wcsncpy_s(lf.lfFaceName, name, _TRUNCATE);
      bool has = false;
      EnumFontFamiliesExW(dc, &lf, [](const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM p) { return *reinterpret_cast<bool *>(p) = true, 0; },
                          reinterpret_cast<LPARAM>(&has), 0);
      if (has) {
        f = utf8(name);
        break;
      }
    }
    ReleaseDC(nullptr, dc);
    return f;
  }();
  return found;
}

bool taskbar_light() {
  DWORD v = 0, size = sizeof v;
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme",
               RRF_RT_REG_DWORD, nullptr, &v, &size);
  return v != 0;
}

std::string ui_language() {
  ULONG n = 0, size = 0;
  if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &n, nullptr, &size) || !size) return "en";
  std::wstring buf(size, L'\0');
  GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &n, buf.data(), &size);
  return utf8(buf.c_str()).substr(0, 2);
}

}  // namespace lexipeek::os
