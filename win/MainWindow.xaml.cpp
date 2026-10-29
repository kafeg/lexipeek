#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <algorithm>
#include <shlobj_core.h>
#include <shobjidl_core.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include "resource.h"
#include "settings_ui.hpp"
#include "ui.hpp"

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
using namespace lexipeek;
using namespace lexipeek::win;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::System::VirtualKey;

namespace {

// The system's save dialog in Downloads with the file's name; "" when cancelled.
std::string save_to(HWND owner, const std::string &name) {
  try {
    auto d = winrt::create_instance<IFileSaveDialog>(CLSID_FileSaveDialog);
    winrt::com_ptr<IShellItem> downloads;
    if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(downloads.put())))) d->SetFolder(downloads.get());
    d->SetFileName(h(name).c_str());
    FILEOPENDIALOGOPTIONS o{};
    d->GetOptions(&o);
    d->SetOptions(o | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    if (d->Show(owner) != S_OK) return "";
    winrt::com_ptr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(d->GetResult(item.put())) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return "";
    auto s = winrt::to_string(path);
    CoTaskMemFree(path);
    return s;
  } catch (winrt::hresult_error const &) {
    return "";
  }
}

// A list's or a passage's lines run on in a one-line row (mac NotesPane's oneLine).
std::string one_line(std::string s) {
  for (auto &c : s)
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  return s;
}

const NotesTab tabs[] = {NotesTab::words, NotesTab::phrases, NotesTab::history};

}  // namespace

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
  AppWindow().Resize({static_cast<int32_t>(1000 * scale), static_cast<int32_t>(700 * scale)});
}

MainWindow::~MainWindow() {
  *alive_ = false;
  if (memory_) memory_.Stop();
  if (listening_ >= 0) notes_bus().forget(listening_);
  if (keep_) keep_->card.forget(card_listening_);
}

void MainWindow::start(MainKeep &keep) {
  keep_ = &keep;
  card_ = std::make_unique<CardWidget>(keep.card, true);
  card_->open_pane = [this](int section, int tab) {
    if (open_pane) open_pane(section, tab);
  };
  card_->submitted = [this](const std::string &t) { look(t); };
  CardFrame().Child(card_->root());
  auto shown = [this] {
    bool card = keep_->card.has_card() || keep_->card.looking();
    CardFrame().Visibility(card ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    Hint().Visibility(card ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  };
  // The model outlives this window: its listener goes with the window.
  card_listening_ = keep.card.listen(shown);
  shown();

  Query().Text(h(keep.query));
  Query().TextChanged([this](auto &&, auto &&) { keep_->query = winrt::to_string(Query().Text()); });
  // Return looks it up, Shift+Return a new line (a pasted text may have many).
  Query().PreviewKeyDown([this](auto &&, xaml::Input::KeyRoutedEventArgs const &k) {
    if (k.Key() != VirtualKey::Enter || (GetKeyState(VK_SHIFT) & 0x8000)) return;
    k.Handled(true);
    look(winrt::to_string(Query().Text()));
  });
  Query().Loaded([this](auto &&, auto &&) {
    if (page_ == lookup) Query().Focus(xaml::FocusState::Programmatic);
  });

  Nav().ItemInvoked([this](auto &&, controls::NavigationViewItemInvokedEventArgs const &e) {
    auto tag = unbox_value_or<hstring>(e.InvokedItemContainer().Tag(), L"");
    if (tag == L"settings" && open_pane) open_pane(-1, 0);
    if (tag == L"about" && open_about) open_about();
  });
  Nav().SelectionChanged([this](auto &&, controls::NavigationViewSelectionChangedEventArgs const &e) {
    auto item = e.SelectedItemContainer().try_as<controls::NavigationViewItem>();
    if (!item) return;
    auto tag = unbox_value_or<hstring>(item.Tag(), L"");
    int p = tag == L"words" ? words : tag == L"phrases" ? phrases : tag == L"history" ? history : tag == L"help" ? help : lookup;
    if (p != page_ && p >= words && p <= history) notes_page_ = 0;
    page_ = p;
    LookupPage().Visibility(p == lookup ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    NotesPage().Visibility(p >= words && p <= history ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    HelpPage().Visibility(p == help ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    if (p == help) draw_help();
    else if (p != lookup) draw_notes();
    else Query().Focus(xaml::FocusState::Programmatic);
  });
  // F1: Help, from any page.
  xaml::Input::KeyboardAccelerator f1;
  f1.Key(VirtualKey::F1);
  f1.Invoked([this](auto &&, xaml::Input::KeyboardAcceleratorInvokedEventArgs const &e) {
    e.Handled(true);
    show_page(help);
  });
  Root().KeyboardAccelerators().Append(f1);
  Root().KeyboardAcceleratorPlacementMode(xaml::Input::KeyboardAcceleratorPlacementMode::Hidden);

  Rows().ItemClick([this](auto &&, controls::ItemClickEventArgs const &e) {
    if (auto item = e.ClickedItem().try_as<xaml::FrameworkElement>()) look(winrt::to_string(unbox_value_or<hstring>(item.Tag(), L"")));
  });
  Rows().KeyDown([this](auto &&, xaml::Input::KeyRoutedEventArgs const &k) {
    if (k.Key() != VirtualKey::Delete) return;
    if (auto item = Rows().SelectedItem().try_as<xaml::FrameworkElement>()) k.Handled(true), remove(winrt::to_string(unbox_value_or<hstring>(item.Tag(), L"")));
  });
  Tsv().Click([this](auto &&, auto &&) {
    auto tab = tabs[page_ - words];
    auto f = save_to(hwnd(*this), app::notes_file(tab));
    if (f.empty()) return;
    auto e = keep_->client.engine;
    page::run<std::string>(keep_->client, [e, tab, f] { return e.export_tsv(tab, f); }, [this, alive = alive_](std::string s) {
      if (*alive) said(s);
    });
  });
  Clear().Click([this](auto &&, auto &&) {
    page::confirm(Root(), app::text("clear_history_q"), app::text("notes_clear_d"), app::text("clear_history"), [this] {
      keep_->client.engine.clear_history();
      notes_bus().changed();
    });
  });
  Export().Click([this](auto &&, auto &&) {
    auto f = save_to(hwnd(*this), app::backup_file());
    if (f.empty()) return;
    auto e = keep_->client.engine;
    page::run<std::string>(keep_->client, [e, f] { return e.backup(f); }, [this, alive = alive_](std::string s) {
      if (*alive) said(s);
    });
  });
  // Off the window's thread: a big backup takes a while.
  Import().Click([this](auto &&, auto &&) {
    auto files = page::pick_files(hwnd(*this), tr("import_btn"));
    if (files.empty()) return;
    auto e = keep_->client.engine;
    auto f = files.front();
    page::run<std::string>(keep_->client, [e, f] { return e.restore(f); }, [this, alive = alive_](std::string s) {
      notes_bus().changed();
      if (*alive) said(s);
    });
  });

  // Notes changed anywhere (a look-up, ☆, ✕, an import): the counts and the open list again.
  listening_ = notes_bus().listen([this] { refresh(Root().RequestedTheme()); });
  // What is in memory, while the window is open.
  memory_ = page::every(15000, [this] { Memory().Text(h(keep_->client.engine.memory().text)); });
  memory_.Start();
  // Closed: its card's work stops; what it listened to forgets it.
  Closed([this](auto &&, auto &&) {
    keep_->card.cancel();
    keep_->card.forget(card_listening_);
    memory_.Stop();
    notes_bus().forget(listening_), listening_ = -1;
  });
}

void MainWindow::refresh(xaml::ElementTheme theme) {
  if (!keep_) return;
  auto dir = keep_->client.data();
  auto s = app::settings(dir);
  LookupItem().Content(box_value(tr("win_lookup")));
  auto counts = keep_->client.engine.notes(NotesTab::words, 0).tabs;  // "Words (3)", "Phrases (1)", "History (12)"
  controls::NavigationViewItem items[] = {WordsItem(), PhrasesItem(), HistoryItem()};
  const char *names[] = {"list_words", "list_phrases", "list_history"};
  for (int i = 0; i < 3; ++i) items[i].Content(box_value(i < static_cast<int>(counts.size()) ? h(counts[i]) : tr(names[i])));
  HelpItem().Content(box_value(tr("help_t")));
  SettingsItem().Content(box_value(tr("settings_more")));
  AboutItem().Content(box_value(tr("menu_about")));
  Memory().Text(h(keep_->client.engine.memory().text));
  Query().PlaceholderText(tr("card_input_hint"));
  Hint().Text(tr("win_hint", app::hotkey_label(s.lookup), app::hotkey_label(s.input)));
  // No language pack yet: the InfoBar with Set Up… (the first run).
  bool pack = app::has_pair(dir);
  NoPack().IsOpen(!pack);
  NoPack().Visibility(pack ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  if (!pack) {
    NoPack().Title(tr("nopack_t"));
    NoPack().Message(tr("nopack_d"));
    NoPack().ActionButton(page::button(tr("setup_btn"), [this] {
      if (open_first_run) open_first_run();
    }));
  }
  Tsv().Content(box_value(tr("tsv_btn")));
  controls::ToolTipService::SetToolTip(Tsv(), box_value(tr("tsv_d")));
  Clear().Content(box_value(tr("clear_history_more")));
  BackupTitle().Text(tr("hw_backup_t"));
  BackupAbout().Text(tr("backup_d"));
  Export().Content(box_value(tr("export_btn")));
  Import().Content(box_value(tr("import_btn")));
  Root().RequestedTheme(theme);
  AppWindow().TitleBar().PreferredTheme(theme == xaml::ElementTheme::Light  ? Microsoft::UI::Windowing::TitleBarTheme::Light
                                        : theme == xaml::ElementTheme::Dark ? Microsoft::UI::Windowing::TitleBarTheme::Dark
                                                                            : Microsoft::UI::Windowing::TitleBarTheme::UseDefaultAppMode);
  help_drawn_ = false;  // the language or the hotkeys may have changed
  if (page_ == help) draw_help();
  if (page_ >= words && page_ <= history) draw_notes();
}

void MainWindow::show_page(int page) {
  controls::NavigationViewItem items[] = {LookupItem(), WordsItem(), PhrasesItem(), HistoryItem(), HelpItem()};
  if (page >= lookup && page <= help) Nav().SelectedItem(items[page]);
}

void MainWindow::look(const std::string &text) {
  if (text.find_first_not_of(" \t\r\n") == std::string::npos || !keep_) return;
  if (winrt::to_string(Query().Text()) != text) Query().Text(h(text));
  show_page(lookup);
  auto &card = keep_->card;
  card.reset();
  card.source = Source::input;
  card.lookup(text);
}

void MainWindow::remove(const std::string &text) {
  if (text.empty()) return;
  keep_->client.engine.remove_note(tabs[page_ - words], text);
  notes_bus().changed();
}

void MainWindow::said(const std::string &s) {
  Said().Text(h(s));
  Said().Visibility(s.empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
}

// The open list's page: a click (or Return) opens the card, ✕ (or Delete) removes; TSV…, the pager, Clear History….
void MainWindow::draw_notes() {
  auto tab = tabs[page_ - words];
  auto v = keep_->client.engine.notes(tab, notes_page_);
  notes_page_ = v.page;  // the core keeps it inside the pages (after a removal)
  const char *names[] = {"list_words", "list_phrases", "list_history"};
  NotesHeading().Text(tr(names[page_ - words]));
  auto selected = Rows().SelectedIndex();
  Rows().Items().Clear();
  auto tip = tr(tab == NotesTab::history ? "forget" : "unsave_word");
  for (auto &r : v.rows) {
    auto g = load(LR"(<Grid ColumnSpacing="12" Padding="4,7,0,8">
  <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
  <StackPanel VerticalAlignment="Center">
    <TextBlock x:Name="Text" Style="{StaticResource BodyTextBlockStyle}" TextWrapping="NoWrap" TextTrimming="CharacterEllipsis"/>
    <TextBlock x:Name="Line" Style="{StaticResource CaptionTextBlockStyle}" TextWrapping="NoWrap" TextTrimming="CharacterEllipsis"
               Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
  </StackPanel>
  <TextBlock x:Name="Looks" Grid.Column="1" VerticalAlignment="Center" Style="{StaticResource CaptionTextBlockStyle}"
             Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
</Grid>)").as<controls::Grid>();
    part<controls::TextBlock>(g, L"Text").Text(h(one_line(r.text)));
    part<controls::TextBlock>(g, L"Line").Text(h(one_line(r.line)));
    part<controls::TextBlock>(g, L"Looks").Text(h(r.looks));
    auto text = r.text;
    auto x = page::icon_button(0xE711, tip, [this, text] { remove(text); });
    x.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::Grid::SetColumn(x, 2);
    g.Children().Append(x);
    controls::ToolTipService::SetToolTip(part<controls::TextBlock>(g, L"Text"), box_value(h(r.text)));
    controls::ListViewItem item;
    item.Content(g);
    item.Tag(box_value(h(r.text)));
    item.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    xaml::Automation::AutomationProperties::SetName(item, h(one_line(r.text)));
    Rows().Items().Append(item);
  }
  if (selected >= 0 && v.rows.size()) Rows().SelectedIndex(std::min<int>(selected, static_cast<int>(v.rows.size()) - 1));
  bool empty = v.rows.empty();
  Empty().Text(h(v.empty));
  Empty().Visibility(empty ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
  Tsv().Visibility(empty ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  Clear().Visibility(v.clear ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
  // The pager: ‹ "51–100 of 412" ›, only when there is more than a page.
  auto pager = Pager();
  pager.Children().Clear();
  pager.Visibility(v.pager.empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  if (!v.pager.empty()) {
    PagerText().Text(h(v.pager));
    auto prev = page::icon_button(0xE76B, L"", [this] { --notes_page_, draw_notes(); });
    auto next = page::icon_button(0xE76C, L"", [this] { ++notes_page_, draw_notes(); });
    prev.IsEnabled(v.page > 0), next.IsEnabled(v.page + 1 < v.pages);
    for (auto e : {prev.as<xaml::UIElement>(), PagerText().as<xaml::UIElement>(), next.as<xaml::UIElement>()}) pager.Children().Append(e);
  }
}

// Every topic on one page as Settings' groups: its title over a card of its items and the pane it is about.
void MainWindow::draw_help() {
  if (help_drawn_) return;
  help_drawn_ = true;
  auto s = app::settings(keep_->client.data());
  HelpHeading().Text(tr("help_t"));
  auto topics = Topics().Children();
  topics.Clear();
  for (auto &t : help_view(app::hotkey_label(s.lookup), app::hotkey_label(s.input))) {
    topics.Append(page::heading(t.title, "", topics.Size() == 0));
    auto card = load(LR"(<StackPanel Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}"
    BorderThickness="1" CornerRadius="8" Padding="16,14,16,14" Spacing="12"/>)").as<controls::StackPanel>();
    for (auto &i : t.items) {
      controls::StackPanel item;
      item.Spacing(2);
      if (!i.title.empty()) item.Children().Append(page::text(i.title, L"BodyStrongTextBlockStyle"));
      item.Children().Append(page::text(i.text, L"BodyTextBlockStyle", true));
      card.Children().Append(item);
    }
    if (!t.go.empty()) {
      controls::HyperlinkButton go;
      go.Content(box_value(h(t.go)));
      go.Padding({0, 0, 0, 0});
      auto section = static_cast<int>(t.section), tab = t.tab;
      go.Click([this, section, tab](auto &&, auto &&) {
        if (open_pane) open_pane(section, tab);
      });
      card.Children().Append(go);
    }
    topics.Append(card);
  }
}

bool MainWindow::scroll_to_end() {
  if (page_ == lookup) return card_ && card_->scroll_to_end();
  controls::ScrollViewer s{nullptr};
  if (page_ == help) s = HelpPage();
  else if (auto inside = xaml::Media::VisualTreeHelper::GetChildrenCount(Rows()) ? xaml::Media::VisualTreeHelper::GetChild(Rows(), 0) : nullptr)
    if (auto border = inside.try_as<controls::Border>()) s = border.Child().try_as<controls::ScrollViewer>();
  if (!s) return false;
  s.UpdateLayout();
  if (s.ScrollableHeight() <= 1) return false;
  s.ChangeView(nullptr, s.ScrollableHeight(), nullptr, true);
  return true;
}

}  // namespace winrt::LexiPeek::implementation
