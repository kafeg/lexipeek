// Settings (qt/settings_window, mac SettingsView) as Windows 11's: sections in a NavigationView, sub-tabs in a SelectorBar,
// the core's rows as setting cards or a pane (Packs, Other dictionaries); About. Closing hides them: a download goes on.
#pragma once
#include <functional>
#include <map>
#include "core_client.hpp"
#include "dicts_pane.hpp"
#include "packs_pane.hpp"
#include "ui.hpp"

namespace lexipeek::win {

// What the shell knows and the core does not, kept between redraws.
struct SettingsShell {
  explicit SettingsShell(CoreClient &c) : client(c) {}
  CoreClient &client;
  bool checking = false, testing = false;
  UpdateCheck update;
  std::string test, downloading, download_error, lookup_error, input_error;
  double progress = 0;
  mutable std::map<std::string, bool> keys;  // per provider: a key is stored (the store read once, again after a change)
  SettingsState state() const;
};

// The order of Settings' sections: General first (as the Mac app), the core keeps InkPeek's.
inline const SettingsSection shown_sections[] = {SettingsSection::general, SettingsSection::languages, SettingsSection::card, SettingsSection::ai,
                                                 SettingsSection::speech};

class SettingsWindow {
 public:
  SettingsWindow(CoreClient &client, xaml::ElementTheme theme);
  void select(SettingsSection section, int tab);
  void refresh(xaml::ElementTheme theme);  // labels in the app's language, the theme, the page again
  void hotkey_errors(const std::string &lookup, const std::string &input);
  void scroll_to_end();  // snapshots: the page's end
  bool scrolls() const;
  std::function<void()> changed;          // settings.json was written: the shell applies it
  std::function<void()> open_first_run;
  std::function<void(bool)> recording;    // a hotkey being recorded: the shell's hotkeys off meanwhile
  xaml::Window window{nullptr};

 private:
  void draw();
  void draw_now();
  xaml::UIElement row(const SettingRow &r);
  xaml::UIElement hotkey(const SettingRow &r);
  xaml::UIElement apps(const SettingRow &r);
  xaml::UIElement method(const SettingRow &r);
  void record(bool input);
  void choose_model(const std::string &key);
  void after(const std::string &error);
  void press(const SettingRow &r);
  void edit_prompt(const std::string &id);
  void download(const std::string &method, const std::vector<std::string> &ids);
  void download_speech();  // Read aloud's voice model
  void add_voice();        // Read aloud → Voice from a recording…
  SettingsShell sh_;
  xaml::Controls::NavigationView nav_{nullptr};
  xaml::Controls::Grid page_{nullptr};
  xaml::Controls::ScrollViewer scroll_{nullptr};
  xaml::Controls::ProgressBar bar_{nullptr};  // the method's download, moved by the timer
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer poll_{nullptr};
  PacksPane packs_;
  DictsPane dicts_;
  SettingsSection section_ = SettingsSection::general, drawn_section_ = SettingsSection::general;
  int drawn_tab_ = -1;
  double offset_ = 0;
  std::map<SettingsSection, int> tabs_;
  std::string error_;
  bool pending_ = false, selecting_ = false;
};

class AboutWindow {
 public:
  AboutWindow(CoreClient &client, xaml::ElementTheme theme);
  void refresh(xaml::ElementTheme theme);
  void select(int tab);
  xaml::Window window{nullptr};

 private:
  void draw();
  void check();
  SettingsShell sh_;
  xaml::Controls::SelectorBar tabs_{nullptr};
  xaml::Controls::StackPanel body_{nullptr};
  xaml::Controls::TextBlock name_{nullptr}, line_{nullptr};
  int tab_ = 0;
};

// This build's version, 0.1.<git commit count> (InkPeek's).
std::string version();

}  // namespace lexipeek::win
