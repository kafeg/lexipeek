// The tray app (mac AppDelegate): its icon and menu, the hotkeys, the pop-up card, the windows.
#pragma once
#include <QFutureWatcher>
#include <QMenu>
#include <QTimer>
#include <QObject>
#include <QSystemTrayIcon>
#include "first_run.hpp"
#include "main_window.hpp"
#include "popup.hpp"
#include "selection_icon.hpp"
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
  // A launch's arguments (this one's or a second launch's): a lexipeek:// link, else the LexiPeek window.
  void handle(const QStringList &args);

 private:
  void fill_menu();
  void register_hotkeys();
  void update_tray();
  void selection_read();  // ⚠ while no language pack is installed
  CoreClient &c_;
  CardPopup popup_;
  SelectionIcon icon_;
  MainWindow *main_ = nullptr;
  SettingsWindow *settings_ = nullptr;
  AboutWindow *about_ = nullptr;
  FirstRunWindow *first_ = nullptr;
  QSystemTrayIcon tray_;
  QMenu menu_;
  QTimer health_;
  // The hotkey's selection, read off the window's thread (an app may take a while to answer).
  QFutureWatcher<std::optional<platform::Selected>> reading_;
  std::string reading_app_;
  bool reading_open_ = false;
  bool pair_ = true;
  Settings s_;
};

}  // namespace lexipeek::qt
