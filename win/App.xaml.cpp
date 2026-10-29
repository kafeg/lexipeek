// LexiPeek for Windows: WinUI 3 (Windows App SDK, C++/WinRT) over the C++ core, as mac/ is SwiftUI over it (qt/ stays for
// Linux). A prototype of the build and runtime model; the shell itself comes later.
// Build: scripts\build-win.cmd [dir] — CMake builds the core and writes <dir>/win/lexipeek-core.props (cmake/win_shell.cmake:
// include dir, the core's whole link closure, cmake/vulkan_win.cpp); NuGet restores packages.config (only the SDK's parts
// used: Base, Foundation, InteractiveExperiences, WinUI); MSBuild builds LexiPeek.vcxproj into <dir>/win/bin. Static CRT
// (/MT) like the core: the Windows App SDK's DLLs carry their own. VS Build Tools lack VS's WindowsXaml C++ targets: the
// project puts the XAML compiler's two passes into the C++ build itself (its last PropertyGroup and target).
// Runs unpackaged and self-contained from any folder: <dir>/win/bin without *.pdb *.lib *.exp *.winmd (~107 MB, ~38 MB zipped;
// the locale folders' .mui are needed). WinUI needs a signed-in desktop: over SSH it fails at start (E_ACCESSDENIED);
// run it there by a scheduled task with an interactive token, as scripts/win-e2e.ps1.
// Arguments: --data <dir> (else LEXIPEEK_DATA, else the default), --look <text> (default "Haus"), --self-test <dir>: once
// the window is drawn, <dir>/self-test.txt (its lines, the time to the first frame) and <dir>/window.png (RenderTargetBitmap),
// then exit 0.
#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "lexipeek/app.hpp"

int __stdcall wXamlGeneratedMain();  // App.xaml.g.hpp's: Application::Start with this App

namespace {
int exit_code = 0;
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  lexipeek::set_ui_platform("windows");
  wXamlGeneratedMain();
  lexipeek::app::leave(exit_code);
}

namespace winrt::LexiPeek::implementation {

App::App() {
  UnhandledException([](auto &&, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const &e) {
    OutputDebugStringW((L"LexiPeek: " + e.Message() + L"\n").c_str());
  });
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &) {
  window = make<MainWindow>();
  window.Activate();
}

void App::quit(int code) {
  exit_code = code;
  Microsoft::UI::Xaml::Application::Current().Exit();
}

}  // namespace winrt::LexiPeek::implementation
