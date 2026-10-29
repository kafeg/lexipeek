// Shared small pieces of the WinUI shell: texts, windows in Windows 11's look, the question window.
#pragma once
#include <functional>
#include <string>

namespace lexipeek::win {

namespace xaml = winrt::Microsoft::UI::Xaml;

// The core's texts (l10n) as WinRT strings; any UTF-8 string too.
winrt::hstring h(const std::string &s);
winrt::hstring tr(const char *key);
winrt::hstring tr(const char *key, const std::string &a);
winrt::hstring tr(const char *key, const std::string &a, const std::string &b);

// The theme Settings → General picks ("light", "dark", else the system's).
xaml::ElementTheme theme(const std::string &setting);

// XAML text into elements (the presentation namespace is added); its x:Name parts by FindName.
xaml::FrameworkElement load(const std::wstring &xaml);
template <class T>
T part(const xaml::FrameworkElement &root, const wchar_t *name) {
  return root.FindName(name).as<T>();
}

HWND hwnd(const xaml::Window &w);
// A window of the app: Mica (Windows 11; the theme's background before it), the content in the title bar under the
// title, the app's icon, width × height DIPs. Not shown yet.
xaml::Window window(const std::string &title, const xaml::UIElement &content, int width, int height, xaml::ElementTheme theme);
// In the middle of the screen with the cursor (where it already is when it is open), in front with the focus.
void present(const xaml::Window &w);
// The window's height fits its content (once it is laid out).
void fit_height(const xaml::Window &w);

// A question as Windows 11's dialogs ask it: title, detail, the accent button (yes) and the other (no; "" none).
// done(true) for yes; closing it is no.
xaml::Window ask(const std::string &title, const std::string &detail, const std::string &yes, const std::string &no,
                 xaml::ElementTheme theme, std::function<void(bool)> done);

}  // namespace lexipeek::win
