// One app per user, the per-user install and uninstall, lexipeek:// links.
#include <filesystem>
#include <system_error>
#include "lexipeek/app.hpp"
#include "lexipeek/os.hpp"
#include "win.hpp"
// clang-format off
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
// clang-format on

namespace fs = std::filesystem;

namespace lexipeek::os {

using namespace win;

namespace {
const wchar_t *receiver = L"LexiPeek receiver";
const std::wstring run_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const std::wstring uninstall_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\LexiPeek";
const std::wstring scheme_key = L"Software\\Classes\\lexipeek";

std::wstring install_dir() { return env(L"LOCALAPPDATA") + L"\\Programs\\LexiPeek"; }
std::wstring shortcut_path() { return env(L"APPDATA") + L"\\Microsoft\\Windows\\Start Menu\\Programs\\LexiPeek.lnk"; }

std::string join(const std::vector<std::string> &args) {
  std::string out;
  for (size_t i = 0; i < args.size(); ++i) out += (i ? "\n" : "") + args[i];
  return out;
}

void send(HWND w, const std::string &bytes) {
  COPYDATASTRUCT cd{1, static_cast<DWORD>(bytes.size()), const_cast<char *>(bytes.data())};
  DWORD_PTR r = 0;
  SendMessageTimeoutW(w, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cd), SMTO_ABORTIFHUNG, 3000, &r);
}

// A file in use cannot be replaced, but it can be renamed: a running copy goes on from the old name.
std::wstring old_name(const fs::path &f) {
  return f.filename() == L"LexiPeek.exe" ? (f.parent_path() / L"LexiPeek.old.exe").wstring() : f.wstring() + L".old";
}

bool put(const fs::path &from, const fs::path &to) {
  if (fs::exists(to) && !DeleteFileW(to.c_str())) MoveFileExW(to.c_str(), old_name(to).c_str(), MOVEFILE_REPLACE_EXISTING);
  if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) return false;
  DeleteFileW((to.wstring() + L":Zone.Identifier").c_str());  // the download's "from the Internet" mark: the installed copy is trusted
  return true;
}
}  // namespace

bool hand_over(const std::vector<std::string> &args) {
  // Another one running: the mutex exists (access denied: it runs at a higher integrity level, still ours).
  bool made = CreateMutexW(nullptr, FALSE, L"Local\\LexiPeek") != nullptr;
  auto e = GetLastError();
  if (made && e != ERROR_ALREADY_EXISTS) return false;
  if (!made && e != ERROR_ACCESS_DENIED) return false;
  // The first one may still be starting: its window comes within a few seconds.
  for (int i = 0; i < 50; ++i) {
    if (HWND w = FindWindowW(nullptr, receiver)) {
      AllowSetForegroundWindow(ASFW_ANY);
      send(w, join(args));
      return true;
    }
    Sleep(100);
  }
  return false;  // no answer: this launch runs as itself
}

void quit_running() {
  // Only a running one's window: no mutex made here (this process must not become "the one running").
  if (HWND w = FindWindowW(nullptr, receiver)) {
    send(w, "--quit");
    for (int i = 0; i < 30 && FindWindowW(nullptr, receiver); ++i) Sleep(100);
  }
}

void receive(std::function<void(const std::vector<std::string> &)> got) {
  on_copydata([got](const std::string &bytes) {
    std::vector<std::string> args;
    for (size_t start = 0;;) {
      auto end = bytes.find('\n', start);
      args.push_back(bytes.substr(start, end - start));
      if (end == std::string::npos) break;
      start = end + 1;
    }
    got(args);
  });
  // Found by its title from now on; a launch at a lower integrity level (a link from the browser or Explorer) may still
  // send its arguments.
  SetWindowTextW(hidden(), receiver);
  ChangeWindowMessageFilterEx(hidden(), WM_COPYDATA, MSGFLT_ALLOW, nullptr);
}

bool installed_here() {
  std::error_code ec;
  return fs::exists(install_dir(), ec) && fs::equivalent(program_dir(), install_dir(), ec);
}

std::string installed_program() { return utf8(install_dir() + L"\\LexiPeek.exe"); }

std::string install(const std::string &version, bool folder) {
  if (installed_here()) return "";  // this copy is the installed one
  fs::path dir = install_dir(), exe = dir / L"LexiPeek.exe";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (!fs::is_directory(dir, ec)) return app::text("err_write", utf8(dir.wstring()));
  std::uintmax_t size = 0;
  if (folder) {
    fs::path from = program_dir();
    for (auto &e : fs::recursive_directory_iterator(from, ec)) {
      auto to = dir / fs::relative(e.path(), from, ec);
      if (e.is_directory()) {
        fs::create_directories(to, ec);
        continue;
      }
      if (!put(e.path(), to)) return app::text("err_write", utf8(to.wstring()));
      size += e.file_size(ec);
    }
  } else {
    if (!put(program_file(), exe)) return app::text("err_write", utf8(exe.wstring()));
    size = fs::file_size(exe, ec);
  }
  if (login()) reg_set(run_key, L"LexiPeek", L"\"" + exe.wstring() + L"\"");
  // The Start menu's shortcut (the shell's own link object).
  auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IShellLinkW *link = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
    link->SetPath(exe.c_str());
    link->SetWorkingDirectory(dir.c_str());
    link->SetDescription(L"LexiPeek");
    IPersistFile *file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
      file->Save(shortcut_path().c_str(), TRUE);
      file->Release();
    }
    link->Release();
  }
  if (SUCCEEDED(com)) CoUninitialize();
  reg_set(uninstall_key, L"DisplayName", L"LexiPeek");
  reg_set(uninstall_key, L"DisplayVersion", wide(version));
  reg_set(uninstall_key, L"Publisher", L"LexiPeek");
  reg_set(uninstall_key, L"DisplayIcon", exe.wstring());
  reg_set(uninstall_key, L"InstallLocation", dir.wstring());
  reg_set(uninstall_key, L"UninstallString", L"\"" + exe.wstring() + L"\" --uninstall");
  reg_set(uninstall_key, L"NoModify", DWORD{1});
  reg_set(uninstall_key, L"NoRepair", DWORD{1});
  reg_set(uninstall_key, L"EstimatedSize", static_cast<DWORD>(size / 1024));
  return "";
}

void uninstall() {
  DeleteFileW(shortcut_path().c_str());
  reg_remove_key(uninstall_key);
  reg_remove(run_key, L"LexiPeek");
  reg_remove_key(scheme_key);
  // The folder (this program in it) once this process has ended.
  auto cmd = L"cmd.exe /c ping -n 4 127.0.0.1 >nul & rmdir /s /q \"" + install_dir() + L"\"";
  wchar_t temp[MAX_PATH + 1];
  GetTempPathW(MAX_PATH + 1, temp);  // never inside the folder it removes
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, temp, &si, &pi))
    CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
}

void remove_old() {
  if (!installed_here()) return;
  std::error_code ec;
  for (auto &e : fs::recursive_directory_iterator(install_dir(), ec))
    if (e.is_regular_file(ec) && (e.path().extension() == L".old" || e.path().filename() == L"LexiPeek.old.exe")) DeleteFileW(e.path().c_str());
}

bool start(const std::string &program) {
  auto exe = wide(program);
  auto line = L"\"" + exe + L"\"";
  auto dir = exe.substr(0, exe.find_last_of(L"\\/"));
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!noted(CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))) return false;
  CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
  return true;
}

void register_scheme() {
  reg_set(scheme_key, L"", L"URL:LexiPeek");
  reg_set(scheme_key, L"URL Protocol", L"");
  reg_set(scheme_key + L"\\shell\\open\\command", L"", L"\"" + program_file() + L"\" \"%1\"");
}

std::string scheme_command() { return utf8(reg_get(scheme_key + L"\\shell\\open\\command", L"")); }

}  // namespace lexipeek::os
