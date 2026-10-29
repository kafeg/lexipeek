#include "pch.h"
#include "settings_window.hpp"
#include <algorithm>
#include <optional>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include "lexipeek/os.hpp"
#include "resource.h"
#include "settings_ui.hpp"
#include "version.h"

namespace lexipeek::win {

namespace controls = xaml::Controls;
using winrt::Windows::Foundation::IInspectable;

std::string version() { return LEXIPEEK_VERSION; }

SettingsState SettingsShell::state() const {
  SettingsState s;
  auto dir = client.data();
  s.trusted = true;  // Windows reads the selection without a permission
  s.has_pair = app::has_pair(dir);
  s.login = os::login();
  s.version = version();
  s.answers = app::answers(dir);
  s.input = app::hotkey_label(app::settings(dir).input);
  s.checking = checking;
  s.update = update.text, s.update_version = update.version, s.update_url = update.url;
  auto provider = app::ai_provider(dir);
  if (!keys.contains(provider)) keys[provider] = !os::secret(provider).empty();
  s.ai_key = keys[provider];
  s.ai_testing = testing;
  s.ai_test = test;
  s.device = client.device;
  s.downloading = downloading;
  s.progress = progress;
  s.download_error = download_error;
  return s;
}

namespace {

// Segoe Fluent Icons for the rows that have a fitting one (Windows Settings' own where it has the same setting).
wchar_t glyph(const std::string &key) {
  static const std::map<std::string, int> glyphs{
      {"open_at_login", 0xE7E8}, {"ui_lang", 0xF2B7},       {"keep_model", 0xE823}, {"lookup", 0xE765},     {"input", 0xE765},
      {"ignore_apps", 0xECAA},   {"setup", 0xE896},         {"answer", 0xE774},     {"study", 0xE7BE},      {"mt_english", 0xE8C1},
      {"phrase_open", 0xE8A7},   {"history", 0xE81C},       {"selection_icon", 0xE8B3}, {"pin_moved", 0xE718}, {"full", 0xE82D},
      {"order", 0xE8CB},         {"theme", 0xE790},         {"width", 0xE740},      {"text", 0xE8E9},       {"font", 0xE8D2},
      {"height", 0xE9A6},        {"ai_provider", 0xE99A},   {"ai_preset", 0xE71D},  {"ai_url", 0xE71B},     {"ai_command", 0xE756},
      {"ai_key", 0xE8D7},        {"ai_model", 0xE950},      {"answer_lang", 0xE774}, {"edit", 0xE70F},      {"ai_test", 0xE9D9},
      {"ai_translate", 0xE8C1},  {"ai_prefetch", 0xE896},   {"ai_reset", 0xE72C},   {"download_error", 0xE783}, {"version", 0xE946},
      {"updates", 0xE895},       {"release", 0xE896},       {"source", 0xE943},     {"inkpeek", 0xE71B},    {"license", 0xE8A5}};
  auto i = glyphs.find(key);
  return static_cast<wchar_t>(i == glyphs.end() ? 0 : i->second);
}

xaml::FrameworkElement root(const xaml::Window &w) { return w.Content().as<xaml::FrameworkElement>(); }

// Closed by its title bar: hidden (a download or a check goes on), shown again as it was.
void hide_on_close(const xaml::Window &w) {
  w.AppWindow().Closing([](winrt::Microsoft::UI::Windowing::AppWindow const &aw, winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const &e) {
    e.Cancel(true);
    aw.Hide();
  });
}

// The core's hotkey label ("Win+Shift+D") as keys in accent boxes (PowerToys' and Windows' shortcut look).
controls::StackPanel keycaps(const std::string &label) {
  controls::StackPanel p;
  p.Orientation(controls::Orientation::Horizontal);
  p.Spacing(4);
  size_t at = 0;
  for (;;) {
    auto plus = label.find('+', at + 1);  // "Ctrl++" keeps its last plus
    auto key = label.substr(at, plus == std::string::npos ? std::string::npos : plus - at);
    auto b = load(LR"(<Border CornerRadius="4" Padding="8,3,8,4" MinWidth="28" Background="{ThemeResource AccentFillColorDefaultBrush}">
  <TextBlock x:Name="Key" HorizontalAlignment="Center" FontWeight="SemiBold" Foreground="{ThemeResource TextOnAccentFillColorPrimaryBrush}"/>
</Border>)");
    part<controls::TextBlock>(b, L"Key").Text(h(key));
    p.Children().Append(b);
    if (plus == std::string::npos) break;
    at = plus + 1;
  }
  return p;
}

// A letter or a digit with Alt, Ctrl or Win (Shift as well), as settings.json keeps it.
std::optional<Hotkey> pressed(winrt::Windows::System::VirtualKey k) {
  int vk = static_cast<int>(k);
  bool letter = vk >= 'A' && vk <= 'Z', digit = vk >= '0' && vk <= '9';
  auto down = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
  Hotkey h;
  h.option = down(VK_MENU), h.control = down(VK_CONTROL), h.shift = down(VK_SHIFT), h.command = down(VK_LWIN) || down(VK_RWIN);
  if (!(letter || digit) || !(h.option || h.control || h.command)) return std::nullopt;
  h.key = std::string(1, static_cast<char>(vk));
  return h;
}

controls::TextBlock error_text(const std::string &s) {
  auto t = load(LR"(<TextBlock TextWrapping="Wrap" Style="{StaticResource CaptionTextBlockStyle}" Foreground="{ThemeResource SystemFillColorCriticalBrush}"/>)")
               .as<controls::TextBlock>();
  t.Text(h(s));
  return t;
}

}  // namespace

// Windows Settings' layout: the sections in a NavigationView on the left, the open one's page on the right.
SettingsWindow::SettingsWindow(CoreClient &client, xaml::ElementTheme theme) : sh_(client), packs_(client), dicts_(client) {
  auto content = load(LR"(<NavigationView IsSettingsVisible="False" IsBackButtonVisible="Collapsed" PaneDisplayMode="Left"
    IsPaneToggleButtonVisible="False" OpenPaneLength="260" IsTitleBarAutoPaddingEnabled="False">
  <NavigationView.Resources>
    <SolidColorBrush x:Key="NavigationViewContentBackground" Color="Transparent"/>
    <SolidColorBrush x:Key="NavigationViewContentGridBorderBrush" Color="Transparent"/>
  </NavigationView.Resources>
  <Grid x:Name="Page">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
    <TextBlock x:Name="Heading" Style="{StaticResource TitleTextBlockStyle}" Margin="36,4,36,16"/>
    <StackPanel x:Name="Top" Grid.Row="1" Margin="36,0,36,0"/>
    <SelectorBar x:Name="Tabs" Grid.Row="2" Margin="28,0,36,12"/>
    <ScrollViewer x:Name="Scroll" Grid.Row="3"/>
  </Grid>
</NavigationView>)");
  nav_ = content.as<controls::NavigationView>();
  page_ = part<controls::Grid>(content, L"Page");
  scroll_ = part<controls::ScrollViewer>(content, L"Scroll");
  // Where the page is scrolled to (VerticalOffset lags behind a ChangeView not yet carried out).
  scroll_.ViewChanged([this](auto &&, controls::ScrollViewerViewChangedEventArgs const &e) {
    if (!e.IsIntermediate()) offset_ = scroll_.VerticalOffset();
  });
  const wchar_t glyphs[] = {0xE713, 0xE774, 0xE8A1, 0xE99A};  // General, Languages, Card, AI
  for (auto g : glyphs) {
    controls::NavigationViewItem item;
    item.Icon(page::icon(g));
    nav_.MenuItems().Append(item);
  }
  nav_.SelectedItem(nav_.MenuItems().GetAt(0));
  nav_.SelectionChanged([this](auto &&, auto &&) {
    if (selecting_) return;
    uint32_t i = 0;
    if (nav_.MenuItems().IndexOf(nav_.SelectedItem(), i)) section_ = shown_sections[i];
    draw();
  });
  part<controls::SelectorBar>(content, L"Tabs").SelectionChanged([this](controls::SelectorBar const &bar, auto &&) {
    if (selecting_) return;
    uint32_t i = 0;
    if (bar.SelectedItem() && bar.Items().IndexOf(bar.SelectedItem(), i)) tabs_[section_] = static_cast<int>(i);
    draw();
  });
  packs_.redraw = dicts_.redraw = [this] { draw(); };
  packs_.changed = [this] {
    if (changed) changed();
  };
  window = win::window(app::text("settings_t"), content, 1000, 720, theme);
  hide_on_close(window);
  poll_ = page::every(500, [this] {
    if (bar_) bar_.Value(sh_.progress);
  });
  refresh(theme);
}

void SettingsWindow::select(SettingsSection section, int tab) {
  tabs_[section] = tab;
  section_ = section;
  draw_now();
}

void SettingsWindow::refresh(xaml::ElementTheme theme) {
  retheme(window, theme, tr("settings_t"));
  dicts_.theme = theme;
  draw();
}

void SettingsWindow::hotkey_errors(const std::string &lookup, const std::string &input) {
  if (sh_.lookup_error == lookup && sh_.input_error == input) return;
  sh_.lookup_error = lookup, sh_.input_error = input;
  draw();
}

void SettingsWindow::after(const std::string &error) {
  error_ = error;
  if (changed) changed();
  draw();
}

// Drawn again once the event that changed it is over (a toggle's own Toggled, a combo box closing).
void SettingsWindow::draw() {
  if (pending_) return;
  pending_ = true;
  sh_.client.ui([this] {
    if (pending_) draw_now();
  });
}

void SettingsWindow::draw_now() {
  pending_ = false;
  bool same = section_ == drawn_section_ && tabs_[section_] == drawn_tab_;
  double scrolled = same ? offset_ : 0;
  drawn_section_ = section_, drawn_tab_ = tabs_[section_];
  auto dir = sh_.client.data();
  auto st = sh_.state();
  auto view = app::settings_page(dir, section_, tabs_[section_], st);
  tabs_[section_] = view.tab;
  selecting_ = true;
  for (uint32_t i = 0; i < 4; ++i) {
    auto item = nav_.MenuItems().GetAt(i).as<controls::NavigationViewItem>();
    item.Content(winrt::box_value(h(view.sections[static_cast<int>(shown_sections[i])])));
    if (shown_sections[i] == section_ && nav_.SelectedItem() != item) nav_.SelectedItem(item);
  }
  part<controls::TextBlock>(page_, L"Heading").Text(h(view.sections[static_cast<int>(section_)]));
  auto top = part<controls::StackPanel>(page_, L"Top");
  top.Children().Clear();
  if (!st.has_pair) {
    auto bar = page::info(app::text("nopack_d"), controls::InfoBarSeverity::Warning, app::text("nopack_t"));
    bar.ActionButton(page::button(tr("setup_btn"), [this] {
      if (open_first_run) open_first_run();
    }));
    bar.Margin({0, 0, 0, 12});
    top.Children().Append(bar);
  }
  // A section's sub-tabs: SelectorBar's text tabs over its page.
  auto tabs = part<controls::SelectorBar>(page_, L"Tabs");
  tabs.Visibility(view.tabs.empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  if (tabs.Items().Size() != view.tabs.size()) {
    tabs.Items().Clear();
    for (size_t i = 0; i < view.tabs.size(); ++i) tabs.Items().Append(controls::SelectorBarItem());
  }
  for (uint32_t i = 0; i < view.tabs.size(); ++i) {
    auto item = tabs.Items().GetAt(i);
    item.Text(h(view.tabs[i]));
    if (static_cast<int>(i) == view.tab && tabs.SelectedItem() != item) tabs.SelectedItem(item);
  }
  selecting_ = false;
  bar_ = nullptr;
  controls::StackPanel body;
  body.Spacing(4);
  body.Padding({36, 0, 36, 36});
  if (!error_.empty()) body.Children().Append(page::info(error_, controls::InfoBarSeverity::Error));
  if (view.pane == "packs") body.Children().Append(packs_.view());
  else if (view.pane == "other") body.Children().Append(dicts_.view());
  else
    for (auto &r : view.rows) {
      if (r.kind == SettingRow::Kind::heading) body.Children().Append(page::heading(r.title, r.about, body.Children().Size() == 0));
      else body.Children().Append(row(r));
    }
  scroll_.Content(body);
  scroll_.UpdateLayout();
  scroll_.ChangeView(nullptr, scrolled, nullptr, true);
  offset_ = scrolled;
}

void SettingsWindow::scroll_to_end() {
  scroll_.UpdateLayout();
  offset_ = scroll_.ScrollableHeight();
  scroll_.ChangeView(nullptr, offset_, nullptr, true);
}

bool SettingsWindow::scrolls() const { return scroll_.ScrollableHeight() > 1; }

xaml::UIElement SettingsWindow::row(const SettingRow &r) {
  using K = SettingRow::Kind;
  auto dir = sh_.client.data();
  auto key = r.key;
  auto g = glyph(r.key);
  switch (r.kind) {
    case K::heading: return page::heading(r.title, r.about, false);
    case K::toggle: {
      auto t = page::toggle(r.on, r.enabled, [this, key, dir](bool on) {
        if (key == "open_at_login") os::set_login(on), on = os::login();
        after(app::toggle(dir, key, on));
      }, r.title);
      if (!r.edit.empty()) {
        auto id = r.edit;
        auto pen = page::icon_button(0xE70F, tr("edit_btn"), [this, id] { edit_prompt(id); });
        pen.Margin({0, 0, 8, 0});
        t.Children().InsertAt(0, pen);
      }
      return page::card(g, r.title, r.about, t);
    }
    case K::choice: {
      controls::ComboBox c;
      for (auto &x : r.choices) c.Items().Append(winrt::box_value(h(x)));
      c.SelectedIndex(r.selected);
      c.IsEnabled(r.enabled);
      c.MinWidth(200);
      xaml::Automation::AutomationProperties::SetName(c, h(r.title));
      auto was = r.selected;
      c.SelectionChanged([this, key, dir, was](IInspectable const &sender, auto &&) {
        auto i = sender.as<controls::ComboBox>().SelectedIndex();
        if (i >= 0 && i != was) after(app::choose(dir, key, i, sh_.state()));
      });
      return page::card(g, r.title, r.about, c);
    }
    case K::hotkey: return hotkey(r);
    case K::apps: return apps(r);
    case K::button: {
      auto row = r;
      auto b = page::button(h(r.value), [this, row] {
        if (row.go_tab >= 0) select(row.go_section, row.go_tab);
        else press(row);
      });
      b.IsEnabled(r.enabled);
      return page::card(g, r.title, r.about, b);
    }
    case K::status: {
      if (!r.on) return page::card(g, r.title, r.about, r.value.empty() ? nullptr : page::text(r.value, L"BodyTextBlockStyle", true));
      controls::StackPanel s;
      s.Orientation(controls::Orientation::Horizontal);
      s.Spacing(8);
      auto ok = load(LR"(<FontIcon Glyph="&#xE73E;" FontSize="14" Foreground="{ThemeResource SystemFillColorSuccessBrush}"/>)");
      s.Children().Append(ok);
      s.Children().Append(page::text(r.value));
      return page::card(g, r.title, r.about, s);
    }
    case K::link: return page::card(g, r.title, r.about, page::link(h(r.value), r.value));
    case K::text:
    case K::model: {
      controls::Grid line;
      line.ColumnSpacing(8);
      line.ColumnDefinitions().Append(controls::ColumnDefinition());
      controls::ColumnDefinition right;
      right.Width(xaml::GridLengthHelper::Auto());
      line.ColumnDefinitions().Append(right);
      controls::TextBox e;
      e.Text(h(r.value));
      e.PlaceholderText(h(r.placeholder));
      xaml::Automation::AutomationProperties::SetName(e, h(r.title));
      auto commit = [this, key, dir, was = r.value](controls::TextBox const &box) {
        auto v = winrt::to_string(box.Text());
        if (v != was) after(app::set_text(dir, key, v));
      };
      e.LostFocus([commit](IInspectable const &sender, auto &&) { commit(sender.as<controls::TextBox>()); });
      e.KeyDown([commit](IInspectable const &sender, xaml::Input::KeyRoutedEventArgs const &k) {
        if (k.Key() == winrt::Windows::System::VirtualKey::Enter) commit(sender.as<controls::TextBox>());
      });
      line.Children().Append(e);
      if (r.kind == K::model && r.on) {
        auto choose = page::button(tr("model_choose"), [this, key] { choose_model(key); });
        controls::Grid::SetColumn(choose, 1);
        line.Children().Append(choose);
      }
      return page::card(g, r.title, r.about, nullptr, line);
    }
    case K::secret: {
      // The API key: typed (hidden), Save puts it into the Credential Manager; it is never shown again.
      controls::Grid line;
      line.ColumnSpacing(8);
      line.ColumnDefinitions().Append(controls::ColumnDefinition());
      for (int i = 0; i < 2; ++i) {
        controls::ColumnDefinition c;
        c.Width(xaml::GridLengthHelper::Auto());
        line.ColumnDefinitions().Append(c);
      }
      controls::PasswordBox e;
      e.PlaceholderText(tr("key_t"));
      xaml::Automation::AutomationProperties::SetName(e, tr("key_t"));
      auto store = [this, dir](controls::PasswordBox const &box) {
        auto key = winrt::to_string(box.Password());
        key.erase(0, key.find_first_not_of(" \t\r\n"));  // a pasted key often brings a space or a line break
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        if (key.empty()) return;
        if (!os::set_secret(app::ai_provider(dir), key)) return after(os::last_error());
        sh_.keys.clear();
        sh_.test.clear();
        draw();
      };
      e.KeyDown([store](IInspectable const &sender, xaml::Input::KeyRoutedEventArgs const &k) {
        if (k.Key() == winrt::Windows::System::VirtualKey::Enter) store(sender.as<controls::PasswordBox>());
      });
      line.Children().Append(e);
      auto save = page::button(tr("save_key"), [store, weak = winrt::make_weak(e)] {
        if (auto box = weak.get()) store(box);
      }, true);
      controls::Grid::SetColumn(save, 1);
      line.Children().Append(save);
      if (r.on) {
        auto remove = page::button(tr("remove"), [this, dir] {
          os::set_secret(app::ai_provider(dir), "");
          sh_.keys.clear();
          draw();
        });
        controls::Grid::SetColumn(remove, 2);
        line.Children().Append(remove);
      }
      return page::card(g, r.title, r.about, nullptr, line);
    }
    case K::method: return method(r);
  }
  return page::card(g, r.title, r.about);
}

// A shortcut: its keys as Windows draws them; a click records new ones (a dialog takes the next combination).
xaml::UIElement SettingsWindow::hotkey(const SettingRow &r) {
  auto s = app::settings(sh_.client.data());
  bool input = r.key == "input";
  controls::StackPanel col;
  col.Spacing(4);
  col.HorizontalAlignment(xaml::HorizontalAlignment::Right);
  auto keys = keycaps(app::hotkey_label(input ? s.input : s.lookup));
  keys.Children().Append(page::icon(0xE70F, 14));
  keys.Children().GetAt(keys.Children().Size() - 1).as<xaml::FrameworkElement>().Margin({8, 0, 0, 0});
  auto b = load(LR"(<Button Background="Transparent" BorderThickness="0" Padding="4"/>)").as<controls::Button>();
  b.Content(keys);
  b.HorizontalAlignment(xaml::HorizontalAlignment::Right);
  xaml::Automation::AutomationProperties::SetName(b, h(app::hotkey_label(input ? s.input : s.lookup)));
  b.Click([this, input](auto &&, auto &&) { record(input); });
  col.Children().Append(b);
  auto err = input ? sh_.input_error : sh_.lookup_error;
  if (!err.empty()) {
    auto e = error_text(err);
    e.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    col.Children().Append(e);
  }
  return page::card(glyph(r.key), r.title, r.about, col);
}

void SettingsWindow::record(bool input) {
  auto dir = sh_.client.data();
  auto picked = std::make_shared<std::optional<Hotkey>>();
  auto body = load(LR"(<StackPanel Spacing="12" MinWidth="320">
  <TextBlock x:Name="Hint" Style="{StaticResource BodyTextBlockStyle}" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
  <ContentControl x:Name="Keys" MinHeight="32" HorizontalAlignment="Left" IsTabStop="False"/>
  <TextBlock x:Name="Error" Style="{StaticResource CaptionTextBlockStyle}" Foreground="{ThemeResource SystemFillColorCriticalBrush}"/>
</StackPanel>)");
  auto s = app::settings(dir);
  part<controls::TextBlock>(body, L"Hint").Text(tr("hk_press"));
  part<controls::ContentControl>(body, L"Keys").Content(keycaps(app::hotkey_label(input ? s.input : s.lookup)));
  auto d = page::dialog(scroll_, tr(input ? "hk_input_t" : "hk_lookup_t"), body, tr("save"), tr("cancel"));
  d.IsPrimaryButtonEnabled(false);
  d.PreviewKeyDown([picked, input, dir, weak = winrt::make_weak(body)](IInspectable const &sender, xaml::Input::KeyRoutedEventArgs const &e) {
    auto k = pressed(e.Key());
    auto b = weak.get();
    if (!k || !b) return;
    e.Handled(true);
    auto s = app::settings(dir);
    part<controls::ContentControl>(b, L"Keys").Content(keycaps(app::hotkey_label(*k)));
    // The other hotkey's keys: not taken, the dialog says why.
    bool clash = *k == (input ? s.lookup : s.input);
    part<controls::TextBlock>(b, L"Error").Text(clash ? tr("hk_conflict") : L"");
    *picked = clash ? std::nullopt : k;
    sender.as<controls::ContentDialog>().IsPrimaryButtonEnabled(!clash);
  });
  if (recording) recording(true);
  page::show(d, [this, picked, input, dir](controls::ContentDialogResult r) {
    if (recording) recording(false);
    if (r != controls::ContentDialogResult::Primary || !*picked) return;
    auto s = app::settings(dir);
    (input ? s.input : s.lookup) = **picked;
    (input ? sh_.input_error : sh_.lookup_error).clear();
    after(app::save(dir, s));
  });
}

// Programs where the hotkey does nothing ("chrome.exe"): a list, ✕ to remove, a field to add one (Enter).
xaml::UIElement SettingsWindow::apps(const SettingRow &r) {
  auto dir = sh_.client.data();
  auto s = app::settings(dir);
  controls::StackPanel col;
  col.Spacing(2);
  if (s.ignore_apps.empty()) col.Children().Append(page::text(app::text("no_apps"), L"CaptionTextBlockStyle", true));
  for (auto &id : s.ignore_apps) {
    controls::Grid line;
    line.ColumnDefinitions().Append(controls::ColumnDefinition());
    controls::ColumnDefinition c;
    c.Width(xaml::GridLengthHelper::Auto());
    line.ColumnDefinitions().Append(c);
    auto name = page::text(id);
    name.VerticalAlignment(xaml::VerticalAlignment::Center);
    line.Children().Append(name);
    auto x = page::icon_button(0xE711, tr("remove"), [this, id, dir] {
      auto s = app::settings(dir);
      std::erase(s.ignore_apps, id);
      after(app::save(dir, s));
    });
    controls::Grid::SetColumn(x, 1);
    line.Children().Append(x);
    col.Children().Append(line);
  }
  controls::TextBox add;
  add.PlaceholderText(L"app.exe");
  xaml::Automation::AutomationProperties::SetName(add, h(r.title));
  add.Margin({0, 6, 0, 0});
  add.KeyDown([this, dir](IInspectable const &sender, xaml::Input::KeyRoutedEventArgs const &k) {
    if (k.Key() != winrt::Windows::System::VirtualKey::Enter) return;
    auto id = winrt::to_string(sender.as<controls::TextBox>().Text());
    id.erase(0, id.find_first_not_of(' '));
    id.erase(id.find_last_not_of(' ') + 1);
    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (id.empty()) return;
    auto s = app::settings(dir);
    if (std::find(s.ignore_apps.begin(), s.ignore_apps.end(), id) == s.ignore_apps.end()) s.ignore_apps.push_back(id);
    after(app::save(dir, s));
  });
  col.Children().Append(add);
  return page::card(glyph(r.key), r.title, r.about, nullptr, col);
}

// A translation method (InkPeek's MethodPane): a radio button, what it does, needs, why not here; its download.
xaml::UIElement SettingsWindow::method(const SettingRow &r) {
  auto dir = sh_.client.data();
  controls::RadioButton radio;
  radio.MinWidth(0);
  radio.IsChecked(r.on);
  radio.IsEnabled(r.enabled);
  xaml::Automation::AutomationProperties::SetName(radio, h(r.title));
  // Picked: chosen, and its models fetched when they are missing (InkPeek's MethodPane).
  bool fetch = !r.button.empty() && r.progress < 0;
  radio.Click([this, key = r.key, dir, selected = r.selected, fetch, value = r.value, ids = r.ids](auto &&, auto &&) {
    after(app::choose(dir, key, selected, sh_.state()));
    if (fetch) download(value, ids);
  });
  controls::StackPanel below;
  below.Spacing(6);
  if (!r.detail.empty()) {
    if (r.enabled) below.Children().Append(page::text(r.detail, L"CaptionTextBlockStyle", true));
    else below.Children().Append(error_text(r.detail));
  }
  if (r.progress >= 0) {
    bar_ = page::progress(r.progress);
    bar_.MaxWidth(360);
    bar_.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    below.Children().Append(bar_);
  }
  xaml::UIElement control{nullptr};
  if (r.progress < 0 && !r.button.empty())
    control = page::button(h(r.button), [this, method = r.value, ids = r.ids] { download(method, ids); });
  auto c = page::card(radio, r.title, r.about, control, below.Children().Size() ? below : nullptr);
  if (!r.enabled) page::dim(c);
  return c;
}

// A button's action, or ✎ of a prompt.
void SettingsWindow::press(const SettingRow &r) {
  auto dir = sh_.client.data();
  if (!r.edit.empty()) return edit_prompt(r.edit);
  if (r.key == "setup") {
    if (open_first_run) open_first_run();
    return;
  }
  if (r.key == "ai_reset") return after(app::reset_prompts(dir));
  if (r.key == "ai_test" && !sh_.testing) {
    sh_.testing = true;
    draw();
    auto k = os::secret(app::ai_provider(dir));
    page::run<std::string>(sh_.client, [dir, k] { return app::ai_test(dir, k); }, [this](std::string s) {
      sh_.test = s;
      sh_.testing = false;
      draw();
    });
  }
}

// The prompt editor (AI → ✎): one field per part, Reset puts the originals back.
void SettingsWindow::edit_prompt(const std::string &id) {
  auto dir = sh_.client.data();
  auto e = app::prompt_editor(dir, id);
  controls::StackPanel body;
  body.Spacing(8);
  body.Width(620);
  if (!e.note.empty()) body.Children().Append(page::text(e.note, L"CaptionTextBlockStyle", true));
  std::vector<controls::TextBox> fields;
  for (auto &f : e.fields) {
    controls::TextBox x;
    x.Header(f.label.empty() ? nullptr : winrt::box_value(h(f.label)));
    x.Text(h(f.text));
    x.AcceptsReturn(true);
    x.TextWrapping(xaml::TextWrapping::Wrap);
    x.Height(e.fields.size() > 1 ? 120 : 300);
    controls::ScrollViewer::SetVerticalScrollBarVisibility(x, controls::ScrollBarVisibility::Auto);
    body.Children().Append(x);
    fields.push_back(x);
  }
  controls::ScrollViewer sv;
  sv.Content(body);
  sv.MaxHeight(520);
  auto d = page::dialog(scroll_, h(e.title), sv, h(e.save), h(e.cancel), h(e.reset));
  d.Resources().Insert(winrt::box_value(L"ContentDialogMaxWidth"), winrt::box_value(720.0));
  page::show(d, [this, dir, e, fields](controls::ContentDialogResult r) {
    if (r == controls::ContentDialogResult::None) return;
    bool cleared = r == controls::ContentDialogResult::Secondary;
    std::vector<std::string> ids, texts;
    for (size_t i = 0; i < e.fields.size(); ++i) ids.push_back(e.fields[i].id), texts.push_back(cleared ? "" : winrt::to_string(fields[i].Text()));
    after(app::set_prompts(dir, ids, texts));
  });
}

// "Choose…": the provider's models (fetched once the dialog opens), a filter, free ones only; a click picks one.
void SettingsWindow::choose_model(const std::string &key) {
  auto dir = sh_.client.data();
  auto body = load(LR"(<Grid RowSpacing="10" Width="480" Height="420">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
  <TextBlock x:Name="Head" Style="{StaticResource BodyStrongTextBlockStyle}"/>
  <Grid Grid.Row="1" ColumnSpacing="12">
    <Grid.ColumnDefinitions><ColumnDefinition/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
    <TextBox x:Name="Filter"/>
    <CheckBox x:Name="Free" Grid.Column="1" Visibility="Collapsed"/>
  </Grid>
  <ProgressRing x:Name="Ring" Grid.Row="2" IsActive="True"/>
  <TextBlock x:Name="Note" Grid.Row="2" TextWrapping="Wrap" Visibility="Collapsed"/>
  <ListView x:Name="List" Grid.Row="2" IsItemClickEnabled="True" SelectionMode="None" Visibility="Collapsed"/>
</Grid>)");
  auto all = std::make_shared<std::vector<ai::Model>>();
  auto shown = std::make_shared<std::vector<ai::Model>>();
  auto filter = part<controls::TextBox>(body, L"Filter");
  auto free = part<controls::CheckBox>(body, L"Free");
  auto list = part<controls::ListView>(body, L"List");
  auto note = part<controls::TextBlock>(body, L"Note");
  part<controls::TextBlock>(body, L"Head").Text(tr("loading_models"));
  filter.PlaceholderText(tr("filter"));
  free.Content(winrt::box_value(tr("free_only")));
  auto fill = [all, shown, weak = winrt::make_weak(body)] {
    auto b = weak.get();
    if (!b) return;
    auto list = part<controls::ListView>(b, L"List");
    auto note = part<controls::TextBlock>(b, L"Note");
    *shown = app::filter_models(*all, winrt::to_string(part<controls::TextBox>(b, L"Filter").Text()),
                                part<controls::CheckBox>(b, L"Free").IsChecked().Value());
    list.Items().Clear();
    for (auto &t : app::model_titles(*shown)) list.Items().Append(winrt::box_value(h(t)));
    note.Text(tr("models_none"));
    note.Visibility(shown->empty() ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    list.Visibility(shown->empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  };
  filter.TextChanged([fill](auto &&, auto &&) { fill(); });
  free.Click([fill](auto &&, auto &&) { fill(); });
  auto d = page::dialog(scroll_, tr("model_t"), body, L"", tr("cancel"));
  list.ItemClick([this, shown, key, dir, weak = winrt::make_weak(d)](IInspectable const &sender, controls::ItemClickEventArgs const &e) {
    uint32_t i = 0;
    if (!sender.as<controls::ListView>().Items().IndexOf(e.ClickedItem(), i) || i >= shown->size()) return;
    if (auto d = weak.get()) d.Hide();
    after(app::set_text(dir, key, (*shown)[i].id));
  });
  page::show(d);
  auto k = os::secret(app::ai_provider(dir));
  page::run<app::AiModels>(sh_.client, [dir, k] { return app::ai_models(dir, k); }, [all, fill, dir, weak = winrt::make_weak(body)](app::AiModels m) {
    auto b = weak.get();
    if (!b) return;
    *all = m.list;
    part<controls::ProgressRing>(b, L"Ring").Visibility(xaml::Visibility::Collapsed);
    part<controls::TextBlock>(b, L"Head").Text(tr("models_title", app::ai_provider_title(dir), std::to_string(all->size())));
    bool any_free = std::any_of(all->begin(), all->end(), [](const ai::Model &x) { return x.free; });
    part<controls::CheckBox>(b, L"Free").Visibility(any_free ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    if (!m.error.empty()) {
      auto note = part<controls::TextBlock>(b, L"Note");
      note.Text(h(m.error));
      note.Visibility(xaml::Visibility::Visible);
      return;
    }
    fill();
  });
}

// Languages → Translation: a method's models, progress on its card.
void SettingsWindow::download(const std::string &method, const std::vector<std::string> &ids) {
  if (!sh_.downloading.empty() || ids.empty()) return;
  sh_.downloading = method, sh_.progress = 0, sh_.download_error.clear();
  app::Download dl;
  auto ticks = page::every(500, [this, dl] {
    auto total = dl.get_total();
    sh_.progress = total > 0 ? static_cast<double>(dl.get_done()) / total : 0;
  });
  ticks.Start();
  poll_.Start();
  auto dir = sh_.client.data();
  page::run<Status>(sh_.client, [dir, ids, dl] { return app::install(dir, ids, dl); }, [this, ticks](Status s) {
    ticks.Stop();
    poll_.Stop();
    sh_.downloading.clear();
    sh_.download_error = s.cancelled ? "" : s.error;
    sh_.client.reload();
    if (changed) changed();
    draw();
  });
  draw();
}

AboutWindow::AboutWindow(CoreClient &client, xaml::ElementTheme theme) : sh_(client) {
  auto content = load(LR"(<Grid Padding="24,0,24,24" RowSpacing="12">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
  <Grid ColumnSpacing="16">
    <Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition/></Grid.ColumnDefinitions>
    <Image x:Name="Logo" Width="64" Height="64"/>
    <StackPanel Grid.Column="1" VerticalAlignment="Center" Spacing="2">
      <TextBlock x:Name="Name" Style="{StaticResource SubtitleTextBlockStyle}"/>
      <TextBlock x:Name="Line" Style="{StaticResource CaptionTextBlockStyle}" Foreground="{ThemeResource TextFillColorSecondaryBrush}" TextWrapping="Wrap"/>
    </StackPanel>
  </Grid>
  <SelectorBar x:Name="Tabs" Grid.Row="1" Margin="-8,0,0,0"/>
  <ScrollViewer Grid.Row="2" Margin="0,0,-16,0"><StackPanel x:Name="Body" Spacing="4" Padding="0,0,16,0"/></ScrollViewer>
</Grid>)");
  tabs_ = part<controls::SelectorBar>(content, L"Tabs");
  body_ = part<controls::StackPanel>(content, L"Body");
  name_ = part<controls::TextBlock>(content, L"Name");
  line_ = part<controls::TextBlock>(content, L"Line");
  for (size_t i = 0; i < about_tabs().size(); ++i) tabs_.Items().Append(controls::SelectorBarItem());
  tabs_.SelectedItem(tabs_.Items().GetAt(0));
  tabs_.SelectionChanged([this](controls::SelectorBar const &bar, auto &&) {
    uint32_t i = 0;
    if (bar.SelectedItem() && bar.Items().IndexOf(bar.SelectedItem(), i)) tab_ = static_cast<int>(i);
    draw();
  });
  window = win::window(app::text("menu_about"), content, 560, 700, theme);
  part<controls::Image>(content, L"Logo").Source(app_icon(static_cast<int>(64 * GetDpiForWindow(hwnd(window)) / 96.0)));
  hide_on_close(window);
  refresh(theme);
}

void AboutWindow::refresh(xaml::ElementTheme theme) {
  retheme(window, theme, tr("menu_about"));
  draw();
}

void AboutWindow::select(int tab) { tabs_.SelectedItem(tabs_.Items().GetAt(static_cast<uint32_t>(std::clamp(tab, 0, static_cast<int>(tabs_.Items().Size()) - 1)))); }

void AboutWindow::draw() {
  auto names = about_tabs();
  for (uint32_t i = 0; i < tabs_.Items().Size() && i < names.size(); ++i) tabs_.Items().GetAt(i).Text(h(names[i]));
  body_.Children().Clear();
  using K = SettingRow::Kind;
  for (auto &r : about_rows(sh_.state(), tab_)) {
    if (r.key == "version") {
      name_.Text(h(r.title));
      line_.Text(h(r.about));
      continue;
    }
    if (r.kind == K::heading) {
      body_.Children().Append(page::heading(r.title, r.about, body_.Children().Size() == 0));
    } else if (r.kind == K::link) {
      auto url = r.value;
      body_.Children().Append(page::card(glyph(r.key), r.title, r.about, page::icon_button(0xE8A7, h(url), [url] { page::open(url); })));
    } else if (r.kind == K::button) {
      controls::StackPanel p;
      p.Orientation(controls::Orientation::Horizontal);
      p.Spacing(12);
      if (sh_.checking) {
        controls::ProgressRing ring;
        ring.Width(20), ring.Height(20);
        p.Children().Append(ring);
      }
      auto b = page::button(h(r.value), [this] { check(); });
      b.IsEnabled(r.enabled && !sh_.checking);
      p.Children().Append(b);
      body_.Children().Append(page::card(glyph(r.key), r.title, r.about, p));
    } else {
      body_.Children().Append(page::card(glyph(r.key), r.title, r.about, r.value.empty() ? nullptr : page::text(r.value, L"BodyTextBlockStyle", true)));
    }
  }
}

// "Check now" only: GitHub's latest release, never on its own.
void AboutWindow::check() {
  if (sh_.checking) return;
  sh_.checking = true;
  draw();
  page::run<UpdateCheck>(sh_.client, [] { return app::check_updates(version()); }, [this](UpdateCheck u) {
    sh_.update = u;
    sh_.checking = false;
    draw();
  });
}

}  // namespace lexipeek::win
