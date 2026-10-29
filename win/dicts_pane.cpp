#include "pch.h"
#include "dicts_pane.hpp"
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include "settings_ui.hpp"

namespace lexipeek::win {

namespace controls = xaml::Controls;

namespace {

void set_title(const xaml::Window &w, const winrt::hstring &title) {
  w.Title(title);
  part<controls::TextBlock>(w.Content().as<xaml::FrameworkElement>(), L"Title").Text(title);
}

}  // namespace

DictsPane::DictsPane(CoreClient &client) : c_(client) {}

xaml::UIElement DictsPane::view() {
  auto dir = c_.data();
  auto v = app::dictionaries(dir);
  controls::StackPanel body;
  body.Spacing(4);
  // Files dropped on the pane are imported.
  body.AllowDrop(true);
  body.Background(xaml::Media::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent()));
  body.DragOver([](auto &&, xaml::DragEventArgs const &e) {
    if (e.DataView().Contains(winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
      e.AcceptedOperation(winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
  });
  body.Drop([this](auto &&, xaml::DragEventArgs const &e) { dropped(e); });
  controls::StackPanel buttons;
  buttons.Orientation(controls::Orientation::Horizontal);
  buttons.Spacing(8);
  buttons.Margin({0, 0, 0, 8});
  root_ = body;
  auto folder = page::button(tr("add_folder_more"), [this, dir] {
    auto d = page::pick_folder(page::owner(root_), tr("folder_panel"));
    if (d.empty()) return;
    message_ = app::add_folder(dir, d);
    changed(true);
  });
  auto import = page::button(tr("import_more"), [this] {
    auto files = page::pick_files(page::owner(root_), tr("import_"));
    if (!files.empty()) DictsPane::import(files);
  });
  auto catalog = page::button(tr("catalog_more"), [this] {
    if (!catalog_) {
      catalog_ = std::make_unique<CatalogWindow>(c_);
      catalog_->closed = [this] { changed(true); };
    }
    catalog_->open(theme);
  });
  for (auto b : {folder, import, catalog}) b.IsEnabled(!busy_), buttons.Children().Append(b);
  body.Children().Append(buttons);
  if (!message_.empty()) {
    auto m = page::text(message_, L"BodyTextBlockStyle", true);
    m.IsTextSelectionEnabled(true);
    body.Children().Append(m);
  }
  body.Children().Append(page::text(v.import_note, L"CaptionTextBlockStyle", true));
  body.Children().Append(page::heading(app::text("lang_own_tab"), "", false));
  bool own = false;
  for (size_t i = 0; i < v.rows.size(); ++i) {
    auto &r = v.rows[i];
    own = own || !r.pack;
    auto id = r.id;
    controls::StackPanel right;
    right.Orientation(controls::Orientation::Horizontal);
    // An own dictionary on or off in the card (packs always are).
    if (!r.pack) {
      auto on = page::toggle(r.on, true, [this, dir, id](bool on) {
        app::set_dict_on(dir, id, on);
        changed(false);
      }, r.title);
      on.Margin({0, 0, 12, 0});
      right.Children().Append(on);
    }
    auto up = page::icon_button(0xE70E, tr("up"), [this, dir, id] {
      app::move_dict(dir, id, -1);
      changed(false);
    });
    up.IsEnabled(i > 0);
    auto down = page::icon_button(0xE70D, tr("down"), [this, dir, id] {
      app::move_dict(dir, id, 1);
      changed(false);
    });
    down.IsEnabled(i + 1 < v.rows.size());
    right.Children().Append(up);
    right.Children().Append(down);
    if (r.removable) {
      auto title = r.title;
      right.Children().Append(page::icon_button(0xE74D, tr("delete"), [this, dir, id, title] {
        page::confirm(root_, app::text("delete_q", title), app::text("delete_d"), app::text("delete"), [this, dir, id] {
          message_ = app::remove_dict(dir, id);
          changed(true);
        });
      }));
    }
    auto c = page::card(static_cast<wchar_t>(r.pack ? 0xE8F1 : 0xE82D), r.title, r.detail, right);
    if (!r.on) page::dim(c);
    body.Children().Append(c);
  }
  if (!own) body.Children().Append(page::text(v.empty, L"BodyTextBlockStyle", true));
  auto note = page::text(v.note, L"CaptionTextBlockStyle", true);
  note.Margin({0, 4, 0, 0});
  body.Children().Append(note);
  if (!v.folders.empty()) {
    body.Children().Append(page::heading(app::text("folders_t"), "", false));
    for (auto &f : v.folders) {
      auto path = f.path;
      auto remove = page::button(tr("remove"), [this, dir, path] {
        app::remove_folder(dir, path);
        changed(true);
      });
      body.Children().Append(page::card(0xE8B7, f.title, f.detail, remove));
    }
  }
  return body;
}

winrt::fire_and_forget DictsPane::dropped(xaml::DragEventArgs e) {
  using namespace winrt::Windows::ApplicationModel::DataTransfer;
  if (!e.DataView().Contains(StandardDataFormats::StorageItems())) co_return;
  auto deferral = e.GetDeferral();
  auto items = co_await e.DataView().GetStorageItemsAsync();
  std::vector<std::string> files;
  for (auto const &i : items)
    if (!i.Path().empty()) files.push_back(winrt::to_string(i.Path()));
  deferral.Complete();
  if (!files.empty()) import(files);
}

void DictsPane::import(const std::vector<std::string> &files) {
  busy_ = true;
  message_ = app::text("importing");
  if (redraw) redraw();
  auto dir = c_.data();
  page::run<std::string>(c_, [dir, files] { return app::imported_text(app::import_dicts(dir, files)); }, [this](std::string text) {
    busy_ = false;
    message_ = text;
    changed(true);
  });
}

// A new dictionary on disk: the library looks again (and indexes it); else only the settings changed.
void DictsPane::changed(bool reload) {
  c_.configure();
  if (reload) c_.reload();
  if (redraw) redraw();
}

CatalogWindow::CatalogWindow(CoreClient &client) : c_(client) {
  auto content = load(LR"(<Grid Padding="24,0,24,24" RowSpacing="12">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel x:Name="Sources" Orientation="Horizontal" Spacing="16"/>
  <TextBox x:Name="Filter" Grid.Row="1"/>
  <ScrollViewer Grid.Row="2" Margin="0,0,-16,0"><StackPanel x:Name="Body" Spacing="4" Padding="0,0,16,0"/></ScrollViewer>
  <Button x:Name="Done" Grid.Row="3" HorizontalAlignment="Right" MinWidth="120" Style="{StaticResource AccentButtonStyle}"/>
</Grid>)");
  sources_ = part<controls::StackPanel>(content, L"Sources");
  body_ = part<controls::StackPanel>(content, L"Body");
  filter_box_ = part<controls::TextBox>(content, L"Filter");
  filter_box_.TextChanged([this](auto &&, auto &&) {
    filter_ = winrt::to_string(filter_box_.Text());
    draw();
  });
  done_ = part<controls::Button>(content, L"Done");
  done_.Click([this](auto &&, auto &&) { close(); });
  window = win::window(app::text("lang_own"), content, 600, 680, xaml::ElementTheme::Default);
  // Closed by its title bar: hidden, the download stopped.
  window.AppWindow().Closing([this](auto &&, winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const &e) {
    e.Cancel(true);
    close();
  });
  poll_ = page::every(300, [this] {
    auto total = dl_.get_total();
    if (bar_ && total > 0) bar_.Value(static_cast<double>(dl_.get_done()) / total);
  });
}

void CatalogWindow::open(xaml::ElementTheme theme) {
  window.Content().as<xaml::FrameworkElement>().RequestedTheme(theme);
  done_.Content(winrt::box_value(tr("done")));
  present(window);
  fetch();
}

void CatalogWindow::close() {
  dl_.cancel();
  window.AppWindow().Hide();
  if (closed) closed();
}

void CatalogWindow::fetch() {
  loading_ = true;
  draw();
  auto dir = c_.data();
  page::run<CatalogList>(c_, [dir] { return app::catalog(dir); }, [this](CatalogList l) {
    list_ = l;
    failed_.clear();
    for (auto &f : l.failed) failed_ += (failed_.empty() ? "" : "\n") + f;
    loading_ = false;
    draw();
  });
}

void CatalogWindow::draw() {
  auto dir = c_.data();
  auto v = app::catalog_view(dir, list_, filter_);
  filter_box_.PlaceholderText(h(v.filter_hint));
  sources_.Children().Clear();
  for (auto &s : v.sources) {
    controls::CheckBox b;
    b.Content(winrt::box_value(h(s.title)));
    b.IsChecked(s.on);
    controls::ToolTipService::SetToolTip(b, winrt::box_value(h(s.about)));
    auto id = s.id;
    b.Click([this, dir, id](winrt::Windows::Foundation::IInspectable const &sender, auto &&) {
      app::set_source_on(dir, id, sender.as<controls::CheckBox>().IsChecked().Value());
      c_.ui([this] { fetch(); });
    });
    sources_.Children().Append(b);
  }
  body_.Children().Clear();
  bar_ = nullptr;
  body_.Children().Append(page::text(v.legend, L"CaptionTextBlockStyle", true));
  if (!downloading_.empty()) {
    bar_ = page::progress(0);
    auto total = dl_.get_total();
    if (total > 0) bar_.Value(static_cast<double>(dl_.get_done()) / total);
    body_.Children().Append(page::card(0xE896, downloading_, "", page::button(tr("cancel"), [this] { dl_.cancel(); }), bar_));
  }
  for (auto *s : {&message_, &failed_, &v.empty})
    if (!s->empty()) body_.Children().Append(page::text(*s, L"BodyTextBlockStyle", true));
  if (loading_) {
    controls::StackPanel p;
    p.Orientation(controls::Orientation::Horizontal);
    p.Spacing(12);
    controls::ProgressRing ring;
    ring.Width(20), ring.Height(20);
    p.Children().Append(ring);
    p.Children().Append(page::text(app::text("loading_lists"), L"BodyTextBlockStyle", true));
    body_.Children().Append(p);
  }
  // The first rows only (a source lists hundreds): the filter finds the others.
  constexpr size_t shown = 150;
  for (size_t i = 0; i < v.rows.size() && i < shown; ++i) {
    auto &r = v.rows[i];
    auto item = r.item;
    auto title = r.title, about = r.about;
    auto get = page::button(tr("download"), [this, item, title, about] {
      page::confirm(body_, title, about, app::text("download"), [this, item, title] { CatalogWindow::get(item, title); });
    });
    get.IsEnabled(downloading_.empty());
    body_.Children().Append(page::card(0, r.title, r.detail, get));
  }
  if (v.rows.size() > shown) body_.Children().Append(page::text(app::text("catalog_rest", std::to_string(v.rows.size() - shown)), L"CaptionTextBlockStyle", true));
  set_title(window, tr("lang_own") + (v.info.empty() ? L"" : L"  ·  " + h(v.info)));
}

void CatalogWindow::get(int item, const std::string &title) {
  if (!downloading_.empty()) return;
  downloading_ = title;
  message_.clear();
  dl_ = app::Download();
  auto dl = dl_;
  auto dir = c_.data();
  auto list = list_;
  poll_.Start();
  page::run<std::string>(c_, [dir, list, item, dl] { return app::imported_text(app::download_dict(dir, list, item, dl)); }, [this](std::string text) {
    poll_.Stop();
    downloading_.clear();
    message_ = text;
    draw();
  });
  draw();
}

}  // namespace lexipeek::win
