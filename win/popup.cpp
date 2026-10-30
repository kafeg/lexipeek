#include "pch.h"
#include "popup.hpp"
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Input.h>
#include <algorithm>
#include <cmath>

namespace lexipeek::win {

using namespace winrt::Microsoft::UI::Composition::SystemBackdrops;
using winrt::Windows::System::VirtualKey;

namespace {
constexpr int escape_key = 100, pin_key = 101, read_key = 102, gap = 6;

// A window of this process is under the point (the card, its menus, the icon at the selection).
bool ours(POINT p) {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetAncestor(WindowFromPoint(p), GA_ROOT), &pid);
  return pid == GetCurrentProcessId();
}
}  // namespace

CardPopup::CardPopup(CoreClient &client) : model(client), card(model) {
  card.drawn = [this] {
    // Laid out first: then its size.
    winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue([this] { fit(); });
  };
  card.submitted = [this](const std::string &t) {
    if (t.find_first_not_of(" \t\r\n") == std::string::npos) return;
    model.input = false;
    model.source = Source::input;
    model.lookup(t);
  };
  // A click anywhere outside the card closes it; the focused app keeps its click. Short: a tap is quick.
  outside_ = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
  outside_.Interval(std::chrono::milliseconds(15));
  outside_.Tick([this](auto &&, auto &&) {
    bool down = os::mouse_down();
    POINT c{};
    GetCursorPos(&c);
    bool outside = down && !was_down_ && !model.pinned && !ours(c);
    was_down_ = down;
    if (outside) return close();
    keys(!model.pinned);
  });
}

// Borderless with the system's rounded corners and shadow, acrylic as Windows 11's flyouts (always as if active: the card
// never takes the focus), on top, not in Alt+Tab.
void CardPopup::make() {
  window = xaml::Window();
  window.Content(card.root());
  auto aw = window.AppWindow();
  auto p = winrt::Microsoft::UI::Windowing::OverlappedPresenter::Create();
  p.SetBorderAndTitleBar(true, false);
  p.IsResizable(false), p.IsMaximizable(false), p.IsMinimizable(false);
  p.IsAlwaysOnTop(true);
  aw.SetPresenter(p);
  aw.IsShownInSwitchers(false);
  aw.Title(L"LexiPeek");
  if (live && DesktopAcrylicController::IsSupported()) {
    backdrop_ = SystemBackdropConfiguration();
    backdrop_.IsInputActive(true);
    acrylic_ = DesktopAcrylicController();
    acrylic_.AddSystemBackdropTarget(window.as<winrt::Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop>());
    acrylic_.SetSystemBackdropConfiguration(backdrop_);
  } else {
    card.solid = true;
  }
  os::round(hwnd(window));
  auto root = card.root();
  root.KeyDown([this](auto &&, xaml::Input::KeyRoutedEventArgs const &e) { key(e); });
  // A drag on the card's background (not on its buttons or text) moves it.
  root.PointerPressed([this, root](auto &&, xaml::Input::PointerRoutedEventArgs const &e) {
    if (!e.GetCurrentPoint(nullptr).Properties().IsLeftButtonPressed()) return;
    GetCursorPos(&drag_);
    auto at = window.AppWindow().Position();
    start_ = {at.X, at.Y};
    dragging_ = root.CapturePointer(e.Pointer());
  });
  root.PointerMoved([this](auto &&, auto &&) {
    if (!dragging_) return;
    POINT c{};
    GetCursorPos(&c);
    window.AppWindow().Move({start_.x + c.x - drag_.x, start_.y + c.y - drag_.y});
  });
  auto dropped = [this, root](auto &&, xaml::Input::PointerRoutedEventArgs const &e) {
    if (!dragging_) return;
    dragging_ = false;
    root.ReleasePointerCapture(e.Pointer());
    auto at = window.AppWindow().Position();
    if (std::abs(at.X - start_.x) + std::abs(at.Y - start_.y) > 4 && pin_moved && !model.pinned) model.toggle_pin();
  };
  root.PointerReleased(dropped);
  root.PointerCaptureLost(dropped);
  window.Closed([this](auto &&, auto &&) {
    open_ = false;
    keys(false);
    outside_.Stop();
    window = nullptr;
  });
}

void CardPopup::show(const std::optional<std::string> &text, Source source, const os::Rect &at, bool focus) {
  close();
  if (!window) make();
  shown = text.value_or("");
  model.reset();
  card.clear_input();
  model.input = !text;
  POINT c{};
  GetCursorPos(&c);
  RECT anchor = at.empty() ? RECT{c.x, c.y, c.x, c.y} : RECT{at.x, at.y, at.x + at.w, at.y + at.h};
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(MonitorFromPoint({anchor.left, anchor.bottom}, MONITOR_DEFAULTTONEAREST), &mi);
  screen_ = mi.rcWork;
  auto aw = window.AppWindow();
  aw.Move({anchor.left, anchor.bottom});  // on the anchor's screen first: its DPI
  scale_ = GetDpiForWindow(hwnd(window)) / 96.0;
  card.screen = (screen_.bottom - screen_.top) / scale_;
  auto t = theme ? theme() : xaml::ElementTheme::Default;
  card.root().RequestedTheme(t);
  if (backdrop_) {
    bool dark = card.root().ActualTheme() == xaml::ElementTheme::Dark;
    backdrop_.Theme(dark ? SystemBackdropTheme::Dark : SystemBackdropTheme::Light);
  }
  card.rebuild();
  fit();
  // Under the anchor (the selection or the cursor), above it when there is no room, inside the screen.
  auto size = aw.Size();
  int g = static_cast<int>(gap * scale_);
  int x = anchor.left, y = anchor.bottom + g;
  if (y + size.Height > screen_.bottom) y = anchor.top - g - size.Height;
  x = std::clamp<int>(x, screen_.left, std::max<int>(screen_.left, screen_.right - size.Width));
  y = std::clamp<int>(y, screen_.top, std::max<int>(screen_.top, screen_.bottom - size.Height));
  aw.Move({x, y});
  model.source = source;
  if (text) model.lookup(*text);
  auto h = hwnd(window);
  auto ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
  SetWindowLongPtrW(h, GWL_EXSTYLE, focus ? ex & ~WS_EX_NOACTIVATE : ex | WS_EX_NOACTIVATE);
  aw.Show(focus);
  open_ = true;
  if (focus) {
    os::activate(h);
    card.focus_field();
  }
  if (live) {
    keys(true);
    was_down_ = os::mouse_down();
    outside_.Start();
  }
}

void CardPopup::close() {
  if (!open_) return;
  open_ = false;
  outside_.Stop();
  keys(false);
  if (window) window.AppWindow().Hide();
  model.pinned = false;
  model.cancel();
}

void CardPopup::keys(bool on) {
  if (on == keys_) return;
  keys_ = on;
  if (on) {
    os::register_hotkey(escape_key, Hotkey{"Esc", false, false, false, false}, [this] { close(); });
    os::register_hotkey(pin_key, Hotkey{"P", false, false, true, false}, [this] { model.toggle_pin(); });
    os::register_hotkey(read_key, Hotkey{".", false, false, true, false}, [this] { model.read(); });
  } else {
    os::unregister_hotkey(escape_key);
    os::unregister_hotkey(pin_key);
    os::unregister_hotkey(read_key);
  }
}

// Its height follows the card (while something loads it keeps a taller one: no jumping); the top edge stays, inside the screen.
void CardPopup::fit() {
  if (!window) return;
  auto root = card.root();
  root.Measure({INFINITY, INFINITY});
  auto d = root.DesiredSize();
  if (d.Width <= 0 || d.Height <= 0) return;
  auto aw = window.AppWindow();
  int w = static_cast<int>(std::ceil(d.Width * scale_)), h = static_cast<int>(std::ceil(d.Height * scale_));
  // The frame by the window's own rectangles (AppWindow's client size counts a title bar this window has not).
  RECT wr{}, cr{};
  GetWindowRect(hwnd(window), &wr);
  GetClientRect(hwnd(window), &cr);
  if (open_ && model.busy()) h = std::max<int>(h, cr.bottom);
  aw.Resize({w + (wr.right - wr.left) - cr.right, h + (wr.bottom - wr.top) - cr.bottom});
  if (!open_) return;
  // Typing done: the card's own keys (←/→, Ctrl+S, Backspace) once its field is gone.
  if (GetForegroundWindow() == hwnd(window) && !model.input) root.as<xaml::Controls::Control>().Focus(xaml::FocusState::Programmatic);
  auto at = aw.Position();
  auto size = aw.Size();
  int x = std::min<int>(at.X, screen_.right - size.Width), y = std::min<int>(at.Y, screen_.bottom - size.Height);
  if (x != at.X || y != at.Y) aw.Move({std::max<int>(x, screen_.left), std::max<int>(y, screen_.top)});
}

// With the focus (typing): Esc, ←/→ tabs, Ctrl+S ☆, Ctrl+P the pin, Ctrl+. read aloud, Backspace back (a text field keeps
// its own keys).
void CardPopup::key(const xaml::Input::KeyRoutedEventArgs &e) {
  bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  auto k = e.Key();
  if (k == VirtualKey::Escape) return e.Handled(true), close();
  if (!model.has_card()) return;
  if (ctrl && k == VirtualKey::S) model.toggle_saved();
  else if (ctrl && k == VirtualKey::P) model.toggle_pin();
  else if (ctrl && static_cast<int>(k) == VK_OEM_PERIOD) model.read();
  else if (k == VirtualKey::Left) model.next_tab(-1);
  else if (k == VirtualKey::Right) model.next_tab(1);
  else if (k == VirtualKey::Back) model.back();
  else return;
  e.Handled(true);
}

}  // namespace lexipeek::win
