// Windows 11 Settings' pieces for the Settings and About windows and their panes: setting cards, group headings, icon
// buttons, dialogs, links, work off the window's thread.
#pragma once
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "core_client.hpp"
#include "ui.hpp"

namespace lexipeek::win::page {

namespace controls = xaml::Controls;

controls::FontIcon icon(wchar_t glyph, double size = 16);
// Body (14 px) or another TextBlock style; secondary: the theme's secondary text colour. Wraps.
controls::TextBlock text(const std::string &s, const wchar_t *style = L"BodyTextBlockStyle", bool secondary = false);
// A setting card: the icon (0 none), the title, its one-line description, the control on the right, more under them.
controls::Grid card(wchar_t glyph, const std::string &title, const std::string &about, const xaml::UIElement &control = nullptr,
                    const xaml::UIElement &below = nullptr);
// The same with a control in the icon's place (a check box, a radio button).
controls::Grid card(const xaml::UIElement &lead, const std::string &title, const std::string &about, const xaml::UIElement &control = nullptr,
                    const xaml::UIElement &below = nullptr);
// The card's title and description dimmed (a dictionary switched off).
void dim(const controls::Grid &card);
// A group's title over the cards under it (and a line about it).
controls::StackPanel heading(const std::string &title, const std::string &about, bool first);
controls::Button button(const winrt::hstring &label, std::function<void()> click, bool accent = false);
// A small borderless button with a glyph and its tooltip.
controls::Button icon_button(wchar_t glyph, const winrt::hstring &tip, std::function<void()> click);
// A ToggleSwitch with "On" / "Off" on its left, as Windows 11's Settings; name: the row's title for screen readers.
controls::StackPanel toggle(bool on, bool enabled, std::function<void(bool)> changed, const std::string &name = "");
controls::ProgressBar progress(double value);  // 0…1; < 0 a moving one
// An error or a note over a page (InfoBar), not closable.
controls::InfoBar info(const std::string &message, controls::InfoBarSeverity severity, const std::string &title = "");
// The window an element is in (pickers' owner).
HWND owner(const xaml::UIElement &e);
// Open dialogs (IFileOpenDialog): files to import, a folder; empty when cancelled.
std::vector<std::string> pick_files(HWND owner, const winrt::hstring &title);
std::string pick_folder(HWND owner, const winrt::hstring &title);
// A link that opens in the browser (or a folder in Explorer).
void open(const std::string &target);
controls::HyperlinkButton link(const winrt::hstring &label, const std::string &url);

// A ContentDialog over the element's window, in its theme: done(the button pressed).
controls::ContentDialog dialog(const xaml::UIElement &on, const winrt::hstring &title, const winrt::Windows::Foundation::IInspectable &content,
                               const winrt::hstring &primary, const winrt::hstring &close, const winrt::hstring &secondary = L"");
void show(controls::ContentDialog d, std::function<void(controls::ContentDialogResult)> done = nullptr);
// Delete "…"? and the like: yes on the primary button.
void confirm(const xaml::UIElement &on, const std::string &title, const std::string &detail, const std::string &yes, std::function<void()> then);

// work on a thread of its own, done on the UI thread (the windows live until the process leaves).
template <class R>
void run(const CoreClient &c, std::function<R()> work, std::function<void(R)> done) {
  std::thread([&c, work = std::move(work), done = std::move(done)] {
    auto r = work();
    c.ui([done, r] { done(r); });
  }).detach();
}

// A repeating timer on the UI thread.
winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer every(int ms, std::function<void()> tick);

}  // namespace lexipeek::win::page
