#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "resource.h"
#include "ui.hpp"

namespace xaml = winrt::Microsoft::UI::Xaml;
using namespace lexipeek::win;

namespace winrt::LexiPeek::implementation {

void MainWindow::InitializeComponent() {
  MainWindowT::InitializeComponent();
  // Mica where the system has it (Windows 11); Windows 10 keeps the theme's solid background.
  if (Microsoft::UI::Composition::SystemBackdrops::MicaController::IsSupported()) SystemBackdrop(xaml::Media::MicaBackdrop()), Root().Background(nullptr);
  ExtendsContentIntoTitleBar(true);
  SetTitleBar(TitleBar());
  AppWindow().TitleBar().PreferredHeightOption(Microsoft::UI::Windowing::TitleBarHeightOption::Tall);
  AppWindow().SetIcon(Microsoft::UI::GetIconIdFromIcon(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP))));
  double scale = GetDpiForWindow(hwnd(*this)) / 96.0;
  AppWindow().Resize({static_cast<int32_t>(1000 * scale), static_cast<int32_t>(680 * scale)});
  Nav().ItemInvoked([this](auto &&, xaml::Controls::NavigationViewItemInvokedEventArgs const &e) {
    auto tag = unbox_value_or<hstring>(e.InvokedItemContainer().Tag(), L"");
    if (tag == L"settings" && open_settings) open_settings();
    if (tag == L"about" && open_about) open_about();
  });
  Nav().SelectionChanged([this](auto &&, xaml::Controls::NavigationViewSelectionChangedEventArgs const &e) {
    auto item = e.SelectedItemContainer().try_as<xaml::Controls::NavigationViewItem>();
    if (!item) return;
    PageTitle().Text(unbox_value_or<hstring>(item.Content(), L""));
    PageIcon().Glyph(item.Icon().as<xaml::Controls::FontIcon>().Glyph());
  });
}

void MainWindow::refresh(xaml::ElementTheme theme) {
  LookupItem().Content(box_value(tr("win_lookup")));
  WordsItem().Content(box_value(tr("list_words")));
  PhrasesItem().Content(box_value(tr("list_phrases")));
  HistoryItem().Content(box_value(tr("list_history")));
  HelpItem().Content(box_value(tr("help_t")));
  SettingsItem().Content(box_value(tr("settings_more")));
  AboutItem().Content(box_value(tr("menu_about")));
  if (auto item = Nav().SelectedItem().try_as<xaml::Controls::NavigationViewItem>()) PageTitle().Text(unbox_value_or<hstring>(item.Content(), L""));
  Root().RequestedTheme(theme);
  AppWindow().TitleBar().PreferredTheme(theme == xaml::ElementTheme::Light  ? Microsoft::UI::Windowing::TitleBarTheme::Light
                                        : theme == xaml::ElementTheme::Dark ? Microsoft::UI::Windowing::TitleBarTheme::Dark
                                                                            : Microsoft::UI::Windowing::TitleBarTheme::UseDefaultAppMode);
}

}  // namespace winrt::LexiPeek::implementation
