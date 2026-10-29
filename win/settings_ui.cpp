#include "pch.h"
#include "settings_ui.hpp"
#include <shobjidl_core.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace lexipeek::win::page {

namespace {

// Theme brushes come from markup ({ThemeResource}): from code they would follow the app's theme, not the window's.
const wchar_t *secondary_text = L" Foreground=\"{ThemeResource TextFillColorSecondaryBrush}\"";

}  // namespace

controls::FontIcon icon(wchar_t glyph, double size) {
  controls::FontIcon f;
  f.Glyph(winrt::hstring(std::wstring(1, glyph)));
  f.FontSize(size);
  return f;
}

controls::TextBlock text(const std::string &s, const wchar_t *style, bool secondary) {
  auto t = load(std::wstring(L"<TextBlock TextWrapping=\"Wrap\" Style=\"{StaticResource ") + style + L"}\"" + (secondary ? secondary_text : L"") + L"/>")
               .as<controls::TextBlock>();
  t.Text(h(s));
  return t;
}

controls::Grid card(wchar_t glyph, const std::string &title, const std::string &about, const xaml::UIElement &control, const xaml::UIElement &below) {
  auto g = load(LR"(<Grid Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}"
      BorderThickness="1" CornerRadius="8" Padding="16,12,16,12" MinHeight="68">
  <Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <FontIcon x:Name="Icon" FontSize="20" Margin="4,0,20,0" VerticalAlignment="Center"/>
  <StackPanel Grid.Column="1" VerticalAlignment="Center" MinHeight="0">
    <TextBlock x:Name="Title" Style="{StaticResource BodyTextBlockStyle}" TextWrapping="Wrap"/>
    <TextBlock x:Name="About" Style="{StaticResource CaptionTextBlockStyle}" Foreground="{ThemeResource TextFillColorSecondaryBrush}" TextWrapping="Wrap"/>
  </StackPanel>
  <ContentControl x:Name="Control" Grid.Column="2" Margin="24,0,0,0" VerticalAlignment="Center" HorizontalAlignment="Right"
                  HorizontalContentAlignment="Right" VerticalContentAlignment="Center" IsTabStop="False"/>
</Grid>)")
               .as<controls::Grid>();
  auto i = part<controls::FontIcon>(g, L"Icon");
  if (glyph) i.Glyph(winrt::hstring(std::wstring(1, glyph)));
  else i.Visibility(xaml::Visibility::Collapsed);
  part<controls::TextBlock>(g, L"Title").Text(h(title));
  auto a = part<controls::TextBlock>(g, L"About");
  a.Text(h(about));
  if (about.empty()) a.Visibility(xaml::Visibility::Collapsed);
  auto c = part<controls::ContentControl>(g, L"Control");
  if (control) c.Content(control);
  else c.Visibility(xaml::Visibility::Collapsed);
  if (below) {
    auto b = below.as<xaml::FrameworkElement>();
    controls::Grid::SetRow(b, 1);
    controls::Grid::SetColumn(b, 1);
    controls::Grid::SetColumnSpan(b, 2);
    b.Margin({0, 12, 0, 0});
    g.Children().Append(b);
  }
  return g;
}

controls::Grid card(const xaml::UIElement &lead, const std::string &title, const std::string &about, const xaml::UIElement &control,
                    const xaml::UIElement &below) {
  auto g = card(0, title, about, control, below);
  auto l = lead.as<xaml::FrameworkElement>();
  l.Margin({0, 0, 12, 0});
  l.VerticalAlignment(xaml::VerticalAlignment::Center);
  g.Children().Append(l);
  return g;
}

void dim(const controls::Grid &card) {
  part<controls::TextBlock>(card, L"Title").Opacity(0.55);
  part<controls::TextBlock>(card, L"About").Opacity(0.55);
}

controls::StackPanel heading(const std::string &title, const std::string &about, bool first) {
  controls::StackPanel p;
  p.Margin({1, first ? 4.0 : 26.0, 0, 6});
  p.Children().Append(text(title, L"BodyStrongTextBlockStyle"));
  if (!about.empty()) p.Children().Append(text(about, L"CaptionTextBlockStyle", true));
  return p;
}

controls::Button button(const winrt::hstring &label, std::function<void()> click, bool accent) {
  controls::Button b;
  b.Content(winrt::box_value(label));
  b.MinWidth(120);
  if (accent) b.Style(xaml::Application::Current().Resources().Lookup(winrt::box_value(L"AccentButtonStyle")).as<xaml::Style>());
  if (click) b.Click([click](auto &&, auto &&) { click(); });
  return b;
}

controls::Button icon_button(wchar_t glyph, const winrt::hstring &tip, std::function<void()> click) {
  // Subtle: no fill or border but under the pointer (Windows 11's icon buttons).
  auto b = load(LR"(<Button Background="Transparent" BorderThickness="0" Padding="8" MinWidth="36" MinHeight="32">
  <Button.Resources>
    <SolidColorBrush x:Key="ButtonBackgroundPointerOver" Color="{ThemeResource SubtleFillColorSecondary}"/>
    <SolidColorBrush x:Key="ButtonBackgroundPressed" Color="{ThemeResource SubtleFillColorTertiary}"/>
    <SolidColorBrush x:Key="ButtonBackgroundDisabled" Color="Transparent"/>
    <SolidColorBrush x:Key="ButtonBorderBrushPointerOver" Color="Transparent"/>
    <SolidColorBrush x:Key="ButtonBorderBrushPressed" Color="Transparent"/>
    <SolidColorBrush x:Key="ButtonBorderBrushDisabled" Color="Transparent"/>
  </Button.Resources>
</Button>)").as<controls::Button>();
  b.Content(icon(glyph, 14));
  controls::ToolTipService::SetToolTip(b, winrt::box_value(tip));
  xaml::Automation::AutomationProperties::SetName(b, tip);
  if (click) b.Click([click](auto &&, auto &&) { click(); });
  return b;
}

controls::StackPanel toggle(bool on, bool enabled, std::function<void(bool)> changed, const std::string &name) {
  auto p = load(LR"(<StackPanel Orientation="Horizontal">
  <TextBlock x:Name="State" VerticalAlignment="Center" Margin="0,0,12,0" Style="{StaticResource BodyTextBlockStyle}"/>
  <ToggleSwitch x:Name="Switch" OnContent="" OffContent="" MinWidth="0" Margin="0,0,-12,0"/>
</StackPanel>)")
               .as<controls::StackPanel>();
  auto state = part<controls::TextBlock>(p, L"State");
  auto sw = part<controls::ToggleSwitch>(p, L"Switch");
  state.Text(tr(on ? "toggle_on" : "toggle_off"));
  sw.IsOn(on);
  xaml::Automation::AutomationProperties::SetName(sw, h(name));
  sw.IsEnabled(enabled);
  state.Opacity(enabled ? 1 : 0.5);
  sw.Toggled([weak = winrt::make_weak(state), changed](winrt::Windows::Foundation::IInspectable const &sender, auto &&) {
    bool on = sender.as<controls::ToggleSwitch>().IsOn();
    if (auto s = weak.get()) s.Text(tr(on ? "toggle_on" : "toggle_off"));
    changed(on);
  });
  return p;
}

controls::ProgressBar progress(double value) {
  controls::ProgressBar p;
  p.Minimum(0), p.Maximum(1);
  if (value < 0) p.IsIndeterminate(true);
  else p.Value(value);
  return p;
}

controls::InfoBar info(const std::string &message, controls::InfoBarSeverity severity, const std::string &title) {
  controls::InfoBar b;
  b.Severity(severity);
  b.Title(h(title));
  b.Message(h(message));
  b.IsClosable(false);
  b.IsOpen(true);
  b.Margin({0, 0, 0, 8});
  return b;
}

HWND owner(const xaml::UIElement &e) {
  if (!e || !e.XamlRoot()) return nullptr;
  return winrt::Microsoft::UI::GetWindowFromWindowId(e.XamlRoot().ContentIslandEnvironment().AppWindowId());
}

namespace {
// The system's open dialog: its picked paths (UTF-8); none when cancelled.
std::vector<std::string> pick(HWND owner, const winrt::hstring &title, FILEOPENDIALOGOPTIONS options) {
  std::vector<std::string> out;
  try {
    auto d = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
    FILEOPENDIALOGOPTIONS o{};
    d->GetOptions(&o);
    d->SetOptions(o | options | FOS_FORCEFILESYSTEM);
    d->SetTitle(title.c_str());
    if (d->Show(owner) != S_OK) return out;
    winrt::com_ptr<IShellItemArray> items;
    winrt::check_hresult(d->GetResults(items.put()));
    DWORD n = 0;
    items->GetCount(&n);
    for (DWORD i = 0; i < n; ++i) {
      winrt::com_ptr<IShellItem> item;
      PWSTR path = nullptr;
      if (SUCCEEDED(items->GetItemAt(i, item.put())) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        out.push_back(winrt::to_string(path));
        CoTaskMemFree(path);
      }
    }
  } catch (winrt::hresult_error const &) {
  }
  return out;
}
}  // namespace

std::vector<std::string> pick_files(HWND owner, const winrt::hstring &title) { return pick(owner, title, FOS_ALLOWMULTISELECT); }

std::string pick_folder(HWND owner, const winrt::hstring &title) {
  auto p = pick(owner, title, FOS_PICKFOLDERS);
  return p.empty() ? "" : p.front();
}

void open(const std::string &target) { ShellExecuteW(nullptr, L"open", h(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

controls::HyperlinkButton link(const winrt::hstring &label, const std::string &url) {
  controls::HyperlinkButton b;
  b.Content(winrt::box_value(label));
  b.Click([url](auto &&, auto &&) { open(url); });
  controls::ToolTipService::SetToolTip(b, winrt::box_value(h(url)));
  return b;
}

controls::ContentDialog dialog(const xaml::UIElement &on, const winrt::hstring &title, const winrt::Windows::Foundation::IInspectable &content,
                               const winrt::hstring &primary, const winrt::hstring &close, const winrt::hstring &secondary) {
  controls::ContentDialog d;
  d.Style(xaml::Application::Current().Resources().Lookup(winrt::box_value(L"DefaultContentDialogStyle")).as<xaml::Style>());
  d.XamlRoot(on.XamlRoot());
  d.RequestedTheme(on.as<xaml::FrameworkElement>().ActualTheme());
  d.Title(winrt::box_value(title));
  d.Content(content);
  d.PrimaryButtonText(primary);
  d.SecondaryButtonText(secondary);
  d.CloseButtonText(close);
  d.DefaultButton(primary.empty() ? controls::ContentDialogButton::Close : controls::ContentDialogButton::Primary);
  return d;
}

namespace {
winrt::fire_and_forget shown(controls::ContentDialog d, std::function<void(controls::ContentDialogResult)> done) {
  auto r = controls::ContentDialogResult::None;
  try {
    r = co_await d.ShowAsync();  // another dialog open on that window: this one is not shown
  } catch (winrt::hresult_error const &) {
    co_return;
  }
  if (done) done(r);
}
}  // namespace

void show(controls::ContentDialog d, std::function<void(controls::ContentDialogResult)> done) { shown(d, std::move(done)); }

void confirm(const xaml::UIElement &on, const std::string &title, const std::string &detail, const std::string &yes, std::function<void()> then) {
  auto body = text(detail);
  show(dialog(on, h(title), body, h(yes), tr("cancel")), [then](controls::ContentDialogResult r) {
    if (r == controls::ContentDialogResult::Primary) then();
  });
}

winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer every(int ms, std::function<void()> tick) {
  auto t = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
  t.Interval(std::chrono::milliseconds(ms));
  t.Tick([tick](auto &&, auto &&) { tick(); });
  return t;
}

}  // namespace lexipeek::win::page
