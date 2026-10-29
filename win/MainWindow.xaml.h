#pragma once
#include "MainWindow.g.h"

namespace winrt::LexiPeek::implementation {

struct MainWindow : MainWindowT<MainWindow> {
  MainWindow() = default;
  void InitializeComponent();

 private:
  fire_and_forget self_test(std::wstring out);
};

}  // namespace winrt::LexiPeek::implementation

namespace winrt::LexiPeek::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}  // namespace winrt::LexiPeek::factory_implementation
