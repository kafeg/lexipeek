#include "pch.h"
#include "shell.hpp"
#include <algorithm>
#include <thread>
#include "App.xaml.h"
#include "ui.hpp"

namespace lexipeek::win {

namespace controls = xaml::Controls;
using controls::Primitives::FlyoutPlacementMode;

namespace {
constexpr int translate_key = 1, input_key = 2;

winrt::LexiPeek::implementation::MainWindow *self(const winrt::LexiPeek::MainWindow &w) {
  return winrt::get_self<winrt::LexiPeek::implementation::MainWindow>(w);
}
}  // namespace

// A host the size of a pixel at the click (no frame, not in Alt+Tab): the menu opens from it beyond its bounds, toward
// the screen's middle, and goes when the host loses the focus.
void MenuHost::show(const controls::MenuFlyout &menu, POINT at, xaml::ElementTheme theme) {
  if (!window_) {
    root_ = controls::Grid();
    window_ = xaml::Window();
    window_.Content(root_);
    auto aw = window_.AppWindow();
    aw.SetPresenter(winrt::Microsoft::UI::Windowing::OverlappedPresenter::CreateForContextMenu());
    aw.IsShownInSwitchers(false);
    root_.Loaded([this](auto &&, auto &&) {
      if (pending_) open_at();
    });
    window_.Activated([this](auto &&, xaml::WindowActivatedEventArgs const &e) {
      if (e.WindowActivationState() == xaml::WindowActivationState::Deactivated && menu_) menu_.Hide();
    });
  }
  hide();
  menu_ = menu;
  menu_.ShouldConstrainToRootBounds(false);
  menu_.Closed([this](winrt::Windows::Foundation::IInspectable const &sender, auto &&) {
    if (menu_ && sender == menu_) window_.AppWindow().Hide();  // not an earlier menu's late Closed
  });
  root_.RequestedTheme(theme);
  at_ = at;
  pending_ = true;
  window_.AppWindow().MoveAndResize({at.x, at.y, 1, 1});
  window_.Activate();
  SetForegroundWindow(hwnd(window_));
  if (root_.IsLoaded()) open_at();
}

void MenuHost::open_at() {
  pending_ = false;
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(MonitorFromPoint(at_, MONITOR_DEFAULTTONEAREST), &mi);
  auto r = mi.rcMonitor;
  bool up = at_.y > (r.top + r.bottom) / 2, left = at_.x > (r.left + r.right) / 2;
  controls::Primitives::FlyoutShowOptions o;
  o.Placement(up ? (left ? FlyoutPlacementMode::TopEdgeAlignedRight : FlyoutPlacementMode::TopEdgeAlignedLeft)
                 : (left ? FlyoutPlacementMode::BottomEdgeAlignedRight : FlyoutPlacementMode::BottomEdgeAlignedLeft));
  menu_.ShowAt(root_, o);
}

xaml::UIElement MenuHost::shown() const {
  if (!root_ || !root_.XamlRoot()) return nullptr;
  for (auto p : xaml::Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root_.XamlRoot()))
    if (p.Child()) return p.Child();
  return nullptr;
}

void MenuHost::hide() {
  if (menu_ && menu_.IsOpen()) menu_.Hide();
}

void MenuHost::close() {
  hide();
  menu_ = nullptr;  // its late Closed finds no host to hide
  if (window_) window_.Close();
  window_ = nullptr;
}

Shell::Shell(CoreClient &client, bool live) : card(client), icon(client), main_keep(client), c_(client), live_(live) {
  main_keep.card.key = [this] { return os::secret(app::ai_provider(c_.data())); };
  card.theme = [this] { return theme(); };
  card.live = live;
  card.model.key = [this] { return os::secret(app::ai_provider(c_.data())); };
  card.card.open_pane = [this](int section, int tab) { open_settings(section, tab); };
  icon.open = [this](const os::Selected &s) { card.show(s.text, Source::hotkey, s.bounds, false); };
  tray_.activated = [this] { open_main(); };
  tray_.menu = [this](POINT p) { show_menu(p); };
  // Light or dark switched: the taskbar's glyph follows.
  tray_.theme_changed = [this] { update_tray(); };
  // A pack installed or removed anywhere (Settings, the first run, the CLI): the icon follows.
  health_ = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
  health_.Interval(std::chrono::seconds(2));
  health_.Tick([this](auto &&, auto &&) {
    if (app::has_pair(c_.data()) != pair_) update_tray();
    // As the Mac's memory pressure: once when memory gets low, off the window's thread.
    bool low = os::memory_low();
    if (low && !low_) c_.worker.start([e = c_.engine] { e.release(); });
    low_ = low;
  });
  health_.Start();
  apply();
}

xaml::ElementTheme Shell::theme() const { return forced_theme ? *forced_theme : win::theme(s_.theme); }

void Shell::apply() {
  // The app's language (Settings → General) at once, as on the Mac.
  app::set_language(c_.data(), os::ui_language());
  s_ = app::settings(c_.data());
  register_hotkeys();
  card.pin_moved = s_.pin_moved;
  if (live_) icon.set_enabled(s_.selection_icon, s_.ignore_apps);
  c_.configure();
  update_tray();
  if (main) self(main)->refresh(theme());
  if (settings_ui) settings_ui->refresh(theme());
  if (about_ui) about_ui->refresh(theme());
  if (first_ui) first_ui->refresh(theme());
}

void Shell::register_hotkeys() {
  if (!live_) return;
  bool a = os::register_hotkey(translate_key, s_.lookup, [this] { translate_selection(); });
  bool b = os::register_hotkey(input_key, s_.input, [this] { type_text(); });
  keys_ = a && b;
  lookup_error_ = a ? "" : app::text("hk_taken"), input_error_ = b ? "" : app::text("hk_taken");
  if (settings_ui) settings_ui->hotkey_errors(lookup_error_, input_error_);
}

// With a card open: another selection opens its card at once; the same one or none closes it.
void Shell::translate_selection() {
  auto app = os::front_app();
  if (std::find(s_.ignore_apps.begin(), s_.ignore_apps.end(), app) != s_.ignore_apps.end()) return card.close();
  if (reading_) return;
  reading_ = true;
  reading_open_ = card.open(), reading_app_ = app;
  // Read off the window's thread (an app may take a while to answer).
  std::thread([this] {
    auto s = os::selection();
    c_.ui([this, s] {
      reading_ = false;
      selection_read(s);
    });
  }).detach();
}

void Shell::selection_read(std::optional<os::Selected> s) {
  auto step = app::hotkey_step(reading_open_, s.has_value(), s ? s->text : "", card.shown);
  if (step == app::HotkeyStep::close) return card.close();
  if (step == app::HotkeyStep::show) return card.show(s->text, Source::hotkey, s->bounds, false);
  // Nothing read: say which app, type meanwhile.
  card.show(std::nullopt, Source::input, {}, true);
  if (!reading_app_.empty()) {
    card.model.unread = reading_app_;
    card.redraw();
  }
}

void Shell::update_tray() {
  pair_ = app::has_pair(c_.data());
  bool pair = pair_ && !force_warnings, keys = keys_ && !force_warnings;
  if (live_)
    tray_.show(!pair || !keys, std::wstring(!pair ? L"LexiPeek — " + tr("nopack_t") : !keys ? L"LexiPeek — " + tr("hk_taken_t") : winrt::hstring(L"LexiPeek")));
  if (main) self(main)->refresh(theme());
}

void Shell::type_text() {
  if (card.open() && card.input()) return card.close();
  card.show(std::nullopt, Source::input, {}, true);
}

void Shell::open_main() {
  if (!main) {
    main = winrt::make<winrt::LexiPeek::implementation::MainWindow>();
    self(main)->open_pane = [this](int section, int tab) { open_settings(section, tab); };
    self(main)->open_about = [this] { open_about(); };
    self(main)->open_first_run = [this] { open_first_run(); };
    self(main)->start(main_keep);
    main.Closed([this](auto &&, auto &&) { main = nullptr; });
  }
  self(main)->refresh(theme());
  present(main);
}

// section -1: as it was left.
void Shell::open_settings(int section, int tab) {
  if (!settings_ui) {
    settings_ui = new SettingsWindow(c_, theme());
    settings_ui->changed = [this] { apply(); };
    settings_ui->open_first_run = [this] { open_first_run(); };
    // Recording a hotkey: ours off, or pressing them would fire them instead.
    settings_ui->recording = [this](bool on) {
      if (!on) return register_hotkeys();
      os::unregister_hotkey(translate_key), os::unregister_hotkey(input_key);
    };
    settings = settings_ui->window;
    // Closed by code (not its X, which hides it): made again next time; the old one may still finish a download.
    settings.Closed([this](auto &&, auto &&) { settings = nullptr, settings_ui = nullptr; });
  }
  settings_ui->hotkey_errors(lookup_error_, input_error_);
  if (section >= 0) settings_ui->select(static_cast<SettingsSection>(section), tab);
  settings_ui->refresh(theme());
  present(settings);
}

void Shell::open_about() {
  if (!about_ui) {
    about_ui = new AboutWindow(c_, theme()), about = about_ui->window;
    about.Closed([this](auto &&, auto &&) { about = nullptr, about_ui = nullptr; });
  }
  about_ui->refresh(theme());
  present(about);
}
// The first pair and its download, the hotkey; closed, it opens at launch only while no pair is installed.
void Shell::open_first_run(bool fetch) {
  if (!first_ui) {
    first_ui = new FirstRunWindow(c_, theme()), first = first_ui->window;
    first_ui->finished = [this] { apply(); };
    first.Closed([this](auto &&, auto &&) { first = nullptr, first_ui = nullptr; });
  }
  first_ui->state.hotkey = app::hotkey_label(s_.lookup);
  first_ui->state.input = app::hotkey_label(s_.input);
  first_ui->refresh(theme());
  present(first);
  if (fetch) first_ui->start();
}

// lexipeek://lookup?text=… (the card with it) and lexipeek://input (the card to type in); a launch without one opens the window.
void Shell::handle(const std::vector<std::string> &args) {
  for (auto &a : args) {
    if (_strnicmp(a.c_str(), "lexipeek:", 9) != 0) continue;
    auto l = app::link(a);
    if (l.input) return card.show(std::nullopt, Source::input, {}, true);
    if (l.lookup) return card.show(l.text, Source::url, {}, false);
    return;
  }
  open_main();
}

void Shell::show_menu(POINT at) { menu.show(fill_menu(), at, theme()); }

// Built on every open: Recents and the memory line stay current (qt/shell.cpp's items).
controls::MenuFlyout Shell::fill_menu() {
  controls::MenuFlyout m;
  auto items = m.Items();
  auto item = [](const winrt::hstring &text, std::function<void()> click, wchar_t glyph = 0) {
    controls::MenuFlyoutItem i;
    i.Text(text);
    if (glyph) {
      controls::FontIcon f;
      f.Glyph(winrt::hstring(std::wstring(1, glyph)));
      i.Icon(f);
    }
    if (click) i.Click([click](auto &&, auto &&) { click(); });
    else i.IsEnabled(false);
    return i;
  };
  auto line = [] { return controls::MenuFlyoutSeparator(); };
  if (!pair_ || force_warnings) {
    items.Append(item(tr("menu_setup"), [this] { open_first_run(); }, 0xE7BA));
    items.Append(item(tr("nopack_t"), nullptr));
    items.Append(line());
  }
  if (!keys_ || force_warnings) {
    items.Append(item(tr("menu_hotkeys"), [this] { open_settings(static_cast<int>(SettingsSection::general), 1); }, 0xE7BA));
    items.Append(item(tr("hk_taken_t"), nullptr));
    items.Append(line());
  }
  items.Append(item(tr("menu_open"), [this] { open_main(); }));
  controls::MenuFlyoutSubItem recents;
  recents.Text(tr("recents"));
  for (auto &r : c_.engine.recents(app::recents_shown())) {
    auto title = r.meaning.empty() ? r.text : r.text + " — " + r.meaning;
    auto text = r.text;
    recents.Items().Append(item(h(app::menu_line(title)), [this, text] { card.show(text, Source::input, {}, false); }));
  }
  if (recents.Items().Size() == 0) recents.Items().Append(item(tr("nothing_yet"), nullptr));
  items.Append(recents);
  items.Append(line());
  auto mem = c_.engine.memory();
  items.Append(item(h(mem.line), nullptr));
  if (mem.held) {
    items.Append(item(h(mem.detail), nullptr));
    items.Append(item(h(mem.unload), [this] { c_.engine.release(); }));
  }
  items.Append(line());
  items.Append(item(tr("menu_answer", app::language(s_.answer)), nullptr));
  items.Append(line());
  items.Append(item(tr("settings_more"), [this] { open_settings(-1, 0); }));
  items.Append(item(tr("menu_about"), [this] { open_about(); }));
  items.Append(item(tr("quit"), [this] { quit(); }));
  return m;
}

// Every window closed before the app ends (a copy each: its Closed handler lets go of the slot).
void Shell::quit(int code) {
  health_.Stop();
  menu.close();
  card.close();
  for (auto w : {card.window, icon.window, settings, about, first, main.try_as<xaml::Window>()})
    if (w) w.Close();
  tray_.remove();  // gone at once, not when Explorer next looks
  winrt::LexiPeek::implementation::App::quit(code);
}

}  // namespace lexipeek::win
