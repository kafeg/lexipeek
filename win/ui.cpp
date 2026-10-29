#include "pch.h"
#include "ui.hpp"
#include <cmath>
#include <memory>
#include "lexipeek/app.hpp"
#include "lexipeek/os.hpp"
#include "resource.h"

namespace lexipeek::win {

using namespace winrt::Microsoft::UI::Windowing;
namespace controls = xaml::Controls;

winrt::hstring h(const std::string &s) { return winrt::to_hstring(s); }
winrt::hstring tr(const char *key) { return h(app::text(key)); }
winrt::hstring tr(const char *key, const std::string &a) { return h(app::text(key, a)); }
winrt::hstring tr(const char *key, const std::string &a, const std::string &b) { return h(app::text(key, a, b)); }

xaml::ElementTheme theme(const std::string &setting) {
  return setting == "light" ? xaml::ElementTheme::Light : setting == "dark" ? xaml::ElementTheme::Dark : xaml::ElementTheme::Default;
}

xaml::FrameworkElement load(const std::wstring &text) {
  auto s = text;
  auto at = s.find_first_of(L" >", s.find(L'<'));
  s.insert(at, L" xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"");
  return xaml::Markup::XamlReader::Load(s).as<xaml::FrameworkElement>();
}

HWND hwnd(const xaml::Window &w) {
  HWND h = nullptr;
  w.as<IWindowNative>()->get_WindowHandle(&h);
  return h;
}

xaml::Window window(const std::string &title, const xaml::UIElement &content, int width, int height, xaml::ElementTheme t) {
  // Mica where the system has it (Windows 11); Windows 10 gets the theme's solid background.
  bool mica = winrt::Microsoft::UI::Composition::SystemBackdrops::MicaController::IsSupported();
  auto root = load(std::wstring(L"<Grid") + (mica ? L"" : L" Background=\"{ThemeResource SolidBackgroundFillColorBaseBrush}\"") + LR"(>
  <Grid.RowDefinitions><RowDefinition Height="48"/><RowDefinition Height="*"/></Grid.RowDefinitions>
  <Grid x:Name="TitleBar" Background="Transparent">
    <TextBlock x:Name="Title" Margin="16,0,0,0" VerticalAlignment="Center" Style="{StaticResource CaptionTextBlockStyle}"/>
  </Grid>
</Grid>)").as<controls::Grid>();
  part<controls::TextBlock>(root, L"Title").Text(h(title));
  controls::Grid::SetRow(content.as<xaml::FrameworkElement>(), 1);
  root.Children().Append(content);
  root.RequestedTheme(t);
  xaml::Window w;
  w.Title(h(title));
  w.Content(root);
  if (mica) w.SystemBackdrop(xaml::Media::MicaBackdrop());
  w.ExtendsContentIntoTitleBar(true);
  w.SetTitleBar(part<xaml::UIElement>(root, L"TitleBar"));
  auto aw = w.AppWindow();
  aw.TitleBar().PreferredHeightOption(TitleBarHeightOption::Tall);
  aw.TitleBar().PreferredTheme(t == xaml::ElementTheme::Light  ? TitleBarTheme::Light
                               : t == xaml::ElementTheme::Dark ? TitleBarTheme::Dark
                                                               : TitleBarTheme::UseDefaultAppMode);
  aw.SetIcon(winrt::Microsoft::UI::GetIconIdFromIcon(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP))));
  double scale = GetDpiForWindow(hwnd(w)) / 96.0;
  aw.Resize({static_cast<int32_t>(width * scale), static_cast<int32_t>(height * scale)});
  return w;
}

void present(const xaml::Window &w) {
  auto aw = w.AppWindow();
  if (!aw.IsVisible()) {
    POINT c{};
    GetCursorPos(&c);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST), &mi);
    auto r = mi.rcWork;
    auto s = aw.Size();
    aw.Move({(r.left + r.right - s.Width) / 2, (r.top + r.bottom - s.Height) / 2});
  }
  w.Activate();
  os::activate(hwnd(w));
}

void fit_height(const xaml::Window &w) {
  auto root = w.Content().as<xaml::FrameworkElement>();
  root.Loaded([](winrt::Windows::Foundation::IInspectable const &sender, auto &&) {
    auto root = sender.as<xaml::FrameworkElement>();
    auto scale = root.XamlRoot().RasterizationScale();
    auto width = static_cast<float>(root.ActualWidth());
    root.Measure({width, INFINITY});
    auto aw = AppWindow::GetFromWindowId(root.XamlRoot().ContentIslandEnvironment().AppWindowId());
    aw.ResizeClient({static_cast<int32_t>(std::ceil(width * scale)), static_cast<int32_t>(std::ceil(root.DesiredSize().Height * scale))});
  });
}

xaml::Window ask(const std::string &title, const std::string &detail, const std::string &yes, const std::string &no, xaml::ElementTheme t,
                 std::function<void(bool)> done) {
  auto content = load(LR"(<Grid>
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel Padding="24,4,24,24" Spacing="12">
    <TextBlock x:Name="Title" Style="{StaticResource SubtitleTextBlockStyle}" TextWrapping="Wrap"/>
    <TextBlock x:Name="Detail" Style="{StaticResource BodyTextBlockStyle}" TextWrapping="Wrap"/>
  </StackPanel>
  <Grid Grid.Row="1" Padding="24" ColumnSpacing="8" Background="{ThemeResource SolidBackgroundFillColorBaseBrush}"
        BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" BorderThickness="0,1,0,0">
    <Grid.ColumnDefinitions><ColumnDefinition/><ColumnDefinition/></Grid.ColumnDefinitions>
    <Button x:Name="Yes" Style="{StaticResource AccentButtonStyle}" HorizontalAlignment="Stretch"/>
    <Button x:Name="No" Grid.Column="1" HorizontalAlignment="Stretch"/>
  </Grid>
</Grid>)");
  part<controls::TextBlock>(content, L"Title").Text(h(title));
  auto d = part<controls::TextBlock>(content, L"Detail");
  d.Text(h(detail));
  if (detail.empty()) d.Visibility(xaml::Visibility::Collapsed);
  auto yes_button = part<controls::Button>(content, L"Yes"), no_button = part<controls::Button>(content, L"No");
  yes_button.Content(winrt::box_value(h(yes)));
  no_button.Content(winrt::box_value(h(no)));
  if (no.empty()) no_button.Visibility(xaml::Visibility::Collapsed), controls::Grid::SetColumnSpan(yes_button, 2);
  auto w = window("LexiPeek", content, 480, 240, t);
  if (auto p = w.AppWindow().Presenter().try_as<OverlappedPresenter>()) p.IsResizable(false), p.IsMaximizable(false), p.IsMinimizable(false);
  auto answered = std::make_shared<bool>(false);
  auto answer = [w, answered, done](bool a) {
    if (*answered) return;
    *answered = true;
    done(a);
    w.Close();
  };
  yes_button.Click([answer](auto &&, auto &&) { answer(true); });
  no_button.Click([answer](auto &&, auto &&) { answer(false); });
  w.Closed([answered, done](auto &&, auto &&) {
    if (!*answered) *answered = true, done(false);
  });
  fit_height(w);
  return w;
}

}  // namespace lexipeek::win
