#ifdef _WIN32
#include "platform.hpp"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QWidget>
#include <chrono>
#include <future>
#include <map>
#include <thread>
// clang-format off
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <uiautomation.h>
#include <wincred.h>
// clang-format on

namespace lexipeek::qt::platform {

namespace {

std::map<int, std::function<void()>> &fired() {
  static std::map<int, std::function<void()>> m;
  return m;
}

// WM_HOTKEY comes to the thread's queue (no window): Qt hands it over here.
struct HotkeyFilter : QAbstractNativeEventFilter {
  bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override {
    auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_HOTKEY) return false;
    auto it = fired().find(static_cast<int>(msg->wParam));
    if (it != fired().end()) it->second();
    return true;
  }
};

std::wstring wide(const std::string &s) { return QString::fromStdString(s).toStdWString(); }
std::string utf8(const wchar_t *s, int n = -1) {
  return (n < 0 ? QString::fromWCharArray(s) : QString::fromWCharArray(s, n)).toStdString();
}

UINT key_code(const std::string &k) {
  if (k == "Esc") return VK_ESCAPE;
  if (k.size() == 1 && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9'))) return static_cast<UINT>(k[0]);
  return 0;
}

// The selection's text and rectangle by UI Automation: the focused element's TextPattern (a thread of its own: COM
// MTA, and a hung app cannot hang the shell).
std::optional<Selected> read_selection() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::optional<Selected> out;
  IUIAutomation *uia = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                 reinterpret_cast<void **>(&uia)))) {
    IUIAutomationElement *el = nullptr;
    if (SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
      IUIAutomationTextPattern *tp = nullptr;
      if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TextPatternId, __uuidof(IUIAutomationTextPattern), reinterpret_cast<void **>(&tp))) && tp) {
        IUIAutomationTextRangeArray *ranges = nullptr;
        if (SUCCEEDED(tp->GetSelection(&ranges)) && ranges) {
          int n = 0;
          ranges->get_Length(&n);
          IUIAutomationTextRange *r = nullptr;
          if (n > 0 && SUCCEEDED(ranges->GetElement(0, &r)) && r) {
            BSTR text = nullptr;
            if (SUCCEEDED(r->GetText(4000, &text)) && text) {
              Selected s;
              s.text = utf8(text, static_cast<int>(SysStringLen(text)));
              SysFreeString(text);
              SAFEARRAY *rects = nullptr;
              if (SUCCEEDED(r->GetBoundingRectangles(&rects)) && rects) {
                double *d = nullptr;
                LONG hi = 0;
                SafeArrayGetUBound(rects, 1, &hi);
                if (hi >= 3 && SUCCEEDED(SafeArrayAccessData(rects, reinterpret_cast<void **>(&d)))) {
                  QRect all;
                  for (LONG i = 0; i + 3 <= hi; i += 4)
                    all |= QRect(static_cast<int>(d[i]), static_cast<int>(d[i + 1]), static_cast<int>(d[i + 2]), static_cast<int>(d[i + 3]));
                  s.bounds = all;
                  SafeArrayUnaccessData(rects);
                }
                SafeArrayDestroy(rects);
              }
              if (!s.text.empty()) out = s;
            }
            r->Release();
          }
          ranges->Release();
        }
        tp->Release();
      }
      el->Release();
    }
    uia->Release();
  }
  CoUninitialize();
  return out;
}

// UI Automation gives physical pixels; Qt's windows go by logical ones on the screen there.
QRect logical(const QRect &px) {
  if (px.isEmpty()) return px;
  for (auto *s : QGuiApplication::screens()) {
    auto g = s->geometry();
    auto r = s->devicePixelRatio();
    QRect phys(g.topLeft(), g.size() * r);
    if (phys.contains(px.center())) {
      auto p = g.topLeft() + (px.topLeft() - g.topLeft()) / r;
      return {p, px.size() / r};
    }
  }
  return px;
}

const char *run_key = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring target(const std::string &provider) { return wide("dev.kafeg.lexipeek.ai/" + provider); }

}  // namespace

bool register_hotkey(int id, const Hotkey &h, std::function<void()> fire) {
  static bool installed = false;
  if (!installed) QCoreApplication::instance()->installNativeEventFilter(new HotkeyFilter), installed = true;
  unregister_hotkey(id);
  UINT mods = MOD_NOREPEAT | (h.option ? MOD_ALT : 0) | (h.control ? MOD_CONTROL : 0) | (h.shift ? MOD_SHIFT : 0) |
              (h.command ? MOD_WIN : 0);
  auto vk = key_code(h.key);
  if (!vk || !RegisterHotKey(nullptr, id, mods, vk)) return false;
  fired()[id] = std::move(fire);
  return true;
}

void unregister_hotkey(int id) {
  if (fired().erase(id)) UnregisterHotKey(nullptr, id);
}

std::string hotkey_label(const Hotkey &h) {
  std::string s;
  if (h.control) s += "Ctrl+";
  if (h.option) s += "Alt+";
  if (h.shift) s += "Shift+";
  if (h.command) s += "Win+";
  return s + h.key;
}

std::optional<Selected> selection() {
  auto f = std::async(std::launch::async, read_selection);
  if (f.wait_for(std::chrono::milliseconds(1500)) != std::future_status::ready) {
    std::thread([f = std::move(f)]() mutable { f.wait(); }).detach();
    return std::nullopt;
  }
  auto s = f.get();
  if (s) s->bounds = logical(s->bounds);
  return s;
}

std::string front_app() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return "";
  wchar_t buf[MAX_PATH];
  DWORD n = MAX_PATH;
  std::string out;
  if (QueryFullProcessImageNameW(p, 0, buf, &n)) out = QDir(QString::fromWCharArray(buf, static_cast<int>(n))).dirName().toStdString();
  CloseHandle(p);
  return QString::fromStdString(out).toLower().toStdString();
}

bool mouse_down() { return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000); }

namespace {
std::function<void(Input)> watching;
HHOOK mouse_hook = nullptr, key_hook = nullptr;

// The hooks only note what happened: the shell acts on it after the system's call returns.
void note(Input i) {
  QMetaObject::invokeMethod(QCoreApplication::instance(), [i] {
    if (watching) watching(i);
  }, Qt::QueuedConnection);
}

LRESULT CALLBACK on_mouse(int code, WPARAM w, LPARAM l) {
  if (code == HC_ACTION) {
    if (w == WM_LBUTTONDOWN) note(Input::down);
    else if (w == WM_LBUTTONUP) note(Input::up);
    else if (w == WM_MOUSEWHEEL || w == WM_RBUTTONDOWN) note(Input::other);
  }
  return CallNextHookEx(nullptr, code, w, l);
}

LRESULT CALLBACK on_key(int code, WPARAM w, LPARAM l) {
  if (code == HC_ACTION && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)) note(Input::other);
  return CallNextHookEx(nullptr, code, w, l);
}
}  // namespace

void watch_input(std::function<void(Input)> on) {
  watching = std::move(on);
  if (watching && !mouse_hook) {
    mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, on_mouse, GetModuleHandleW(nullptr), 0);
    key_hook = SetWindowsHookExW(WH_KEYBOARD_LL, on_key, GetModuleHandleW(nullptr), 0);
  } else if (!watching && mouse_hook) {
    UnhookWindowsHookEx(mouse_hook);
    UnhookWindowsHookEx(key_hook);
    mouse_hook = key_hook = nullptr;
  }
}

bool front_is_mine() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}

int ram_mb() {
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof m;
  return GlobalMemoryStatusEx(&m) ? static_cast<int>(m.ullTotalPhys / (1024 * 1024)) : 0;
}

bool login() { return QSettings(run_key, QSettings::NativeFormat).contains("LexiPeek"); }

void set_login(bool on) {
  QSettings s(run_key, QSettings::NativeFormat);
  if (on) s.setValue("LexiPeek", "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\"");
  else s.remove("LexiPeek");
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
  if (value.empty()) return CredDeleteW(t.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
  CREDENTIALW c{};
  c.Type = CRED_TYPE_GENERIC;
  c.TargetName = t.data();
  c.CredentialBlobSize = static_cast<DWORD>(value.size());
  c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(value.data()));
  c.Persist = CRED_PERSIST_LOCAL_MACHINE;
  std::wstring user = L"LexiPeek";
  c.UserName = user.data();
  return CredWriteW(&c, 0);
}

void round(QWidget *w) {
  auto pref = DWMWCP_ROUND;
  DwmSetWindowAttribute(reinterpret_cast<HWND>(w->winId()), DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);
}

}  // namespace lexipeek::qt::platform
#endif
