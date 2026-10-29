#include "pch.h"
#include "snapshots.hpp"
#include <wincodec.h>
#include <filesystem>
#include <memory>
#include <vector>
#include "ui.hpp"

namespace lexipeek::win {

namespace {

// Premultiplied BGRA over the window's own base (Mica's or the theme's) as a PNG (WIC).
bool save(std::vector<uint8_t> px, int w, int h, bool dark, const std::filesystem::path &file) {
  const uint8_t base = dark ? 0x20 : 0xF3;
  for (size_t i = 0; i + 3 < px.size(); i += 4) {
    int a = px[i + 3];
    for (int c = 0; c < 3; ++c) px[i + c] = static_cast<uint8_t>(px[i + c] + base * (255 - a) / 255);
    px[i + 3] = 255;
  }
  try {
    auto wic = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICBitmap> bitmap;
    winrt::check_hresult(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, w * 4, static_cast<UINT>(px.size()), px.data(), bitmap.put()));
    winrt::com_ptr<IWICStream> stream;
    winrt::check_hresult(wic->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
    winrt::check_hresult(frame->Initialize(nullptr));
    winrt::check_hresult(frame->WriteSource(bitmap.get(), nullptr));
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(encoder->Commit());
    return true;
  } catch (winrt::hresult_error const &) {
    return false;
  }
}

// What an element draws (RenderTargetBitmap: XAML's own rendering, whatever the screen shows) into a PNG; done(ok).
winrt::fire_and_forget render(xaml::UIElement e, std::filesystem::path file, bool dark, std::function<void(bool)> done) {
  bool ok = false;
  try {
    if (e) {
      xaml::Media::Imaging::RenderTargetBitmap rtb;
      co_await rtb.RenderAsync(e);
      auto pixels = co_await rtb.GetPixelsAsync();
      ok = save({pixels.data(), pixels.data() + pixels.Length()}, rtb.PixelWidth(), rtb.PixelHeight(), dark, file);
    }
  } catch (winrt::hresult_error const &) {
  }
  done(ok);
}

xaml::UIElement content(const xaml::Window &w) { return w ? w.Content() : nullptr; }

struct Case {
  std::string name;
  std::function<void(Shell &)> show, hide;
  std::function<xaml::UIElement(Shell &)> drawn;  // what is rendered
  std::function<bool(Shell &)> end = nullptr;     // scrolled to its end: true when it scrolls (<name>-end too)
};

// Settings' page by a snapshot name, settings-<section>[-<tab>] (snapshot-cases.sh's settings_shots), or about[-licenses];
// hidden after (closed by code, the window would be made again).
Case pane_case(const std::string &name) {
  auto hide = [](xaml::Window w) {
    if (w) w.AppWindow().Hide();
  };
  if (name.starts_with("about")) {
    int tab = name.starts_with("about-licenses") ? 1 : 0;
    return {name, [tab](Shell &s) { s.open_about(), s.about_ui->select(tab); }, [hide](Shell &s) { hide(s.about); },
            [](Shell &s) { return content(s.about); }};
  }
  static const char *keys[] = {"languages", "card", "ai", "general"};
  SettingsSection sec = SettingsSection::general;
  int tab = 0;
  for (auto x : shown_sections) {
    auto base = std::string("settings-") + keys[static_cast<int>(x)];
    if (!name.starts_with(base)) continue;
    sec = x;
    auto tabs = settings_tab_keys(x);
    for (size_t i = 0; i < tabs.size(); ++i)
      if (name.starts_with(base + "-" + tabs[i])) tab = static_cast<int>(i);
  }
  return {name, [sec, tab](Shell &s) { s.open_settings(static_cast<int>(sec), tab); }, [hide](Shell &s) { hide(s.settings); },
          [](Shell &s) { return content(s.settings); }, [](Shell &s) {
            s.settings_ui->scroll_to_end();
            return s.settings_ui->scrolls();
          }};
}

// Where the tray's menu opens: the taskbar's corner of the main screen (the icon itself may be in the overflow).
POINT tray_corner() {
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
  return {mi.rcWork.right - 120, mi.rcWork.bottom - 4};
}

std::vector<Case> cases() {
  // A copy closes it: its Closed handler lets go of the slot.
  auto close = [](xaml::Window w) {
    if (w) w.Close();
  };
  auto asked = std::make_shared<xaml::Window>(nullptr);
  auto menu = [](Shell &s) { return s.menu.shown(); };
  auto card = [](Shell &s) { return content(s.card.window); };
  std::vector<Case> all{
      {"menu", [](Shell &s) { s.show_menu(tray_corner()); }, [](Shell &s) { s.menu.hide(); }, menu},
      {"menu-warnings",
       [](Shell &s) {
         s.force_warnings = true;
         s.show_menu(tray_corner());
       },
       [](Shell &s) {
         s.menu.hide();
         s.force_warnings = false;
       },
       menu},
      {"card", [](Shell &s) { s.card.show("das Haus", Source::hotkey, {600, 300, 120, 24}, false); }, [](Shell &s) { s.card.close(); }, card},
      {"card-input",
       [](Shell &s) {
         s.card.show(std::nullopt, Source::input, {600, 300, 120, 24}, true);
         s.card.unread = "notepad.exe";
         s.card.redraw();
       },
       [](Shell &s) { s.card.close(); },
       card},
      {"main", [](Shell &s) { s.open_main(); }, [close](Shell &s) { close(s.main.try_as<xaml::Window>()); },
       [](Shell &s) { return s.main ? s.main.Content() : nullptr; }},
      {"firstrun", [](Shell &s) { s.open_first_run(); }, [close](Shell &s) { close(s.first); }, [](Shell &s) { return content(s.first); }},
      {"install",
       [asked](Shell &s) {
         *asked = ask(app::text("inst_q_t"), app::text("inst_q_d"), app::text("inst_btn"), app::text("inst_run"), s.theme(), [](bool) {});
         present(*asked);
       },
       [asked, close](Shell &) { close(*asked); },
       [asked](Shell &) { return content(*asked); }},
  };
  // Every Settings section × tab, About's tabs.
  static const char *keys[] = {"languages", "card", "ai", "general"};
  for (auto sec : shown_sections) {
    auto tabs = settings_tab_keys(sec);
    for (size_t tab = 0; tab < std::max<size_t>(1, tabs.size()); ++tab)
      all.push_back(pane_case(std::string("settings-") + keys[static_cast<int>(sec)] + (tabs.empty() ? "" : "-" + tabs[tab])));
  }
  all.push_back(pane_case("about"));
  all.push_back(pane_case("about-licenses"));
  return all;
}

struct Run {
  Shell &shell;
  std::filesystem::path dir;
  std::function<void(int)> done;
  std::vector<Case> all = cases();
  size_t step = 0;
  int failed = 0;
  std::vector<winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer> timers;  // kept: one may still be in its Tick

  void next() {
    if (step == all.size() * 2) return done(failed);
    auto &c = all[step % all.size()];
    bool dark = step >= all.size();
    shell.forced_theme = dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
    c.show(shell);
    // Drawn, animations over: then its picture.
    after(1500, [this, &c, dark] {
      auto file = dir / (c.name + (dark ? "-dark.png" : "-light.png"));
      render(c.drawn(shell), file, dark, [this, &c, dark](bool ok) {
        failed += !ok;
        auto finish = [this, &c] {
          c.hide(shell);
          ++step;
          after(500, [this] { next(); });
        };
        if (!c.end || !c.end(shell)) return finish();
        after(500, [this, &c, dark, finish] {
          render(c.drawn(shell), dir / (c.name + (dark ? "-dark-end.png" : "-light-end.png")), dark, [this, finish](bool ok) {
            failed += !ok;
            finish();
          });
        });
      });
    });
  }

  void after(int ms, std::function<void()> f) {
    auto t = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
    t.IsRepeating(false);
    t.Interval(std::chrono::milliseconds(ms));
    t.Tick([f](auto &&, auto &&) { f(); });
    t.Start();
    timers.push_back(t);
  }
};

}  // namespace

void snapshots(Shell &shell, const std::string &dir, std::function<void(int)> done) {
  std::filesystem::path out(winrt::to_hstring(dir).c_str());
  std::filesystem::create_directories(out);
  auto *run = new Run{shell, out, std::move(done)};  // lives until the process leaves
  run->next();
}

}  // namespace lexipeek::win
