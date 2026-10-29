// The icon at a new selection (Card → Opening, qt/selection_icon, mac SelectionIcon): after a drag or a double click in
// another app the selection is read (UI Automation, never the clipboard) and a small icon appears by the cursor; a click
// opens the card.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "core_client.hpp"
#include "lexipeek/os.hpp"
#include "ui.hpp"

namespace lexipeek::win {

class SelectionIcon {
 public:
  explicit SelectionIcon(CoreClient &client);
  void set_enabled(bool on, std::vector<std::string> off_apps);
  std::function<void(const os::Selected &)> open;
  xaml::Window window{nullptr};

 private:
  void input(os::Input i);
  void show_at(const os::Selected &s, POINT at);
  void close();
  CoreClient &c_;
  POINT down_at_{}, up_at_{};
  ULONGLONG last_up_ = 0;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer hide_{nullptr}, read_{nullptr};
  std::atomic<bool> reading_ = false;
  os::Selected found_;
  std::vector<std::string> off_apps_;
};

}  // namespace lexipeek::win
