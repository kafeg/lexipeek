// The tray app (qt/shell, mac AppDelegate): the tray icon and its menu, the hotkeys, the pop-up card, the windows.
#pragma once
#include <atomic>
#include <optional>
#include "MainWindow.xaml.h"
#include "core_client.hpp"
#include "first_run.hpp"
#include "popup.hpp"
#include "selection_icon.hpp"
#include "settings_window.hpp"
#include "tray.hpp"

namespace lexipeek::win {

// The tray's menu (a WinUI MenuFlyout, as Windows 11's own tray menus) in a borderless host window at the click.
class MenuHost {
 public:
  void show(const xaml::Controls::MenuFlyout &menu, POINT at, xaml::ElementTheme theme);
  void hide();
  void close();  // the host too (before the app ends)
  bool open() const { return menu_ && menu_.IsOpen(); }
  xaml::UIElement shown() const;  // the open menu's presenter (snapshots)

 private:
  void open_at();
  xaml::Window window_{nullptr};
  xaml::Controls::Grid root_{nullptr};
  xaml::Controls::MenuFlyout menu_{nullptr};
  POINT at_{};
  bool pending_ = false;
};

class Shell {
 public:
  // live: the tray icon, hotkeys and lexipeek:// (not for snapshots).
  Shell(CoreClient &client, bool live);
  void translate_selection();  // the lookup hotkey
  void type_text();            // the input hotkey
  void open_main();
  void open_settings(int section, int tab);
  void open_about();
  void open_first_run(bool fetch = true);  // fetch: the packs list when there is none (not in snapshots)
  void apply();  // settings.json changed: hotkeys, theme, the core
  // A launch's arguments (this one's or a second launch's): a lexipeek:// link, else the LexiPeek window.
  void handle(const std::vector<std::string> &args);
  void show_menu(POINT at);  // the tray's menu at a screen point (physical pixels)
  void quit(int code = 0);  // every window closed, the tray icon gone, then the app ends
  xaml::ElementTheme theme() const;
  CoreClient &client() { return c_; }

  // What snapshots draw (win/snapshots.cpp).
  CardPopup card;
  SelectionIcon icon;
  MenuHost menu;
  winrt::LexiPeek::MainWindow main{nullptr};
  MainKeep main_keep;  // the window's card and query (the window is made again after a close)
  xaml::Window settings{nullptr}, about{nullptr}, first{nullptr};
  SettingsWindow *settings_ui = nullptr;  // made once, hidden when closed
  AboutWindow *about_ui = nullptr;
  FirstRunWindow *first_ui = nullptr;
  std::optional<xaml::ElementTheme> forced_theme;  // light or dark whatever the settings say
  bool force_warnings = false;                     // ⚠ lines as if no pack and a hotkey taken

 private:
  xaml::Controls::MenuFlyout fill_menu();
  void register_hotkeys();
  void update_tray();
  void selection_read(std::optional<os::Selected> s);
  CoreClient &c_;
  bool live_;
  Tray tray_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer health_{nullptr};
  std::atomic<bool> reading_ = false;
  std::string reading_app_;
  bool reading_open_ = false;
  bool pair_ = true;
  bool keys_ = true;  // both hotkeys registered
  std::string lookup_error_, input_error_;  // a hotkey taken: Settings says so on its row
  bool low_ = false;  // memory was low at the last check
  Settings s_;
};

}  // namespace lexipeek::win
