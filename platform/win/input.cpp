// Hotkeys, the selected text (UI Automation, a classic Edit), the focused app, input anywhere.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <future>
#include <map>
#include <thread>
#include "lexipeek/os.hpp"
#include "win.hpp"
// clang-format off
#include <objbase.h>
#include <uiautomation.h>
// clang-format on

namespace lexipeek::os {

using namespace win;

namespace {

std::map<int, std::function<void()>> &fired() {
  static std::map<int, std::function<void()>> m;
  return m;
}

UINT key_code(const std::string &k) {
  if (k == "Esc") return VK_ESCAPE;
  if (k.size() == 1 && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9'))) return static_cast<UINT>(k[0]);
  return 0;
}

Rect unite(const Rect &a, const Rect &b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  int l = std::min(a.x, b.x), t = std::min(a.y, b.y), r = std::max(a.x + a.w, b.x + b.w), btm = std::max(a.y + a.h, b.y + b.h);
  return {l, t, r - l, btm - t};
}

// A classic Edit or RichEdit control with the focus (Notepad and many Win32 apps: UI Automation gives no text there):
// its selection by EM_GETSEL and its text by WM_GETTEXT, never the clipboard.
std::optional<Selected> read_edit() {
  GUITHREADINFO gui{};
  gui.cbSize = sizeof gui;
  if (!GetGUIThreadInfo(GetWindowThreadProcessId(GetForegroundWindow(), nullptr), &gui) || !gui.hwndFocus) return std::nullopt;
  wchar_t cls[64];
  GetClassNameW(gui.hwndFocus, cls, 64);
  auto name = lower(cls);
  bool rich = name.starts_with(L"richedit");
  if (name != L"edit" && !rich) return std::nullopt;
  DWORD_PTR start = 0, end = 0, length = 0;
  SendMessageTimeoutW(gui.hwndFocus, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end), SMTO_ABORTIFHUNG, 500, nullptr);
  if (end <= start) return std::nullopt;
  SendMessageTimeoutW(gui.hwndFocus, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG, 500, &length);
  if (length == 0 || length > 4'000'000) return std::nullopt;
  std::wstring text(length + 1, L'\0');
  DWORD_PTR got = 0;
  SendMessageTimeoutW(gui.hwndFocus, WM_GETTEXT, length + 1, reinterpret_cast<LPARAM>(text.data()), SMTO_ABORTIFHUNG, 500, &got);
  text.resize(got);
  // RichEdit counts a paragraph break as one character, its WM_GETTEXT gives CRLF: one character per break here too.
  if (rich)
    for (size_t i = text.find(L"\r\n"); i != std::wstring::npos; i = text.find(L"\r\n", i)) text.erase(i, 1);
  if (end > text.size()) return std::nullopt;
  Selected s;
  s.text = utf8(text.c_str() + start, static_cast<int>(end - start));
  if (s.text.empty()) return std::nullopt;
  // The caret's line as the anchor (the selection's own rectangle is not asked for).
  RECT caret = gui.rcCaret;
  POINT a{caret.left, caret.top}, b{caret.right, caret.bottom};
  ClientToScreen(gui.hwndCaret ? gui.hwndCaret : gui.hwndFocus, &a);
  ClientToScreen(gui.hwndCaret ? gui.hwndCaret : gui.hwndFocus, &b);
  if (gui.hwndCaret) s.bounds = {a.x, a.y, b.x - a.x + 1, b.y - a.y + 1};
  return s;
}

// The selection's text and rectangle by UI Automation: the focused element's TextPattern (a thread of its own: COM
// MTA, and a hung app cannot hang the shell).
std::optional<Selected> read_selection() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::optional<Selected> out;
  IUIAutomation *uia = nullptr;
  // CUIAutomation8: the newer client, with the proxies of Win32 controls.
  if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                 reinterpret_cast<void **>(&uia))) ||
      SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
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
                  Rect all;
                  for (LONG i = 0; i + 3 <= hi; i += 4)
                    all = unite(all, {static_cast<int>(d[i]), static_cast<int>(d[i + 1]), static_cast<int>(d[i + 2]), static_cast<int>(d[i + 3])});
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
  if (!out) out = read_edit();
  return out;
}

std::string hex(HRESULT h) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%x", static_cast<unsigned>(h));
  return buf;
}

}  // namespace

bool register_hotkey(int id, const Hotkey &h, std::function<void()> fire) {
  // A copy: the handler may unregister its own hotkey (Esc closes the card), which destroys the map's one.
  on_hotkey([](int i) {
    if (auto it = fired().find(i); it != fired().end()) std::function<void()>(it->second)();
  });
  unregister_hotkey(id);
  UINT mods = MOD_NOREPEAT | (h.option ? MOD_ALT : 0) | (h.control ? MOD_CONTROL : 0) | (h.shift ? MOD_SHIFT : 0) |
              (h.command ? MOD_WIN : 0);
  auto vk = key_code(h.key);
  if (!vk || !noted(RegisterHotKey(hidden(), id, mods, vk))) return false;
  fired()[id] = std::move(fire);
  return true;
}

void unregister_hotkey(int id) {
  if (fired().erase(id)) UnregisterHotKey(hidden(), id);
}

std::string probe_selection() {
  std::string out;
  auto hr = [&](const char *what, HRESULT h) { out += std::string(what) + " hr=0x" + hex(h) + "\n"; };
  auto bstr = [](BSTR b) {
    std::string s = b ? utf8(b, static_cast<int>(SysStringLen(b))) : "";
    SysFreeString(b);
    return s;
  };
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IUIAutomation *uia = nullptr;
  HRESULT h = CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), reinterpret_cast<void **>(&uia));
  hr("CoCreateInstance CUIAutomation8", h);
  if (!uia) return out;
  IUIAutomationElement *el = nullptr;
  hr("GetFocusedElement", uia->GetFocusedElement(&el));
  for (int depth = 0; el && depth < 4; ++depth) {
    BSTR name = nullptr, cls = nullptr, fw = nullptr;
    CONTROLTYPEID type = 0;
    el->get_CurrentName(&name), el->get_CurrentClassName(&cls), el->get_CurrentFrameworkId(&fw), el->get_CurrentControlType(&type);
    out += "element[" + std::to_string(depth) + "] class=" + bstr(cls) + " type=" + std::to_string(type) + " fw=" + bstr(fw) +
           " name=" + bstr(name).substr(0, 60) + "\n";
    IUIAutomationTextPattern *tp = nullptr;
    h = el->GetCurrentPatternAs(UIA_TextPatternId, __uuidof(IUIAutomationTextPattern), reinterpret_cast<void **>(&tp));
    hr("  TextPattern", h);
    if (tp) {
      IUIAutomationTextRangeArray *ranges = nullptr;
      hr("  GetSelection", tp->GetSelection(&ranges));
      int n = 0;
      if (ranges) ranges->get_Length(&n);
      out += "  ranges=" + std::to_string(n) + "\n";
      IUIAutomationTextRange *r = nullptr;
      if (n > 0 && SUCCEEDED(ranges->GetElement(0, &r)) && r) {
        BSTR text = nullptr;
        hr("  GetText", r->GetText(200, &text));
        out += "  text=" + bstr(text) + "\n";
        r->Release();
      }
      if (ranges) ranges->Release();
      tp->Release();
    }
    IUIAutomationTreeWalker *walker = nullptr;
    uia->get_ControlViewWalker(&walker);
    IUIAutomationElement *parent = nullptr;
    if (walker) walker->GetParentElement(el, &parent), walker->Release();
    el->Release();
    el = parent;
  }
  if (el) el->Release();
  uia->Release();
  CoUninitialize();
  auto e = read_edit();
  out += "edit fallback: " + (e ? e->text : std::string("none")) + "\n";
  return out;
}

std::optional<Selected> selection() {
  auto f = std::async(std::launch::async, read_selection);
  if (f.wait_for(std::chrono::milliseconds(1500)) != std::future_status::ready) {
    std::thread([f = std::move(f)]() mutable { f.wait(); }).detach();
    return std::nullopt;
  }
  return f.get();
}

std::string front_app() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return "";
  wchar_t buf[MAX_PATH];
  DWORD n = MAX_PATH;
  std::wstring out;
  if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
    out.assign(buf, n);
    out = out.substr(out.find_last_of(L"\\/") + 1);
  }
  CloseHandle(p);
  return utf8(lower(out));
}

bool mouse_down() { return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000); }

namespace {
std::function<void(Input)> watching;
HHOOK mouse_hook = nullptr, key_hook = nullptr;

// The hooks only note what happened: the shell acts on it after the system's call returns.
void note(Input i) {
  post([i] {
    if (watching) watching(i);
  });
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
  hidden();  // on this thread: the notes come back here
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

}  // namespace lexipeek::os
