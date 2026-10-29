#pragma once
#include <functional>
#include "MainWindow.g.h"

namespace winrt::LexiPeek::implementation {

// The LexiPeek window; its pages come in phase 3 (the frame, its labels and the foot's actions now).
struct MainWindow : MainWindowT<MainWindow> {
  MainWindow() = default;
  void InitializeComponent();
  void refresh(Microsoft::UI::Xaml::ElementTheme theme);  // labels in the app's language, the theme
  std::function<void()> open_settings, open_about;
};

}  // namespace winrt::LexiPeek::implementation

namespace winrt::LexiPeek::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}  // namespace winrt::LexiPeek::factory_implementation
