// The LexiPeek window: a NavigationView, its pages drawn from the core's views. Prototype: Look up shows the core's card
// of one word as text lines.
#include "pch.h"
#include "MainWindow.xaml.h"
#include "App.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include "lexipeek/app.hpp"

namespace xaml = winrt::Microsoft::UI::Xaml;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Storage::Streams;

namespace {

std::wstring arg(const wchar_t *name) {
  int n = 0;
  auto **argv = CommandLineToArgvW(GetCommandLineW(), &n);
  std::wstring v;
  for (int i = 1; i + 1 < n; i++)
    if (std::wstring_view(argv[i]) == name) v = argv[i + 1];
  LocalFree(argv);
  return v;
}

std::string utf8(const std::wstring &w) { return winrt::to_string(w); }

std::vector<std::string> card_lines(const std::string &data, const std::string &text) {
  using namespace lexipeek;
  MEMORYSTATUSEX m{.dwLength = sizeof(MEMORYSTATUSEX)};
  GlobalMemoryStatusEx(&m);
  auto engine = app::Engine::make(data, app::device(static_cast<int>(m.ullTotalPhys >> 20), {}, Device{}.arch));
  auto card = engine.lookup(text, Source::input);
  std::vector<std::string> lines{"data: " + (data.empty() ? app::data_dir() : data),
                                 "hotkey: " + app::hotkey_label(Hotkey{}), "header: " + card.header};
  for (auto &e : card.entries) lines.push_back(e.dictionary + ": " + e.line);
  if (!card.error.empty()) lines.push_back("error: " + card.error);
  return lines;
}

}  // namespace

namespace winrt::LexiPeek::implementation {

void MainWindow::InitializeComponent() {
  MainWindowT::InitializeComponent();
  auto data = arg(L"--data");
  wchar_t env[MAX_PATH]{};
  if (data.empty() && GetEnvironmentVariableW(L"LEXIPEEK_DATA", env, MAX_PATH)) data = env;
  wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
  LCIDToLocaleName(GetUserDefaultUILanguage(), locale, LOCALE_NAME_MAX_LENGTH, 0);
  lexipeek::app::set_language(utf8(data), utf8(locale).substr(0, 2));  // the system's UI language: "ru" of "ru-RU"
  LookupItem().Content(box_value(to_hstring(lexipeek::app::text("win_lookup"))));
  NotesItem().Content(box_value(to_hstring(lexipeek::app::text("tab_words"))));
  auto look = arg(L"--look");
  for (auto &line : card_lines(utf8(data), look.empty() ? "Haus" : utf8(look))) {
    xaml::Controls::TextBlock t;
    t.Text(to_hstring(line));
    t.TextWrapping(xaml::TextWrapping::Wrap);
    Lines().Children().Append(t);
  }
  AppWindow().Resize({1000, 700});
  if (auto out = arg(L"--self-test"); !out.empty())
    Lines().Loaded([this, out](auto &&, auto &&) { self_test(out); });
}

// Waits for a frame, then renders the window's content into a PNG and lists what the page shows.
fire_and_forget MainWindow::self_test(std::wstring out) {
  auto strong = get_strong();
  std::filesystem::create_directories(out);
  std::ofstream txt(std::filesystem::path(out) / "self-test.txt", std::ios::binary);
  for (auto &&child : Lines().Children()) txt << to_string(child.as<xaml::Controls::TextBlock>().Text()) << "\n";
  try {
    xaml::Media::Imaging::RenderTargetBitmap bitmap;
    co_await bitmap.RenderAsync(Content());
    auto pixels = co_await bitmap.GetPixelsAsync();
    InMemoryRandomAccessStream stream;
    auto encoder = co_await BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream);
    encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, bitmap.PixelWidth(), bitmap.PixelHeight(), 96, 96,
                         {pixels.data(), pixels.data() + pixels.Length()});
    co_await encoder.FlushAsync();
    DataReader reader(stream.GetInputStreamAt(0));
    auto size = static_cast<uint32_t>(stream.Size());
    co_await reader.LoadAsync(size);
    std::vector<uint8_t> png(size);
    reader.ReadBytes(png);
    std::ofstream(std::filesystem::path(out) / "window.png", std::ios::binary).write(reinterpret_cast<char *>(png.data()), png.size());
    txt << "png: " << bitmap.PixelWidth() << "x" << bitmap.PixelHeight() << "\n";
  } catch (hresult_error const &e) {
    txt << "png failed: " << to_string(e.message()) << "\n";
  }
  FILETIME created{}, unused{};
  GetProcessTimes(GetCurrentProcess(), &created, &unused, &unused, &unused);
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  auto ticks = [](FILETIME f) { return ULARGE_INTEGER{.LowPart = f.dwLowDateTime, .HighPart = f.dwHighDateTime}.QuadPart; };
  txt << "drawn after: " << (ticks(now) - ticks(created)) / 10000 << " ms\n";
  txt.close();
  App::quit(0);
}

}  // namespace winrt::LexiPeek::implementation
