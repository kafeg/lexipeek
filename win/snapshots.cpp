#include "pch.h"
#include "snapshots.hpp"
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
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
  std::function<bool(Shell &)> quiet = [](Shell &) { return true; };   // drawn for good (nothing on its way)
  std::function<bool(Shell &)> step = [](Shell &) { return false; };   // once quiet: what is still asked done (true), or final
  std::function<bool(Shell &)> end = nullptr;  // scrolled to its end: true when it scrolls (<name>-end too)
  int wait = 1500;  // ms at least: drawn, animations over
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
  Case c{name, [sec, tab](Shell &s) { s.open_settings(static_cast<int>(sec), tab); }, [hide](Shell &s) { hide(s.settings); },
         [](Shell &s) { return content(s.settings); }};
  c.end = [](Shell &s) {
    s.settings_ui->scroll_to_end();
    return s.settings_ui->scrolls();
  };
  return c;
}


// Where the tray's menu opens: the taskbar's corner of the main screen (the icon itself may be in the overflow).
POINT tray_corner() {
  MONITORINFO mi{};
  mi.cbSize = sizeof mi;
  GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
  return {mi.rcWork.right - 120, mi.rcWork.bottom - 4};
}

std::filesystem::path path(const std::string &s) { return std::filesystem::path(winrt::to_hstring(s).c_str()); }

// A card case's asks (the Mac app's Snapshot.Request), done in its order once the card is quiet.
struct Request {
  std::optional<CardTab> tab;
  bool full = false, save = false, back = false, page = false, ai = false, whole = false;
  std::optional<std::string> open, section, answer;
  std::optional<int> entry;
  std::optional<Method> method;
  bool first = true;
};

// The fixture answers for a case into the replies dir ("error": every request fails with 401).
void answers(const std::string &fixtures, const std::string &kind) {
  wchar_t env[MAX_PATH]{};
  if (fixtures.empty() || kind.empty() || !GetEnvironmentVariableW(L"LEXIPEEK_AI_REPLIES", env, MAX_PATH)) return;
  std::filesystem::path replies(env);
  std::error_code e;
  for (auto &f : std::filesystem::directory_iterator(replies, e)) std::filesystem::remove(f.path(), e);
  if (kind == "error") return void(std::ofstream(replies / "status") << "401");
  for (auto &f : std::filesystem::directory_iterator(path(fixtures) / path(kind), e))
    if (f.path().extension() == ".json") std::filesystem::copy_file(f.path(), replies / f.path().filename(), e);
}

// A first-run step on an empty data dir (firstrun?step=…): packs.json / installed.json of `from` copied in as the step needs.
Case firstrun_case(const std::string &name, const std::string &step, const std::string &from) {
  return {name,
          [step, from](Shell &s) {
            auto dir = path(s.client().data().empty() ? app::data_dir() : s.client().data());
            std::error_code e;
            for (auto *f : {"packs.json", "installed.json"}) std::filesystem::remove(dir / f, e);
            auto copy = [&](const char *f) { std::filesystem::copy_file(path(from) / f, dir / f, std::filesystem::copy_options::overwrite_existing, e); };
            if (!from.empty() && (step == "pair" || step == "download" || step == "error" || step == "done")) copy("packs.json");
            if (!from.empty() && step == "done") copy("installed.json");
            s.open_first_run(false);
            auto &st = s.first_ui->state;
            st.list_error.clear(), st.error.clear(), st.downloading = false, st.done = st.total = 0;
            st.fetching = step == "list";
            if (step == "list-error") st.list_error = "Could not resolve host: github.com";
            if (step == "download") st.downloading = true, st.done = 27'000'000, st.total = 68'000'000;
            if (step == "error") st.error = "Could not connect to server";
            s.first_ui->refresh(s.theme());
          },
          [](Shell &s) {
            if (s.first) s.first.AppWindow().Hide();
          },
          [](Shell &s) { return content(s.first); }};
}

// A line of scripts/snapshot-cases.sh's cases: name|url[|kind], the url not percent-encoded ("#" and "+" are text).
std::optional<Case> card_case(const std::string &line, const std::string &fixtures, const std::string &firstrun_from) {
  auto bar = line.find('|');
  if (bar == std::string::npos || bar == 0) return std::nullopt;
  auto name = line.substr(0, bar), rest = line.substr(bar + 1);
  auto bar2 = rest.find('|');
  auto url = rest.substr(0, bar2), kind = bar2 == std::string::npos ? "" : rest.substr(bar2 + 1);
  auto q = url.find('?');
  auto host = url.substr(0, q);
  // The window: phase 3; noaccess: the Mac's Accessibility (Windows asks for none).
  if (host != "lookup" && host != "input" && host != "unread" && host != "firstrun") return std::nullopt;
  std::map<std::string, std::string> f;
  for (size_t at = q == std::string::npos ? url.size() : q + 1; at < url.size();) {
    auto amp = url.find('&', at);
    auto kv = url.substr(at, amp == std::string::npos ? std::string::npos : amp - at);
    auto eq = kv.find('=');
    f[kv.substr(0, eq)] = eq == std::string::npos ? "" : kv.substr(eq + 1);
    at = amp == std::string::npos ? url.size() : amp + 1;
  }
  if (host == "firstrun") return firstrun_case(name, f["step"], firstrun_from);
  Request r;
  if (auto t = f["tab"]; !t.empty())
    r.tab = t == "dictionary" ? CardTab::dictionary : t == "analysis" ? CardTab::analysis : t == "context" ? CardTab::context : CardTab::translate;
  r.full = f["full"] == "1", r.save = f["save"] == "1", r.back = f["back"] == "1", r.page = f["page"] == "1", r.whole = f["whole"] == "1";
  if (!f["open"].empty()) r.open = f["open"];
  if (!f["section"].empty()) r.section = f["section"];
  if (!f["answer"].empty()) r.answer = f["answer"];
  if (!f["entry"].empty()) r.entry = std::stoi(f["entry"]);
  auto m = f["method"];
  r.ai = m == "ai";
  if (m == "fast") r.method = Method::fast;
  if (m == "direct") r.method = Method::direct;
  if (m == "llm") r.method = Method::llm;
  auto text = f["text"];
  for (size_t at; (at = text.find("\\n")) != std::string::npos;) text.replace(at, 2, "\n");  // a line break
  bool pin = f["pin"] == "1";
  auto req = std::make_shared<Request>(r);
  Case c{name,
         [host, text, kind, fixtures, req, r, pin](Shell &s) {
           *req = r;  // each pass asks again
           answers(fixtures, kind);
           s.card.show(host == "lookup" ? std::optional<std::string>(text) : std::nullopt, Source::input, {600, 300, 120, 24}, false);
           if (host == "unread") s.card.model.unread = "Example", s.card.redraw();
           if (pin) s.card.model.toggle_pin();
         },
         [](Shell &s) { s.card.close(); },
         [](Shell &s) { return s.card.card.root(); }};
  c.wait = 300;
  c.end = [](Shell &s) { return s.card.card.scroll_to_end(); };
  c.quiet = [host](Shell &s) { return host != "lookup" || (s.card.model.has_card() && !s.card.model.busy()); };
  c.step = [req](Shell &s) {
    auto &r = *req;
    auto &model = s.card.model;
    if (!model.has_card()) return false;
    if (r.first) {
      r.first = false;
      if (r.method && model.nav.state.has_method) return model.use(*r.method), true;
      if (r.ai && model.nav.card().ai.translate) return model.choose_ai(), true;
    }
    if (model.look().tabs) {
      if (r.tab && model.nav.state.tab != *r.tab) return model.select(*r.tab), true;
      if (r.open) return model.open(*std::exchange(r.open, std::nullopt)), true;
      if (r.full && !model.nav.state.full_entry && !model.nav.card().entries.empty()) return model.full_entries(), true;
      if (r.section) return model.open_section(0, *std::exchange(r.section, std::nullopt)), true;
      if (r.entry) return model.open_entry(*std::exchange(r.entry, std::nullopt)), true;
      if (r.page && model.nav.state.section.page == 0 && !model.nav.state.section.key.empty()) return r.page = false, model.turn(1), true;
      if (r.back) return r.back = false, model.back(), true;
      if (r.answer) return model.relookup("", *std::exchange(r.answer, std::nullopt)), true;
    }
    if (r.save && !model.nav.state.saved) return r.save = false, model.toggle_saved(), true;
    if (r.whole) return r.whole = false, s.card.card.show_whole(true), true;
    return false;
  };
  return c;
}

std::vector<Case> cases(const std::string &file, const std::string &fixtures, const std::string &firstrun_from) {
  // A copy closes it: its Closed handler lets go of the slot.
  auto close = [](xaml::Window w) {
    if (w) w.Close();
  };
  auto asked = std::make_shared<xaml::Window>(nullptr);
  auto menu = [](Shell &s) { return s.menu.shown(); };
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
      {"main", [](Shell &s) { s.open_main(); }, [close](Shell &s) { close(s.main.try_as<xaml::Window>()); },
       [](Shell &s) { return s.main ? s.main.Content() : nullptr; }},
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
  std::ifstream in(path(file), std::ios::binary);
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (auto c = card_case(line, fixtures, firstrun_from)) all.push_back(*c);
  }
  return all;
}

struct Run {
  Shell &shell;
  std::filesystem::path dir;
  std::function<void(int)> done;
  std::vector<Case> all;
  size_t step = 0;
  int failed = 0, settled = 0;
  ULONGLONG since = 0;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer poll{nullptr};
  std::vector<winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer> timers;  // kept: one may still be in its Tick

  Case &now() { return all[step % all.size()]; }
  bool dark() const { return step >= all.size(); }
  std::filesystem::path file(const char *end = "") { return dir / path(now().name + (dark() ? "-dark" : "-light") + end + ".png"); }

  void next() {
    if (step == all.size() * 2) return done(failed);
    shell.forced_theme = dark() ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
    now().show(shell);
    settled = 0;
    since = GetTickCount64();
    poll.Start();
  }

  // Quiet (or 90 s gone: a failure) and settled for a few ticks: the next ask, else its picture (and the body's end).
  void tick() {
    auto &c = now();
    bool quiet = c.quiet(shell), late = GetTickCount64() - since > 90000;
    if (!quiet && !late) return;
    if (++settled < 4 || GetTickCount64() - since < static_cast<ULONGLONG>(c.wait)) return;
    if (quiet && c.step(shell)) return void(settled = 0);
    poll.Stop();
    if (!quiet) fail("not quiet");
    render(c.drawn(shell), file(), dark(), [this](bool ok) {
      if (!ok) fail("not rendered");
      auto &c = now();
      bool card = c.end && c.end(shell);
      after(card ? 500 : 0, [this, card] {
        auto finish = [this] {
          now().hide(shell);
          ++step;
          after(500, [this] { next(); });
        };
        if (!card) return finish();
        render(now().drawn(shell), file("-end"), dark(), [this, finish](bool ok) {
          if (!ok) fail("end not rendered");
          finish();
        });
      });
    });
  }

  // Which case failed and how: <dir>/failed.txt.
  void fail(const char *why) {
    ++failed;
    std::ofstream(dir / "failed.txt", std::ios::app) << now().name << (dark() ? " dark: " : " light: ") << why << "\n";
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

void snapshots(Shell &shell, const std::string &dir, const std::string &cases_file, const std::string &fixtures, const std::string &firstrun_from,
               std::function<void(int)> done) {
  std::filesystem::path out = path(dir);
  std::filesystem::create_directories(out);
  // AI answers from files: any key will do (the provider would want one).
  wchar_t env[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"LEXIPEEK_AI_REPLIES", env, MAX_PATH)) shell.card.model.key = [] { return std::string("snapshot"); };
  auto *run = new Run{shell, out, std::move(done), cases(cases_file, fixtures, firstrun_from)};  // lives until the process leaves
  run->poll = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
  run->poll.Interval(std::chrono::milliseconds(100));
  run->poll.Tick([run](auto &&, auto &&) { run->tick(); });
  run->next();
}

}  // namespace lexipeek::win
