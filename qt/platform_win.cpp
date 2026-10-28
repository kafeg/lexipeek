#ifdef _WIN32
#include "platform.hpp"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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

// The error of the last failed call here, kept before anything else can change GetLastError.
DWORD failed_with = 0;
bool noted(bool ok) {
  if (!ok) failed_with = GetLastError();
  return ok;
}

std::wstring wide(const std::string &s) { return QString::fromStdString(s).toStdWString(); }
std::string utf8(const wchar_t *s, int n = -1) {
  return (n < 0 ? QString::fromWCharArray(s) : QString::fromWCharArray(s, n)).toStdString();
}

UINT key_code(const std::string &k) {
  if (k == "Esc") return VK_ESCAPE;
  if (k.size() == 1 && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9'))) return static_cast<UINT>(k[0]);
  return 0;
}

// A classic Edit or RichEdit control with the focus (Notepad and many Win32 apps: UI Automation gives no text there):
// its selection by EM_GETSEL and its text by WM_GETTEXT, never the clipboard.
std::optional<Selected> read_edit() {
  GUITHREADINFO gui{};
  gui.cbSize = sizeof gui;
  if (!GetGUIThreadInfo(GetWindowThreadProcessId(GetForegroundWindow(), nullptr), &gui) || !gui.hwndFocus) return std::nullopt;
  wchar_t cls[64];
  GetClassNameW(gui.hwndFocus, cls, 64);
  auto name = QString::fromWCharArray(cls).toLower();
  if (name != "edit" && !name.startsWith("richedit")) return std::nullopt;
  DWORD_PTR start = 0, end = 0, length = 0;
  SendMessageTimeoutW(gui.hwndFocus, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end), SMTO_ABORTIFHUNG, 500, nullptr);
  if (end <= start) return std::nullopt;
  SendMessageTimeoutW(gui.hwndFocus, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG, 500, &length);
  if (length == 0 || length > 4'000'000) return std::nullopt;
  std::wstring text(length + 1, L'\0');
  DWORD_PTR got = 0;
  SendMessageTimeoutW(gui.hwndFocus, WM_GETTEXT, length + 1, reinterpret_cast<LPARAM>(text.data()), SMTO_ABORTIFHUNG, 500, &got);
  text.resize(got);
  if (end > text.size()) return std::nullopt;
  Selected s;
  s.text = utf8(text.c_str() + start, static_cast<int>(end - start));
  if (s.text.empty()) return std::nullopt;
  // The caret's line as the anchor (the selection's own rectangle is not asked for).
  RECT caret = gui.rcCaret;
  POINT a{caret.left, caret.top}, b{caret.right, caret.bottom};
  ClientToScreen(gui.hwndCaret ? gui.hwndCaret : gui.hwndFocus, &a);
  ClientToScreen(gui.hwndCaret ? gui.hwndCaret : gui.hwndFocus, &b);
  if (gui.hwndCaret) s.bounds = QRect(QPoint(a.x, a.y), QPoint(b.x, b.y));
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
  if (!out) out = read_edit();
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
  if (!vk || !noted(RegisterHotKey(nullptr, id, mods, vk))) return false;
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

std::string probe_selection() {
  std::string out;
  auto hr = [&](const char *what, HRESULT h) { out += std::string(what) + " hr=0x" + QString::number(static_cast<uint>(h), 16).toStdString() + "\n"; };
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

namespace {
const wchar_t *receiver = L"LexiPeek receiver";

// WM_COPYDATA to the hidden receiver window: a second launch's arguments, one per line.
struct Receiver : QWidget {
  std::function<void(const QStringList &)> got;
  bool nativeEvent(const QByteArray &, void *message, qintptr *result) override {
    auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_COPYDATA) return false;
    auto *cd = reinterpret_cast<COPYDATASTRUCT *>(msg->lParam);
    auto args = QString::fromUtf8(static_cast<const char *>(cd->lpData), static_cast<int>(cd->cbData)).split('\n');
    QMetaObject::invokeMethod(this, [this, args] { got(args); }, Qt::QueuedConnection);
    *result = 1;
    return true;
  }
};
}  // namespace

bool hand_over(const QStringList &args) {
  CreateMutexW(nullptr, FALSE, L"Local\\LexiPeek");
  if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
  // The first one may still be starting: its window comes within a few seconds.
  for (int i = 0; i < 50; ++i) {
    if (HWND w = FindWindowW(nullptr, receiver)) {
      auto bytes = args.join('\n').toUtf8();
      COPYDATASTRUCT cd{1, static_cast<DWORD>(bytes.size()), bytes.data()};
      AllowSetForegroundWindow(ASFW_ANY);
      SendMessageW(w, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cd));
      return true;
    }
    Sleep(100);
  }
  return true;
}

void receive(std::function<void(const QStringList &)> got) {
  auto *r = new Receiver;
  r->got = std::move(got);
  r->setWindowTitle(QString::fromWCharArray(receiver));
  r->winId();  // the native window, never shown
}

void register_scheme() {
  QSettings s("HKEY_CURRENT_USER\\Software\\Classes\\lexipeek", QSettings::NativeFormat);
  s.setValue("Default", "URL:LexiPeek");
  s.setValue("URL Protocol", "");
  auto exe = "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\" \"%1\"";
  s.setValue("shell/open/command/Default", exe);
}

namespace {

// A program by name as the shell finds it: PATH (and the usual install places) with .exe, .cmd, .bat; "" when none.
std::wstring find_program(const std::string &name) {
  auto w = wide(name);
  if (name.find_first_of("\\/") != std::string::npos) return GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES ? w : L"";
  auto path = QString::fromLocal8Bit(qgetenv("PATH")).split(';', Qt::SkipEmptyParts);
  auto home = QDir::homePath();
  path << home + "/.local/bin" << QString::fromLocal8Bit(qgetenv("APPDATA")) + "/npm";
  for (auto &dir : path)
    for (auto ext : {"", ".exe", ".cmd", ".bat"}) {
      auto f = QDir(dir).filePath(QString::fromStdString(name) + ext);
      if (QFileInfo(f).isFile()) return QDir::toNativeSeparators(f).toStdWString();
    }
  return L"";
}

// One argument on a command line as the C runtime splits it.
std::wstring quote(const std::wstring &a) {
  if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
  std::wstring out = L"\"";
  size_t slashes = 0;
  for (auto ch : a) {
    if (ch == L'\\') ++slashes;
    else if (ch == L'"') out.append(slashes * 2 + 1, L'\\'), slashes = 0;
    else slashes = 0;
    out += ch;
  }
  out.append(slashes, L'\\');
  return out + L"\"";
}

std::string read_all(HANDLE h) {
  std::string out;
  char buf[4096];
  DWORD n = 0;
  while (ReadFile(h, buf, sizeof buf, &n, nullptr) && n > 0) out.append(buf, n);
  return out;
}

// The program with the prompt on stdin, stdout and stderr read, the whole tree stopped after cli_timeout.
ai::CliRan run_cli(const ai::CliCall &c) {
  ai::CliRan r;
  auto program = find_program(c.program);
  if (program.empty()) return r.code = 127, r.err = c.program + ": not found", r;
  std::wstring line = quote(program);
  for (auto &a : c.args) line += L" " + quote(wide(a));
  if (!c.stdin_arg.empty()) line += L" " + quote(wide(c.stdin_arg));
  auto lower = QString::fromStdWString(program).toLower();
  if (lower.endsWith(".cmd") || lower.endsWith(".bat")) line = L"cmd.exe /d /s /c \"" + line + L"\"";
  // The environment with the call's own.
  std::wstring env;
  auto *strings = GetEnvironmentStringsW();
  for (auto *p = strings; *p; p += wcslen(p) + 1) env += std::wstring(p) + L'\0';
  FreeEnvironmentStringsW(strings);
  for (auto &[k, v] : c.env) env += wide(k + "=" + v) + L'\0';
  env += L'\0';
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE in_r, in_w, out_r, out_w, err_r, err_w;
  CreatePipe(&in_r, &in_w, &sa, 0);
  CreatePipe(&out_r, &out_w, &sa, 0);
  CreatePipe(&err_r, &err_w, &sa, 0);
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_r, si.hStdOutput = out_w, si.hStdError = err_w;
  PROCESS_INFORMATION pi{};
  auto tmp = QDir::toNativeSeparators(QDir::tempPath()).toStdWString();
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
  limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof limit);
  bool ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                           env.data(), tmp.c_str(), &si, &pi);
  CloseHandle(in_r), CloseHandle(out_w), CloseHandle(err_w);
  if (!ok) {
    CloseHandle(in_w), CloseHandle(out_r), CloseHandle(err_r), CloseHandle(job);
    return r.code = 127, r.err = c.program + ": cannot start", r;
  }
  AssignProcessToJobObject(job, pi.hProcess);
  ResumeThread(pi.hThread);
  auto out = std::async(std::launch::async, read_all, out_r);
  auto err = std::async(std::launch::async, read_all, err_r);
  DWORD n = 0;
  WriteFile(in_w, c.prompt.data(), static_cast<DWORD>(c.prompt.size()), &n, nullptr);
  CloseHandle(in_w);
  if (WaitForSingleObject(pi.hProcess, ai::cli_timeout * 1000) == WAIT_TIMEOUT) {
    TerminateJobObject(job, 143);
    WaitForSingleObject(pi.hProcess, 5000);
    r.code = 143;
  } else {
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    r.code = static_cast<int>(code);
  }
  CloseHandle(job);  // kills what the program left running
  r.out = out.get(), r.err = err.get();
  CloseHandle(out_r), CloseHandle(err_r), CloseHandle(pi.hProcess), CloseHandle(pi.hThread);
  // cmd.exe says "not recognized" with 9009.
  if (r.code == 9009) r.code = 127;
  return r;
}

}  // namespace

std::function<ai::CliRan(const ai::CliCall &)> cli_runner() { return run_cli; }

std::string last_error() {
  auto code = failed_with;
  if (!code) return "";
  wchar_t buf[512];
  auto n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, buf, 512, nullptr);
  return std::to_string(code) + " " + QString::fromWCharArray(buf, static_cast<int>(n)).trimmed().toStdString();
}

void round(QWidget *w) {
  auto pref = DWMWCP_ROUND;
  DwmSetWindowAttribute(reinterpret_cast<HWND>(w->winId()), DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref);
}

}  // namespace lexipeek::qt::platform
#endif
