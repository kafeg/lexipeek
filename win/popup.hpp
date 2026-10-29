// The pop-up card (qt/popup, mac CardController): a borderless window at the selection or the cursor that takes no focus
// unless typing; Esc, an outside click (unless pinned) or the hotkey again close it; a drag moves it.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include "card.hpp"
#include "lexipeek/os.hpp"

namespace lexipeek::win {

class CardPopup {
 public:
  explicit CardPopup(CoreClient &client);
  CardModel model;
  CardWidget card;
  std::function<xaml::ElementTheme()> theme;
  bool live = true;        // Esc and Ctrl+P for every app, an outside click closes it (not for snapshots)
  bool pin_moved = false;  // a dragged card stays open, as pinned (Settings → General)
  // text nil: the text field (focused when focus); at: the selection's bounds (physical pixels), else the cursor.
  void show(const std::optional<std::string> &text, Source source, const os::Rect &at, bool focus);
  void close();
  bool open() const { return open_; }
  bool input() const { return model.input; }
  void redraw() { card.rebuild(); }
  std::string shown;  // the text the open card was asked for
  xaml::Window window{nullptr};

 private:
  void make();
  void fit();
  void keys(bool on);  // Esc and Ctrl+P for every app: only while the card is open and not pinned
  void key(const xaml::Input::KeyRoutedEventArgs &e);
  RECT screen_{};
  double scale_ = 1;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer outside_{nullptr};
  winrt::Microsoft::UI::Composition::SystemBackdrops::DesktopAcrylicController acrylic_{nullptr};
  winrt::Microsoft::UI::Composition::SystemBackdrops::SystemBackdropConfiguration backdrop_{nullptr};
  bool open_ = false, was_down_ = false, keys_ = false, dragging_ = false;
  POINT drag_{}, start_{};
};

}  // namespace lexipeek::win
