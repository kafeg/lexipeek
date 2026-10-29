#include "pch.h"
#include "first_run.hpp"
#include "lexipeek/os.hpp"
#include "settings_ui.hpp"

namespace lexipeek::win {

namespace controls = xaml::Controls;

FirstRunWindow::FirstRunWindow(CoreClient &client, xaml::ElementTheme theme) : c_(client) {
  auto content = load(LR"(<Grid>
  <Grid.RowDefinitions><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <Grid Padding="40,4,40,24">
    <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
    <Image x:Name="Logo" Width="48" Height="48" HorizontalAlignment="Left" Margin="0,0,0,20"/>
    <TextBlock x:Name="Step" Grid.Row="1" Style="{StaticResource CaptionTextBlockStyle}" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
    <TextBlock x:Name="Heading" Grid.Row="2" Margin="0,2,0,12" Style="{StaticResource TitleTextBlockStyle}" TextWrapping="Wrap"/>
    <TextBlock x:Name="Text" Grid.Row="3" Margin="0,0,0,20" Style="{StaticResource BodyTextBlockStyle}" TextWrapping="Wrap"/>
    <StackPanel x:Name="Body" Grid.Row="4" Spacing="8"/>
  </Grid>
  <Grid Grid.Row="1" Padding="24" Background="{ThemeResource SolidBackgroundFillColorBaseBrush}"
        BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" BorderThickness="0,1,0,0">
    <StackPanel x:Name="Buttons" Orientation="Horizontal" HorizontalAlignment="Right" Spacing="8"/>
  </Grid>
</Grid>)");
  body_ = part<controls::StackPanel>(content, L"Body");
  buttons_ = part<controls::StackPanel>(content, L"Buttons");
  step_ = part<controls::TextBlock>(content, L"Step");
  title_ = part<controls::TextBlock>(content, L"Heading");
  text_ = part<controls::TextBlock>(content, L"Text");
  window = win::window(app::text("fr_window"), content, 600, 660, theme);
  part<controls::Image>(content, L"Logo").Source(app_icon(static_cast<int>(48 * GetDpiForWindow(hwnd(window)) / 96.0)));
  if (auto p = window.AppWindow().Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>())
    p.IsResizable(false), p.IsMaximizable(false);
  // Its title bar's X: hidden as finished (a later opening shows the step it is at).
  window.AppWindow().Closing([this](auto &&, winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const &e) {
    e.Cancel(true);
    close();
  });
  state.trusted = true;
  state.system = os::ui_language();
  poll_ = page::every(300, [this] {
    state.done = dl_.get_done(), state.total = dl_.get_total();
    look_ = app::first_run(c_.data(), state);
    if (!bar_ || !look_.progress) return draw();
    if (look_.fraction >= 0) bar_.IsIndeterminate(false), bar_.Value(look_.fraction);
    if (progress_) progress_.Text(h(look_.progress_text));
  });
  refresh(theme);
}

void FirstRunWindow::refresh(xaml::ElementTheme theme) {
  retheme(window, theme, tr("fr_window"));
  draw_again();
}

void FirstRunWindow::draw() {
  step_.Text(h(look_.step_text));
  title_.Text(h(look_.title));
  text_.Text(h(look_.text));
  text_.Visibility(look_.text.empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  body_.Children().Clear();
  bar_ = nullptr, progress_ = nullptr;
  auto add = [this](const xaml::UIElement &e) { body_.Children().Append(e); };
  if (look_.step == FirstStep::pair) {
    add(page::text(look_.answer_label));
    controls::ComboBox answers;
    answers.MinWidth(260);
    answers.Margin({0, 0, 0, 12});
    for (auto &a : look_.answers) {
      controls::ComboBoxItem i;
      i.Content(winrt::box_value(h(a.title)));
      i.Tag(winrt::box_value(h(a.code)));
      answers.Items().Append(i);
      if (a.code == look_.answer) answers.SelectedItem(i);
    }
    answers.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const &sender, auto &&) {
      auto i = sender.as<controls::ComboBox>().SelectedItem();
      if (!i) return;
      state.answer = winrt::to_string(winrt::unbox_value<winrt::hstring>(i.as<controls::ComboBoxItem>().Tag()));
      state.book.clear();
      c_.ui([this] { draw_again(); });
    });
    add(answers);
    add(page::text(look_.book_label));
    // The language on the left, its level and size on the right (Settings' list look: a card with rows).
    controls::ListView books;
    books.SelectionMode(controls::ListViewSelectionMode::Single);
    books.Height(232);
    books.Padding({0, 4, 0, 4});
    books.CornerRadius({8, 8, 8, 8});
    books.BorderThickness({1, 1, 1, 1});
    books.Background(xaml::Application::Current().Resources().Lookup(winrt::box_value(L"CardBackgroundFillColorDefaultBrush")).as<xaml::Media::Brush>());
    books.BorderBrush(xaml::Application::Current().Resources().Lookup(winrt::box_value(L"CardStrokeColorDefaultBrush")).as<xaml::Media::Brush>());
    controls::ListViewItem picked{nullptr};
    for (auto &b : look_.books) {
      controls::Grid line;
      line.ColumnDefinitions().Append(controls::ColumnDefinition());
      line.ColumnDefinitions().Append(controls::ColumnDefinition());
      line.ColumnDefinitions().GetAt(1).Width(xaml::GridLengthHelper::Auto());
      line.ColumnSpacing(12);
      auto title = page::text(b.title);
      title.VerticalAlignment(xaml::VerticalAlignment::Center);
      auto detail = page::text(b.detail, L"BodyTextBlockStyle", true);
      detail.VerticalAlignment(xaml::VerticalAlignment::Center);
      controls::Grid::SetColumn(detail, 1);
      line.Children().Append(title);
      line.Children().Append(detail);
      controls::ListViewItem i;
      i.Content(line);
      i.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
      i.Tag(winrt::box_value(h(b.code)));
      books.Items().Append(i);
      if (b.code == look_.book) picked = i;
    }
    if (picked) {
      books.SelectedItem(picked);
      books.Loaded([picked](winrt::Windows::Foundation::IInspectable const &sender, auto &&) { sender.as<controls::ListView>().ScrollIntoView(picked); });
    }
    books.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const &sender, auto &&) {
      auto i = sender.as<controls::ListView>().SelectedItem();
      if (!i) return;
      state.book = winrt::to_string(winrt::unbox_value<winrt::hstring>(i.as<controls::ListViewItem>().Tag()));
      c_.ui([this] { draw_again(); });
    });
    add(books);
  }
  if ((look_.step == FirstStep::download || look_.step == FirstStep::list) && look_.progress) {
    bar_ = page::progress(look_.fraction);
    add(bar_);
    if (!look_.progress_text.empty()) add(progress_ = page::text(look_.progress_text, L"CaptionTextBlockStyle", true));
  }
  if (!look_.error.empty()) add(page::info(look_.error, controls::InfoBarSeverity::Error));
  if (!look_.note.empty()) add(page::text(look_.note, L"CaptionTextBlockStyle", true));
  buttons_.Children().Clear();
  if (!look_.secondary.empty()) buttons_.Children().Append(page::button(h(look_.secondary), [this] { other(); }));
  if (!look_.button.empty()) {
    auto b = page::button(h(look_.button), [this] { primary(); }, true);
    b.IsEnabled(look_.button_enabled);
    buttons_.Children().Append(b);
  }
}

void FirstRunWindow::start() {
  if (look_.step == FirstStep::list || look_.step == FirstStep::pair) fetch();
}

void FirstRunWindow::primary() {
  switch (look_.step) {
    case FirstStep::list: return fetch();
    case FirstStep::pair: return install();
    case FirstStep::download:
      if (state.downloading) return dl_.cancel();
      return install();
    default: close();
  }
}

void FirstRunWindow::other() {
  if (look_.step == FirstStep::download) state.error.clear(), draw_again();
}

// Done or its X: the download stopped, the first run finished (it opens again at launch only without a pair).
void FirstRunWindow::close() {
  dl_.cancel();
  app::finish_first_run(c_.data());
  window.AppWindow().Hide();
  if (finished) finished();
}

// The release's list: fresh, or kept from the last time.
void FirstRunWindow::fetch() {
  if (state.fetching) return;
  state.fetching = true;
  draw_again();
  auto dir = c_.data();
  page::run<std::string>(c_, [dir] { return app::refresh(dir); }, [this](std::string e) {
    state.list_error = e;
    state.fetching = false;
    draw_again();
  });
}

// The picked pair: its answer language for the next cards, then what it still needs, with progress.
void FirstRunWindow::install() {
  if (state.downloading || look_.book.empty()) return;
  auto dir = c_.data();
  auto answer = look_.answer, book = look_.book;
  app::set_answer(dir, answer);
  c_.configure();
  state.answer = answer, state.book = book;
  auto ids = app::missing_ids(dir, book, answer, c_.device);
  state.error.clear();
  state.done = state.total = 0;
  state.downloading = true;
  dl_ = app::Download();
  auto dl = dl_;
  poll_.Start();
  page::run<Status>(c_, [dir, ids, dl] { return app::install(dir, ids, dl); }, [this](Status s) {
    poll_.Stop();
    state.error = s.cancelled ? "" : s.error;
    state.downloading = false;
    c_.reload();
    draw_again();
  });
  draw_again();
}

void FirstRunWindow::draw_again() {
  look_ = app::first_run(c_.data(), state);
  draw();
}

}  // namespace lexipeek::win
