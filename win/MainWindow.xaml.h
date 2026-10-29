#pragma once
#include <functional>
#include <memory>
#include "MainWindow.g.h"
#include "card.hpp"

namespace lexipeek::win {
// What outlives the LexiPeek window (closed, it is made again): its card and the query.
struct MainKeep {
  explicit MainKeep(CoreClient &c) : client(c), card(c) {}
  CoreClient &client;
  CardModel card;
  std::string query;
};
}  // namespace lexipeek::win

namespace winrt::LexiPeek::implementation {

// The LexiPeek window (qt/main_window, mac MainWindow): Look up (its card) · Words · Phrases · History (Notes) · Help.
struct MainWindow : MainWindowT<MainWindow> {
  enum Page { lookup, words, phrases, history, help };
  MainWindow() = default;
  ~MainWindow();
  void InitializeComponent();
  void start(lexipeek::win::MainKeep &keep);  // once, after it is made
  void refresh(Microsoft::UI::Xaml::ElementTheme theme);  // labels in the app's language, the theme, the open page
  void show_page(int page);
  void look(const std::string &text);
  bool scroll_to_end();  // the open page's end (snapshots); false when it does not scroll
  std::function<void(int section, int tab)> open_pane;  // section -1: Settings as it was left
  std::function<void()> open_about, open_first_run;

 private:
  void draw_notes();
  void draw_help();
  void remove(const std::string &text);
  void said(const std::string &s);
  lexipeek::win::MainKeep *keep_ = nullptr;
  std::unique_ptr<lexipeek::win::CardWidget> card_;
  Microsoft::UI::Dispatching::DispatcherQueueTimer memory_{nullptr};
  int page_ = lookup, notes_page_ = 0, listening_ = -1, card_listening_ = -1;
  bool help_drawn_ = false;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // work off the window's thread finds it gone
};

}  // namespace winrt::LexiPeek::implementation

namespace winrt::LexiPeek::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}  // namespace winrt::LexiPeek::factory_implementation
