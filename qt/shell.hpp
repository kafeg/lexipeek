// The tray app (mac AppDelegate): its icon and menu, the hotkeys, the pop-up card, the windows.
#pragma once
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>
#include "first_run.hpp"
#include "main_window.hpp"
#include "popup.hpp"
#include "settings_window.hpp"

namespace lexipeek::qt {

class Shell : public QObject {
  Q_OBJECT
 public:
  explicit Shell(CoreClient &client);
  void translate_selection();  // the lookup hotkey
  void type_text();            // the input hotkey
  void open_main(int page = -1);
  void open_settings(int section, int tab);
  void open_about();
  void open_first_run();
  void apply();  // settings.json changed: hotkeys, theme, the core

 private:
  void fill_menu();
  void register_hotkeys();
  CoreClient &c_;
  CardPopup popup_;
  MainWindow *main_ = nullptr;
  SettingsWindow *settings_ = nullptr;
  AboutWindow *about_ = nullptr;
  FirstRunWindow *first_ = nullptr;
  QSystemTrayIcon tray_;
  QMenu menu_;
  Settings s_;
};

}  // namespace lexipeek::qt
