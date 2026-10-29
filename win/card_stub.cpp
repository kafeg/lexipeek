#include "pch.h"
#include "card_stub.hpp"
#include <algorithm>
#include "lexipeek/app.hpp"

namespace lexipeek::win {

namespace controls = xaml::Controls;

namespace {
constexpr int escape_key = 100;  // the pop-up card's, as qt/popup.cpp
}

void CardStub::show(const std::optional<std::string> &text, Source source, const os::Rect &at, bool focus) {
  close();
  shown = text.value_or("");
  unread.clear();
  input_ = !text, source_ = source;
  if (!window) {
    content_ = load(LR"(<StackPanel Padding="16,0,16,16" Spacing="8">
  <TextBlock x:Name="Text" Style="{StaticResource SubtitleTextBlockStyle}" TextWrapping="Wrap" IsTextSelectionEnabled="True"/>
  <TextBlock x:Name="Note" TextWrapping="Wrap" Foreground="{ThemeResource SystemFillColorCautionBrush}"/>
  <TextBox x:Name="Field"/>
  <TextBlock Style="{StaticResource CaptionTextBlockStyle}" Foreground="{ThemeResource TextFillColorSecondaryBrush}" Text="M8 · phase 2"/>
</StackPanel>)");
    auto field = part<controls::TextBox>(content_, L"Field");
    field.KeyDown([this](winrt::Windows::Foundation::IInspectable const &sender, xaml::Input::KeyRoutedEventArgs const &e) {
      if (e.Key() != winrt::Windows::System::VirtualKey::Enter) return;
      shown = winrt::to_string(sender.as<controls::TextBox>().Text());
      draw();
    });
    window = win::window("LexiPeek", content_, 380, 200, theme ? theme() : xaml::ElementTheme::Default);
    window.Closed([this](auto &&, auto &&) {
      open_ = false;
      keys(false);
      window = nullptr;
    });
  }
  window.Content().as<xaml::FrameworkElement>().RequestedTheme(theme ? theme() : xaml::ElementTheme::Default);
  draw();
  // Under the selection (or the cursor), inside the screen there.
  POINT c{};
  GetCursorPos(&c);
  RECT anchor = at.empty() ? RECT{c.x, c.y, c.x, c.y} : RECT{at.x, at.y, at.x + at.w, at.y + at.h};
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(MonitorFromPoint({anchor.left, anchor.bottom}, MONITOR_DEFAULTTONEAREST), &mi);
  auto wr = mi.rcWork;
  auto aw = window.AppWindow();
  auto size = aw.Size();
  int x = std::clamp(static_cast<int>(anchor.left), static_cast<int>(wr.left), std::max<int>(wr.left, wr.right - size.Width));
  int y = anchor.bottom + 6;
  if (y + size.Height > wr.bottom) y = std::max<int>(wr.top, anchor.top - 6 - size.Height);
  aw.Move({x, y});
  aw.Show(focus);
  if (focus) {
    os::activate(hwnd(window));
    part<controls::TextBox>(content_, L"Field").Focus(xaml::FocusState::Programmatic);
  }
  open_ = true;
  keys(true);
}

void CardStub::draw() {
  if (!content_) return;
  auto text = part<controls::TextBlock>(content_, L"Text");
  text.Text(h(shown));
  text.Visibility(shown.empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  auto note = part<controls::TextBlock>(content_, L"Note");
  note.Text(unread.empty() ? winrt::hstring() : tr("card_unread", unread));
  note.Visibility(unread.empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  auto field = part<controls::TextBox>(content_, L"Field");
  field.PlaceholderText(tr("card_input_hint"));
  field.Visibility(input_ ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
}

void CardStub::close() {
  if (!open_) return;
  open_ = false;
  keys(false);
  if (window) window.AppWindow().Hide();
}

// Esc for every app: only while the card is open.
void CardStub::keys(bool on) {
  if (on == keys_) return;
  keys_ = on;
  if (on) os::register_hotkey(escape_key, Hotkey{"Esc", false, false, false, false}, [this] { close(); });
  else os::unregister_hotkey(escape_key);
}

}  // namespace lexipeek::win
