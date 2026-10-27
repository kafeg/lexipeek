// Settings (mac SettingsView): the sections as a tab bar, a section's sub-tabs, the core's rows or a pane of its own;
// About (version, the update check, links · Licenses).
#pragma once
#include <QWidget>
#include <map>
#include "core_client.hpp"

class QTabBar;
class QScrollArea;
class QVBoxLayout;

namespace lexipeek::qt {

// What the shell knows and the core does not, kept between redraws.
struct SettingsShell {
  explicit SettingsShell(CoreClient &c) : client(c) {}
  CoreClient &client;
  bool checking = false, testing = false;
  UpdateCheck update;
  std::string test, downloading, download_error, lookup_error, input_error;
  double progress = 0;
  SettingsState state() const;
};

class SettingsWindow : public QWidget {
  Q_OBJECT
 public:
  explicit SettingsWindow(CoreClient &client);
  void select(SettingsSection section, int tab);
  void refresh();
  void hotkey_errors(const std::string &lookup, const std::string &input);

 signals:
  void changed();  // settings.json was written: the shell applies it
  void openFirstRun();

 private:
  void draw();
  QWidget *row(const SettingRow &r);
  void after(const std::string &error);
  void press(const SettingRow &r);
  void download(const std::string &method, const std::vector<std::string> &ids);
  SettingsShell sh_;
  QTabBar *sections_;
  QVBoxLayout *body_;
  QWidget *packs_ = nullptr;  // kept while the window lives: a download goes on across redraws
  SettingsSection section_ = SettingsSection::general;
  std::map<SettingsSection, int> tabs_;
  std::string error_;
};

class AboutWindow : public QWidget {
  Q_OBJECT
 public:
  explicit AboutWindow(CoreClient &client);
  void refresh();

 private:
  SettingsShell sh_;
  QTabBar *tabs_;
  QVBoxLayout *body_;
};

// The order of Settings' sections: General first (as the Mac app), the core keeps InkPeek's.
inline const SettingsSection shown_sections[] = {SettingsSection::general, SettingsSection::languages, SettingsSection::card,
                                                 SettingsSection::ai};

// This build's version, 0.1.<git commit count> (InkPeek's).
std::string version();

}  // namespace lexipeek::qt
