// CLI AI providers: a process in a job object, the prompt on stdin, its output read.
#include <future>
#include "lexipeek/os.hpp"
#include "win.hpp"

namespace lexipeek::os {

using namespace win;

namespace {

bool is_file(const std::wstring &f) {
  auto a = GetFileAttributesW(f.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// A program by name as the shell finds it: PATH (and the usual install places) with .exe, .cmd, .bat; "" when none.
std::wstring find_program(const std::string &name) {
  auto w = wide(name);
  if (name.find_first_of("\\/") != std::string::npos) return GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES ? w : L"";
  std::vector<std::wstring> path;
  auto all = env(L"PATH");
  for (size_t start = 0; start <= all.size();) {
    auto end = all.find(L';', start);
    if (end == std::wstring::npos) end = all.size();
    if (end > start) path.push_back(all.substr(start, end - start));
    start = end + 1;
  }
  path.push_back(env(L"USERPROFILE") + L"\\.local\\bin");
  path.push_back(env(L"APPDATA") + L"\\npm");
  for (auto &dir : path)
    for (auto ext : {L"", L".exe", L".cmd", L".bat"}) {
      auto f = dir + L"\\" + w + ext;
      if (is_file(f)) return f;
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
  auto low = lower(program);
  if (low.ends_with(L".cmd") || low.ends_with(L".bat")) line = L"cmd.exe /d /s /c \"" + line + L"\"";
  // The environment with the call's own.
  std::wstring environment;
  auto *strings = GetEnvironmentStringsW();
  for (auto *p = strings; *p; p += wcslen(p) + 1) environment += std::wstring(p) + L'\0';
  FreeEnvironmentStringsW(strings);
  for (auto &[k, v] : c.env) environment += wide(k + "=" + v) + L'\0';
  environment += L'\0';
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
  wchar_t tmp[MAX_PATH + 1];
  GetTempPathW(MAX_PATH + 1, tmp);
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
  limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof limit);
  bool ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                           environment.data(), tmp, &si, &pi);
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

}  // namespace lexipeek::os
