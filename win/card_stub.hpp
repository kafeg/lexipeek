// The pop-up card's stand-in until phase 2 builds the card: the text it was asked for, at the selection; a field to type
// in; Esc closes it.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include "lexipeek/core.hpp"
#include "lexipeek/os.hpp"
#include "ui.hpp"

namespace lexipeek::win {

class CardStub {
 public:
  std::function<xaml::ElementTheme()> theme;
  // text nil: the field to type in (focused when focus); at: the selection's bounds (physical pixels), else the cursor.
  void show(const std::optional<std::string> &text, Source source, const os::Rect &at, bool focus);
  void close();
  bool open() const { return open_; }
  bool input() const { return input_; }  // the card to type in
  void redraw() { draw(); }
  std::string shown;   // the text the open card was asked for
  std::string unread;  // the app whose selection could not be read ("" none)
  xaml::Window window{nullptr};

 private:
  void draw();
  void keys(bool on);
  bool open_ = false, keys_ = false, input_ = false;
  Source source_ = Source::input;
  xaml::FrameworkElement content_{nullptr};
};

}  // namespace lexipeek::win
