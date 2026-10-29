// LexiPeek for Windows: WinUI 3 (Windows App SDK, C++/WinRT) over the C++ core, as mac/ is SwiftUI over it (qt/ stays for
// Linux and as Windows' fallback build). The process as qt/main.cpp: one instance per user, lexipeek:// links, the per-user
// install, every way out through app::leave. Build and what ships: scripts\build-win.cmd.
// WinUI needs a signed-in desktop: over SSH it fails at start (E_ACCESSDENIED); run it there by a scheduled task with an
// interactive token, as scripts/win-e2e.ps1. Over SSH only --install, --uninstall --yes, --self-test, --probe-selection work.
// Arguments: --data <dir> (else LEXIPEEK_DATA, else the default), lexipeek://… links, --install, --uninstall [--yes],
// --self-test <file> (the OS parts, no window), --probe-selection <file>, --snapshots <dir> (win/snapshots.hpp).
#include "pch.h"
#include "App.xaml.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "lexipeek/app.hpp"
#include "lexipeek/os.hpp"
#include "lexipeek/packs.hpp"
#include "shell.hpp"
#include "snapshots.hpp"
#include "ui.hpp"
#include "version.h"

int __stdcall wXamlGeneratedMain();  // App.xaml.g.hpp's: Application::Start with this App

using namespace lexipeek;
using winrt::LexiPeek::implementation::App;

namespace {

int exit_code = 0;
std::vector<std::string> arguments;  // this launch's, without the program
std::string data;
enum class Ask { none, install, uninstall } asking = Ask::none;
win::CoreClient *client = nullptr;  // both live until the process leaves
win::Shell *shell = nullptr;

bool has(std::string_view a) { return std::find(arguments.begin(), arguments.end(), a) != arguments.end(); }

std::string opt(std::string_view name) {
  for (size_t i = 0; i + 1 < arguments.size(); ++i)
    if (arguments[i] == name) return arguments[i + 1];
  return "";
}

std::filesystem::path path(const std::string &s) { return std::filesystem::path(winrt::to_hstring(s).c_str()); }

// `--self-test <file>`: the shared OS layer's checks, then what this machine is; the exit code the failures' count.
int self_test(const std::string &file) {
  std::ofstream out(path(file), std::ios::binary);
  if (!out) return 1;
  int failed = os::self_test(out);
  out << "info front app: " << os::front_app() << ", selection: " << (os::selection() ? "some" : "none") << "\n";
  out << "info data dir: " << app::data_dir() << ", version " << LEXIPEEK_VERSION << ", cpu " << cpu_arch();
  for (auto &x : cpu_features()) out << " " << x;
  out << "\n";
  return failed;
}

// The tray app on its data dir: a second launch's arguments come to it, this launch's lexipeek:// link opens.
void run() {
  client = new win::CoreClient(data, app::device(os::ram_mb(), cpu_features(), cpu_arch()));
  if (auto dir = opt("--snapshots"); !dir.empty()) {
    shell = new win::Shell(*client, false);
    return win::snapshots(*shell, dir, [](int failed) { shell->quit(failed); });
  }
  shell = new win::Shell(*client, true);
  os::receive([](const std::vector<std::string> &a) {
    if (std::find(a.begin(), a.end(), "--quit") != a.end()) return shell->quit();  // a newer copy installed, or uninstalled
    shell->handle(a);
  });
  os::register_scheme();
  if (app::first_run_at_launch(data, true)) shell->open_first_run();
  if (std::any_of(arguments.begin(), arguments.end(), [](const std::string &a) { return _strnicmp(a.c_str(), "lexipeek:", 9) == 0; })) shell->handle(arguments);
}

// Started by itself from elsewhere (a download): installed for this user and started from there, or run as it is.
void install_or_run(winrt::Microsoft::UI::Xaml::ElementTheme theme) {
  win::present(win::ask(app::text("inst_q_t"), app::text("inst_q_d"), app::text("inst_btn"), app::text("inst_run"), theme, [theme](bool yes) {
    auto as_it_is = [] {
      if (os::hand_over(arguments)) return App::quit(0);
      run();
    };
    if (!yes) return as_it_is();
    os::quit_running();  // an older copy running gives way
    auto e = os::install(LEXIPEEK_VERSION, true);
    if (e.empty()) return App::quit(os::start(os::installed_program()) ? 0 : 1);
    win::present(win::ask(e, "", app::text("m_close"), "", theme, [as_it_is](bool) { as_it_is(); }));
  }));
}

}  // namespace

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int n = 0;
  auto **argv = CommandLineToArgvW(GetCommandLineW(), &n);
  for (int i = 1; i < n; ++i) arguments.push_back(winrt::to_string(argv[i]));
  LocalFree(argv);
  data = opt("--data");
  wchar_t env[MAX_PATH]{};
  if (data.empty() && GetEnvironmentVariableW(L"LEXIPEEK_DATA", env, MAX_PATH)) data = winrt::to_string(env);
  set_ui_platform("windows");
  if (auto r = os::cli_runner()) ai::set_cli_runner(r);
  app::set_language(data, os::ui_language());
  // Settings → Apps → LexiPeek → Uninstall (--yes: without asking): a running copy quits first.
  if (has("--uninstall")) {
    if (!has("--yes")) asking = Ask::uninstall;
    else os::quit_running(), os::uninstall(), app::leave(0);
  } else if (has("--install")) {
    os::quit_running();
    auto e = os::install(LEXIPEEK_VERSION, true);
    std::fputs(((e.empty() ? os::installed_program() : e) + "\n").c_str(), stderr);
    app::leave(e.empty() ? 0 : 1);
  } else if (auto f = opt("--self-test"); !f.empty()) {
    app::leave(self_test(f));
  } else if (auto p = opt("--probe-selection"); !p.empty()) {
    // What UI Automation sees at the focus, after 3 s (time to focus the app to look at).
    Sleep(3000);
    std::ofstream(path(p), std::ios::binary) << os::probe_selection() << "front app: " << os::front_app() << "\n";
    app::leave(0);
  } else if (!has("--snapshots")) {
    if (arguments.empty() && !os::installed_here()) asking = Ask::install;
    else if (os::hand_over(arguments)) app::leave(0);  // one LexiPeek per user: a second launch hands its arguments over
  }
  os::remove_old();  // left by an update
  wXamlGeneratedMain();
  app::leave(exit_code);
}

namespace winrt::LexiPeek::implementation {

App::App() {
  UnhandledException([](auto &&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const &e) {
    OutputDebugStringW((L"LexiPeek: " + e.Message() + L"\n").c_str());
  });
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &) {
  DispatcherShutdownMode(Microsoft::UI::Xaml::DispatcherShutdownMode::OnExplicitShutdown);  // a tray app: no window is no reason to end
  auto theme = win::theme(app::settings(data).theme);
  if (asking == Ask::uninstall)
    return win::present(win::ask(app::text("uninst_q", app::data_dir()), "", app::text("delete"), app::text("cancel"), theme, [](bool yes) {
      if (yes) os::quit_running(), os::uninstall();
      quit(0);
    }));
  if (asking == Ask::install) return install_or_run(theme);
  run();
}

void App::quit(int code) {
  exit_code = code;
  Microsoft::UI::Xaml::Application::Current().Exit();
}

}  // namespace winrt::LexiPeek::implementation
