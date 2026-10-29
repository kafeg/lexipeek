// Settings → Languages → Packs (qt/packs_pane, mac PacksView): the core's packs_view as setting cards; downloads and
// removals here, their progress kept while the window is closed.
#pragma once
#include <functional>
#include <optional>
#include "core_client.hpp"
#include "ui.hpp"

namespace lexipeek::win {

class PacksPane {
 public:
  explicit PacksPane(CoreClient &client);
  xaml::UIElement view();  // drawn now
  std::function<void()> changed;  // packs installed or removed
  std::function<void()> redraw;   // the pane wants to be drawn again

 private:
  void refresh();
  void run(const PackAction &a);
  void finished(const Status &s);
  xaml::UIElement line(const PackLine &l);
  CoreClient &c_;
  PacksView view_;
  std::optional<std::string> busy_;  // what the progress says ("" while the list is fetched)
  std::string error_, list_error_;
  app::Download dl_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer poll_{nullptr};
  xaml::Controls::ProgressBar bar_{nullptr};
};

}  // namespace lexipeek::win
