// The first-run window (qt/first_run, mac FirstRun) as Windows' own setup pages: a large title, a short text, the pair's
// choices, one accent button. The core's first_run_view says which step it is and what it shows; Windows has no
// Accessibility step (the state is always trusted).
#pragma once
#include <functional>
#include "core_client.hpp"
#include "ui.hpp"

namespace lexipeek::win {

class FirstRunWindow {
 public:
  FirstRunWindow(CoreClient &client, xaml::ElementTheme theme);
  FirstRunState state;
  void refresh(xaml::ElementTheme theme);  // the theme, the step drawn again
  void start();                            // the packs list fetched when there is none
  std::function<void()> finished;          // Done or closed: it opens by itself again only while no pair is installed
  xaml::Window window{nullptr};

 private:
  void draw();
  void draw_again();  // the core's view of the state again, then drawn
  void primary();
  void other();
  void fetch();
  void install();
  void close();
  CoreClient &c_;
  FirstRunView look_;
  xaml::Controls::StackPanel body_{nullptr}, buttons_{nullptr};
  xaml::Controls::TextBlock step_{nullptr}, title_{nullptr}, text_{nullptr};
  xaml::Controls::ProgressBar bar_{nullptr};
  xaml::Controls::TextBlock progress_{nullptr};
  app::Download dl_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer poll_{nullptr};
};

}  // namespace lexipeek::win
