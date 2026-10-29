// Settings → Languages → Other dictionaries (qt/dicts_pane, mac OtherDictionariesPane): the core's rows (packs and own
// ones in the card's order), on / off, order, folders read in place, import (also by a drop), the catalog of public sources.
#pragma once
#include <functional>
#include <memory>
#include "core_client.hpp"
#include "ui.hpp"

namespace lexipeek::win {

// The catalog: the switched-on sources' lists (fetched on opening), a filter, one download at a time with progress.
class CatalogWindow {
 public:
  explicit CatalogWindow(CoreClient &client);
  void open(xaml::ElementTheme theme);
  std::function<void()> closed;
  xaml::Window window{nullptr};

 private:
  void fetch();
  void draw();
  void get(int item, const std::string &title);
  void close();
  CoreClient &c_;
  CatalogList list_;
  bool loading_ = false;
  std::string filter_, downloading_, message_, failed_;
  app::Download dl_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer poll_{nullptr};
  xaml::Controls::StackPanel sources_{nullptr}, body_{nullptr};
  xaml::Controls::TextBox filter_box_{nullptr};
  xaml::Controls::Button done_{nullptr};
  xaml::Controls::ProgressBar bar_{nullptr};
};

class DictsPane {
 public:
  explicit DictsPane(CoreClient &client);
  xaml::UIElement view();
  std::function<void()> redraw;
  xaml::ElementTheme theme = xaml::ElementTheme::Default;  // the catalog's

 private:
  winrt::fire_and_forget dropped(xaml::DragEventArgs e);
  void import(const std::vector<std::string> &files);
  void changed(bool reload);
  CoreClient &c_;
  xaml::UIElement root_{nullptr};  // the pane drawn last (dialogs' and pickers' window)
  std::string message_;
  bool busy_ = false;
  std::unique_ptr<CatalogWindow> catalog_;
};

}  // namespace lexipeek::win
