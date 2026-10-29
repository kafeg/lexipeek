#include "pch.h"
#include "selection_icon.hpp"
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <thread>
#include "lexipeek/app.hpp"
#include "resource.h"

namespace lexipeek::win {

namespace controls = xaml::Controls;

namespace {
constexpr int size = 28;  // DIPs

#pragma comment(lib, "comctl32.lib")

// No minimum size (a XAML window is at least ~136 × 39 px otherwise).
LRESULT CALLBACK tiny(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
  if (m == WM_GETMINMAXINFO) {
    auto r = DefSubclassProc(h, m, w, l);
    reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {1, 1};
    return r;
  }
  return DefSubclassProc(h, m, w, l);
}

bool ours(POINT p) {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetAncestor(WindowFromPoint(p), GA_ROOT), &pid);
  return pid == GetCurrentProcessId();
}

}  // namespace

SelectionIcon::SelectionIcon(CoreClient &client) : c_(client) {
  auto q = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
  hide_ = q.CreateTimer();
  hide_.IsRepeating(false);
  hide_.Interval(std::chrono::seconds(4));
  hide_.Tick([this](auto &&, auto &&) { close(); });
  // The app updates its selection after the mouse-up; read off the window's thread (an app may take a while to answer).
  read_ = q.CreateTimer();
  read_.IsRepeating(false);
  read_.Interval(std::chrono::milliseconds(60));
  read_.Tick([this](auto &&, auto &&) {
    if (reading_.exchange(true)) return;
    std::thread([this] {
      auto s = os::selection();
      c_.ui([this, s] {
        reading_ = false;
        if (s) show_at(*s, up_at_);
      });
    }).detach();
  });
}

void SelectionIcon::set_enabled(bool on, std::vector<std::string> off_apps) {
  off_apps_ = std::move(off_apps);
  close();
  os::watch_input(on ? std::function<void(os::Input)>([this](os::Input i) { input(i); }) : nullptr);
}

void SelectionIcon::input(os::Input i) {
  POINT at{};
  GetCursorPos(&at);
  if (i == os::Input::down) {
    down_at_ = at;
    if (!ours(at)) close();
    return;
  }
  if (i == os::Input::other) return close();
  // Up: a drag or a double click in another app is a new selection.
  auto now = GetTickCount64();
  bool twice = now - last_up_ < GetDoubleClickTime();
  last_up_ = now;
  bool dragged = std::abs(at.x - down_at_.x) + std::abs(at.y - down_at_.y) > 6;
  // A drag on a window of ours (moving the card) is not a selection in another app.
  if (!(dragged || twice) || os::front_is_mine() || ours(at) || ours(down_at_)) return;
  if (std::find(off_apps_.begin(), off_apps_.end(), os::front_app()) != off_apps_.end()) return;
  up_at_ = at;
  read_.Stop();
  read_.Start();
}

// A small button with the app's icon by the cursor, above it; it takes no focus.
void SelectionIcon::show_at(const os::Selected &s, POINT at) {
  found_ = s;
  if (!window) {
    window = xaml::Window();
    auto aw = window.AppWindow();
    // A context menu's frame: none.
    auto p = winrt::Microsoft::UI::Windowing::OverlappedPresenter::CreateForContextMenu();
    p.IsAlwaysOnTop(true);
    aw.SetPresenter(p);
    aw.IsShownInSwitchers(false);
    aw.Title(L"LexiPeek");
    auto h = hwnd(window);
    SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
    SetWindowSubclass(h, tiny, 1, 0);
    os::round(h);
    double scale = GetDpiForWindow(h) / 96.0;
    auto root = load(LR"(<Grid Background="{ThemeResource SolidBackgroundFillColorQuarternaryBrush}">
  <Button x:Name="Open" Width="28" Height="28" Padding="4" Background="Transparent" BorderThickness="0">
    <Image x:Name="Icon" Width="20" Height="20"/>
  </Button>
</Grid>)");
    part<controls::Image>(root, L"Icon").Source(app_icon(static_cast<int>(std::lround(20 * scale))));
    auto open_button = part<controls::Button>(root, L"Open");
    controls::ToolTipService::SetToolTip(open_button, winrt::box_value(tr("lookup_service")));
    open_button.Click([this](auto &&, auto &&) {
      close();
      if (open) open(found_);
    });
    window.Content(root);
    aw.ResizeClient({static_cast<int32_t>(std::lround(size * scale)), static_cast<int32_t>(std::lround(size * scale))});
    window.Closed([this](auto &&, auto &&) { window = nullptr; });
  }
  auto aw = window.AppWindow();
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &mi);
  auto g = mi.rcWork;
  auto sz = aw.Size();
  double scale = GetDpiForWindow(hwnd(window)) / 96.0;
  aw.Move({std::min<int>(at.x + static_cast<int>(8 * scale), g.right - sz.Width), std::max<int>(at.y - static_cast<int>(10 * scale) - sz.Height, g.top)});
  aw.Show(false);
  hide_.Stop();
  hide_.Start();
}

void SelectionIcon::close() {
  if (hide_) hide_.Stop();
  if (window) window.AppWindow().Hide();
}

}  // namespace lexipeek::win
