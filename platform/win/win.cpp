#include "win.hpp"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <deque>
#include <mutex>
#include "lexipeek/os.hpp"

namespace lexipeek::os::win {

namespace {
DWORD failed_with = 0;
std::function<void(int)> hotkey_handler;
std::function<void(const std::string &)> copydata_handler;
std::mutex posted_mutex;
std::deque<std::function<void()>> posted;
constexpr UINT run_posted = WM_APP + 1;

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_HOTKEY) {
    if (hotkey_handler) hotkey_handler(static_cast<int>(w));
    return 0;
  }
  if (m == WM_COPYDATA) {
    // Handled after the sender's call returns: it waits for this one.
    auto *cd = reinterpret_cast<COPYDATASTRUCT *>(l);
    std::string args(static_cast<const char *>(cd->lpData), cd->cbData);
    post([args] {
      if (copydata_handler) copydata_handler(args);
    });
    return 1;
  }
  if (m == run_posted) {
    for (;;) {
      std::function<void()> f;
      {
        std::lock_guard lock(posted_mutex);
        if (posted.empty()) break;
        f = std::move(posted.front());
        posted.pop_front();
      }
      f();
    }
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

std::wstring wide(const std::string &s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

std::string utf8(const wchar_t *s, int n) {
  if (!s) return {};
  if (n < 0) n = static_cast<int>(wcslen(s));
  if (n == 0) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, s, n, nullptr, 0, nullptr, nullptr);
  std::string out(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s, n, out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return s;
}

bool noted(bool ok) {
  if (!ok) failed_with = GetLastError();
  return ok;
}

std::wstring program_file() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    auto n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n < buf.size()) return buf.resize(n), buf;
    buf.resize(buf.size() * 2);
  }
}

std::wstring program_dir() {
  auto f = program_file();
  return f.substr(0, f.find_last_of(L"\\/"));
}

std::wstring env(const wchar_t *name) {
  auto n = GetEnvironmentVariableW(name, nullptr, 0);
  if (!n) return {};
  std::wstring v(n, L'\0');
  v.resize(GetEnvironmentVariableW(name, v.data(), n));
  return v;
}

HWND hidden() {
  static HWND h = [] {
    WNDCLASSW c{};
    c.lpfnWndProc = proc;
    c.hInstance = GetModuleHandleW(nullptr);
    c.lpszClassName = L"LexiPeekHidden";
    RegisterClassW(&c);
    // Top-level (FindWindow finds it by its title, set once launches are received), never shown.
    return CreateWindowExW(WS_EX_TOOLWINDOW, c.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, c.hInstance, nullptr);
  }();
  return h;
}

void post(std::function<void()> f) {
  {
    std::lock_guard lock(posted_mutex);
    posted.push_back(std::move(f));
  }
  PostMessageW(hidden(), run_posted, 0, 0);
}

void on_hotkey(std::function<void(int)> f) { hotkey_handler = std::move(f); }
void on_copydata(std::function<void(const std::string &)> f) { copydata_handler = std::move(f); }

std::wstring reg_get(const std::wstring &path, const std::wstring &name) {
  DWORD size = 0;
  auto *n = name.empty() ? nullptr : name.c_str();
  if (RegGetValueW(HKEY_CURRENT_USER, path.c_str(), n, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) return {};
  std::wstring v(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(HKEY_CURRENT_USER, path.c_str(), n, RRF_RT_REG_SZ, nullptr, v.data(), &size) != ERROR_SUCCESS) return {};
  v.resize(wcslen(v.c_str()));
  return v;
}

bool reg_has(const std::wstring &path, const std::wstring &name) {
  return RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name.empty() ? nullptr : name.c_str(), RRF_RT_ANY, nullptr, nullptr, nullptr) ==
         ERROR_SUCCESS;
}

void reg_set(const std::wstring &path, const std::wstring &name, const std::wstring &value) {
  RegSetKeyValueW(HKEY_CURRENT_USER, path.c_str(), name.empty() ? nullptr : name.c_str(), REG_SZ, value.c_str(),
                  static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void reg_set(const std::wstring &path, const std::wstring &name, DWORD value) {
  RegSetKeyValueW(HKEY_CURRENT_USER, path.c_str(), name.c_str(), REG_DWORD, &value, sizeof value);
}

void reg_remove(const std::wstring &path, const std::wstring &name) { RegDeleteKeyValueW(HKEY_CURRENT_USER, path.c_str(), name.c_str()); }

void reg_remove_key(const std::wstring &path) { RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()), RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str()); }

}  // namespace lexipeek::os::win

namespace lexipeek::os {

std::string last_error() {
  auto code = win::failed_with;
  if (!code) return "";
  wchar_t buf[512];
  auto n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, buf, 512, nullptr);
  auto text = win::utf8(buf, static_cast<int>(n));
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
  return std::to_string(code) + " " + text;
}

}  // namespace lexipeek::os
