#pragma once
#include "App.xaml.g.h"

namespace winrt::LexiPeek::implementation {

struct App : AppT<App> {
  App();
  void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &);
  // Ends the message loop; wWinMain then leaves with [code] (no static destructors, as every LexiPeek program).
  static void quit(int code);

 private:
  Microsoft::UI::Xaml::Window window{nullptr};
};

}  // namespace winrt::LexiPeek::implementation
