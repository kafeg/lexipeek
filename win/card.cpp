#include "pch.h"
#include "card.hpp"
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <algorithm>
#include <cmath>
#include "lexipeek/app.hpp"

namespace lexipeek::win {

namespace controls = xaml::Controls;
namespace docs = xaml::Documents;
using winrt::Windows::Foundation::IInspectable;
using Style = CardRow::Style;

namespace {
std::string trimmed(const std::string &s) {
  auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace

int NotesBus::listen(std::function<void()> f) {
  listeners_[next_] = std::move(f);
  return next_++;
}
void NotesBus::forget(int id) { listeners_.erase(id); }
void NotesBus::changed() {
  auto all = listeners_;  // a listener may add or forget one
  for (auto &[id, f] : all) f();
}
NotesBus &notes_bus() {
  static auto *bus = new NotesBus;  // never destroyed: the process leaves by app::leave
  return *bus;
}

CardModel::CardModel(CoreClient &client) : c_(client), session_(app::CardSession::make(client.engine)) {
  poll_ = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
  poll_.Interval(std::chrono::milliseconds(40));
  poll_.Tick([this](auto &&, auto &&) {
    if (session_.poll()) relook();
    if (session_.notes_changed()) notes_bus().changed();
    if (!session_.busy()) poll_.Stop();
  });
  // Notes may have removed (or another card saved) this card's word: its ★ again.
  notes_ = notes_bus().listen([this] {
    session_.recheck_saved();
    if (session_.poll()) relook();
  });
}

CardModel::~CardModel() {
  poll_.Stop();
  notes_bus().forget(notes_);
}

void CardModel::relook() {
  nav = session_.nav();
  look_ = session_.view();
  auto all = changed;
  for (auto &f : all)
    if (f) f();
}

void CardModel::acted() {
  if (session_.poll()) relook();
  if (session_.notes_changed()) notes_bus().changed();
  if (session_.busy() && !poll_.IsRunning()) poll_.Start();
}

void CardModel::reset() {
  pinned = false;
  unread.clear();
  session_.reset();
  acted();
}

void CardModel::lookup(const std::string &text, const std::string &lang, const std::string &answer) {
  session_.set_key(key());
  session_.look(trimmed(text), source, lang, answer);
  acted();
}
void CardModel::open(const std::string &word) { session_.set_key(key()), session_.open(word), acted(); }
void CardModel::relookup(const std::string &lang, const std::string &answer) { session_.set_key(key()), session_.relookup(lang, answer), acted(); }
void CardModel::back() { session_.set_key(key()), session_.back(), acted(); }
void CardModel::open_section(int entry, const std::string &section) { session_.open_section(entry, section), acted(); }
void CardModel::turn(int by) { session_.turn(by), acted(); }
void CardModel::open_entry(int entry) { session_.open_entry(entry), acted(); }
void CardModel::select(CardTab tab) { session_.set_key(key()), session_.select(tab), acted(); }
void CardModel::next_tab(int by) { session_.set_key(key()), session_.next_tab(by), acted(); }
void CardModel::full_entries() { session_.full_entries(), acted(); }

void CardModel::choose(Method m) {
  app::pick_method(c_.data(), m);
  c_.configure();
  use(m);
}

void CardModel::use(Method m) { session_.set_key(key()), session_.use(m), acted(); }
void CardModel::choose_ai() { session_.set_key(key()), session_.choose_ai(), acted(); }
void CardModel::toggle_saved() { session_.toggle_saved(), acted(); }
void CardModel::cancel() { session_.cancel(), acted(); }

void CardModel::toggle_pin() {
  pinned = !pinned;
  relook();
}

namespace {

// Windows 11's text and fill colours (its design tokens) for the card's theme: runs take no theme resources.
xaml::Media::SolidColorBrush brush(uint32_t argb) {
  return xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{static_cast<uint8_t>(argb >> 24), static_cast<uint8_t>(argb >> 16),
                                                                static_cast<uint8_t>(argb >> 8), static_cast<uint8_t>(argb)});
}
xaml::Media::SolidColorBrush primary(bool dark) { return brush(dark ? 0xFFFFFFFF : 0xE4000000); }
xaml::Media::SolidColorBrush secondary(bool dark) { return brush(dark ? 0xC5FFFFFF : 0x9E000000); }
xaml::Media::SolidColorBrush critical(bool dark) { return brush(dark ? 0xFFFF99A4 : 0xFFC42B1C); }
xaml::Media::SolidColorBrush accent(bool dark) {
  using winrt::Windows::UI::ViewManagement::UIColorType;
  auto c = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(dark ? UIColorType::AccentLight2 : UIColorType::AccentDark1);
  return xaml::Media::SolidColorBrush(c);
}
xaml::Media::SolidColorBrush clear() { return xaml::Media::SolidColorBrush(winrt::Microsoft::UI::Colors::Transparent()); }

xaml::Media::FontFamily symbols() { return xaml::Media::FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"); }

controls::TextBlock text(const std::string &s, double size, bool select = true) {
  controls::TextBlock t;
  t.Text(h(s));
  t.FontSize(size);
  t.TextWrapping(xaml::TextWrapping::Wrap);
  t.IsTextSelectionEnabled(select);
  return t;
}

docs::Run run(const std::string &s, bool bold = false, bool italic = false) {
  docs::Run r;
  r.Text(h(s));
  if (bold) r.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
  if (italic) r.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
  return r;
}

// Rich text (the core's spans) as runs and line breaks.
void add(const docs::InlineCollection &to, const std::vector<Span> &spans) {
  for (auto &s : spans) {
    size_t at = 0;
    for (;;) {
      auto nl = s.text.find('\n', at);
      if (auto part = s.text.substr(at, nl == std::string::npos ? std::string::npos : nl - at); !part.empty()) to.Append(run(part, s.bold, s.italic));
      if (nl == std::string::npos) break;
      to.Append(docs::LineBreak());
      at = nl + 1;
    }
  }
}

// A row's link in the text's own colour (a hand over it): a click does what it says.
docs::Hyperlink link(bool dark, std::function<void()> click) {
  docs::Hyperlink l;
  l.UnderlineStyle(docs::UnderlineStyle::None);
  l.Foreground(primary(dark));
  l.Click([click](auto &&, auto &&) { click(); });
  return l;
}

void tip(const xaml::DependencyObject &o, const std::string &s) {
  if (!s.empty()) controls::ToolTipService::SetToolTip(o, winrt::box_value(h(s)));
}

// A command of the bottom row: the system's icon (16 px, as Windows 11's command bars), its tooltip.
controls::Button command(wchar_t glyph, const std::string &tooltip, std::function<void()> click, xaml::Media::Brush color = nullptr, double size = 16) {
  controls::FontIcon f;
  f.FontFamily(symbols());
  f.Glyph(winrt::hstring(std::wstring(1, glyph)));
  f.FontSize(size);
  if (color) f.Foreground(color);
  controls::Button b;
  b.Content(f);
  b.Width(32), b.Height(32), b.Padding({0, 0, 0, 0});
  b.Background(clear()), b.BorderThickness({0, 0, 0, 0});
  tip(b, tooltip);
  if (click) b.Click([click](auto &&, auto &&) { click(); });
  return b;
}

// A menu button of the bottom row (the method, the badge): its text and the chevron.
controls::DropDownButton menu_button(const std::string &title, const controls::MenuFlyout &menu, double size) {
  controls::DropDownButton b;
  b.Content(winrt::box_value(h(title)));
  b.FontSize(size);
  b.Height(32), b.Padding({8, 0, 6, 0});
  b.Background(clear()), b.BorderThickness({0, 0, 0, 0});
  b.Flyout(menu);
  return b;
}

controls::MenuFlyoutItem menu_item(const std::string &title, std::function<void()> click, bool enabled = true) {
  controls::MenuFlyoutItem i;
  i.Text(h(title));
  i.IsEnabled(enabled);
  if (click) i.Click([click](auto &&, auto &&) { click(); });
  return i;
}

void copy_text(const std::string &s) {
  auto w = winrt::to_hstring(s);
  if (!OpenClipboard(nullptr)) return;
  EmptyClipboard();
  if (auto g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
    memcpy(GlobalLock(g), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(g);
    if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
  }
  CloseClipboard();
}

}  // namespace

CardWidget::CardWidget(CardModel &model, bool embedded) : m_(model), embedded_(embedded) {
  root_.HorizontalAlignment(embedded ? xaml::HorizontalAlignment::Stretch : xaml::HorizontalAlignment::Left);
  root_.VerticalAlignment(xaml::VerticalAlignment::Stretch);
  root_.IsTabStop(true);  // the card's own keys once its field is gone
  root_.UseSystemFocusVisuals(false);
  // Drawn again once the event that changed it is over (a click's handler may be the one that changed it).
  m_.changed.push_back([this] {
    if (pending_) return;
    pending_ = true;
    winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue([this] {
      pending_ = false;
      rebuild();
    });
  });
  root_.ActualThemeChanged([this](auto &&, auto &&) { rebuild(); });
  rebuild();
}

double CardWidget::px(int delta) const { return (v_.text > 0 ? v_.text : 13) + 1 + delta; }  // 13 pt on the Mac: Windows' 14 px Body

void CardWidget::focus_field() {
  if (field_) field_.Focus(xaml::FocusState::Programmatic);
}

bool CardWidget::scroll_to_end() {
  if (!body_ || body_.ScrollableHeight() <= 0) return false;
  body_.ChangeView(nullptr, winrt::Windows::Foundation::IReference<double>(body_.ScrollableHeight()), nullptr, true);
  return true;
}

void CardWidget::show_whole(bool on) {
  whole_ = on;
  winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue([this] { rebuild(); });
}

void CardWidget::rebuild() {
  v_ = m_.look();
  if (auto place = m_.nav.place(); place != place_) place_ = place, whole_ = false, cut_ = false;
  bool dark = root_.ActualTheme() == xaml::ElementTheme::Dark;
  limit_ = screen * (v_.height > 0 ? v_.height : 45) / 100;
  root_.FontFamily(xaml::Media::FontFamily(v_.font == "serif" ? L"Georgia, Cambria" : v_.font == "mono" ? L"Cascadia Mono, Consolas" : L"Segoe UI Variable Text, Segoe UI"));
  controls::Grid grid;
  for (auto height : {xaml::GridLengthHelper::Auto(), xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star), xaml::GridLengthHelper::Auto()}) {
    controls::RowDefinition r;
    r.Height(height);
    grid.RowDefinitions().Append(r);
  }
  if (!embedded_) grid.Width(v_.width > 0 ? v_.width : 400);
  // A flyout's acrylic fallback where nothing is behind it; else clear (a drag on the background moves the card).
  grid.Background(solid ? brush(dark ? 0xFF2C2C2C : 0xFFF9F9F9) : clear());
  controls::StackPanel top;
  top.Padding({16, 12, 16, 8});
  top.Spacing(8);
  grid.Children().Append(top);
  field_ = nullptr, recents_ = nullptr, body_ = nullptr;
  if (!m_.unread.empty()) {
    auto u = text(app::text("card_unread", m_.unread), px(-1), false);
    u.Foreground(secondary(dark));
    top.Children().Append(u);
  }
  if (m_.input) top.Children().Append(field());
  if (!m_.has_card()) {
    top.Padding({16, 12, 16, 12});
    if (m_.looking()) top.Children().Append(text("…", px(), false));
    root_.Content(grid);
    if (drawn) drawn();
    return;
  }
  if (!(embedded_ && v_.style == HeaderStyle::passage)) top.Children().Append(header(v_));
  if (v_.tabs) top.Children().Append(tabs(v_));

  controls::StackPanel body;
  body.Padding({16, 0, 20, 12});
  body.Spacing(8);
  for (auto &r : v_.rows) body.Children().Append(row(r));
  if (!v_.fix.empty()) {
    controls::HyperlinkButton fix;
    fix.Content(winrt::box_value(h(v_.fix)));
    fix.FontSize(px(-2));
    fix.Padding({0, 2, 0, 2});
    auto section = static_cast<int>(v_.fix_section), tab = v_.fix_tab;
    fix.Click([this, section, tab](auto &&, auto &&) {
      if (open_pane) open_pane(section, tab);
    });
    body.Children().Append(fix);
  }
  if (embedded_) body.Children().Append(bar(v_));  // in the window the bar follows the text
  controls::ScrollViewer scroll;
  scroll.Content(body);
  scroll.HorizontalScrollBarVisibility(controls::ScrollBarVisibility::Disabled);
  scroll.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
  scroll.VerticalAlignment(xaml::VerticalAlignment::Top);
  if (!embedded_) scroll.MaxHeight(limit_);
  controls::Grid::SetRow(scroll, 1);
  grid.Children().Append(scroll);
  body_ = scroll;
  if (!embedded_) {
    auto b = bar(v_);
    controls::Grid::SetRow(b, 2);  // at the foot while the pop-up keeps a taller height
    grid.Children().Append(b);
  }
  root_.Content(grid);
  if (drawn) drawn();
}

// As InkPeek's title: a word's or a phrase's two lines, a passage's one, cut with "…"; a click on a cut one shows it whole
// (scrolling, half the card's limit at most) with ⌃ to fold it again.
xaml::FrameworkElement CardWidget::header(const CardView &v) {
  bool dark = root_.ActualTheme() == xaml::ElementTheme::Dark;
  controls::StackPanel p;
  p.Spacing(2);
  // Windows' type ramp: a word as Subtitle (20 px semibold), a phrase as Body Strong, a passage as Body, secondary.
  double size = v.style == HeaderStyle::word ? px(6) : px();
  auto title = text(v.title, size, false);
  if (v.style != HeaderStyle::passage) title.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
  else title.Foreground(secondary(dark));
  if (whole_ && cut_) {
    title.IsTextSelectionEnabled(true);
    controls::ScrollViewer s;
    s.Content(title);
    s.MaxHeight(limit_ / 2);
    s.HorizontalScrollBarVisibility(controls::ScrollBarVisibility::Disabled);
    p.Children().Append(s);
    auto fold = command(0xE70E, app::text("card_collapse"), [this] { show_whole(false); }, nullptr, 12);
    fold.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    fold.Height(24);
    p.Children().Append(fold);
  } else {
    title.MaxLines(v.style == HeaderStyle::passage ? 1 : 2);
    title.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
    tip(title, v.tooltip);
    auto tooltip = v.tooltip;
    title.IsTextTrimmedChanged([this, tooltip](controls::TextBlock const &t, auto &&) {
      cut_ = t.IsTextTrimmed();
      tip(t, cut_ ? app::text("card_whole") : tooltip);
    });
    title.Tapped([this](auto &&, auto &&) {
      if (cut_) show_whole(true);
    });
    p.Children().Append(title);
  }
  if (!v.subtitle.empty()) {
    auto s = text(v.subtitle, px(-2), false);
    s.Foreground(secondary(dark));
    p.Children().Append(s);
  }
  return p;
}

// The card's tabs as Windows 11's SelectorBar; a smaller size when they would not fit the card.
xaml::FrameworkElement CardWidget::tabs(const CardView &v) {
  controls::SelectorBar bar;
  bar.Margin({-12, 0, 0, 0});  // its items' text under the title's
  controls::SelectorBarItem selected{nullptr};
  std::vector<controls::SelectorBarItem> items;
  for (auto &t : v.tab_items) {
    controls::SelectorBarItem i;
    i.Text(h(t.title));
    i.FontSize(px());
    i.Tag(winrt::box_value(static_cast<int>(t.tab)));
    bar.Items().Append(i);
    items.push_back(i);
    if (t.tab == v.tab) selected = i;
  }
  if (selected) bar.SelectedItem(selected);
  bar.SelectionChanged([this](controls::SelectorBar const &b, auto &&) {
    auto item = b.SelectedItem();
    if (!item) return;
    auto tab = static_cast<CardTab>(winrt::unbox_value<int>(item.Tag()));
    if (tab != v_.tab) m_.select(tab);
  });
  double room = (embedded_ ? std::max(root_.ActualWidth(), 300.0) : (v.width > 0 ? v.width : 400)) - 32;
  bar.Loaded([items, room, size = px()](IInspectable const &sender, auto &&) {
    auto b = sender.as<controls::SelectorBar>();
    for (double s = size; s > size - 3; --s) {
      b.Measure({INFINITY, INFINITY});
      if (b.DesiredSize().Width <= room) break;
      for (auto &i : items) i.FontSize(s - 1);
    }
  });
  return bar;
}

xaml::FrameworkElement CardWidget::row(const CardRow &r) {
  bool dark = root_.ActualTheme() == xaml::ElementTheme::Dark;
  auto grey = [dark](controls::TextBlock t) {
    t.Foreground(secondary(dark));
    return t;
  };
  switch (r.style) {
    case Style::entry: {
      controls::StackPanel p;
      p.Spacing(2);
      if (!r.label.empty()) p.Children().Append(grey(text(r.label, px(-2), false)));
      if (r.open == "entry") {
        // A dictionary's short line (its headword and meaning): a click opens its full entry alone.
        auto entry = r.entry;
        auto t = text("", px(), false);
        auto l = link(dark, [this, entry] { m_.open_entry(entry); });
        l.Inlines().Append(run(r.text));
        t.Inlines().Append(l);
        tip(t, app::text("card_entry_tip"));
        p.Children().Append(t);
      } else if (!r.html.empty()) {
        auto t = text("", px());
        add(t.Inlines(), html_spans(r.html));
        p.Children().Append(t);
      } else {
        p.Children().Append(text(r.text, px()));
      }
      return p;
    }
    case Style::gloss: {
      auto t = text("", px(), r.link.empty());
      auto meaning = run(r.text.empty() ? "" : " " + r.text);
      meaning.Foreground(secondary(dark));
      if (r.link.empty()) {
        t.Inlines().Append(run(r.label, true));
        t.Inlines().Append(meaning);
        return t;
      }
      auto word = r.link;
      auto l = link(dark, [this, word] { m_.open(word); });
      l.Inlines().Append(run(r.label, true));
      t.Inlines().Append(l);
      t.Inlines().Append(meaning);
      tip(t, app::text("card_open_word", word));
      return t;
    }
    case Style::heading: {
      auto t = text(r.text, px());
      t.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
      return t;
    }
    case Style::sense: {
      auto t = text("", px());
      auto n = run(r.label + "  ");
      n.Foreground(secondary(dark));
      t.Inlines().Append(n);
      t.Inlines().Append(run(r.text));
      return t;
    }
    case Style::example: {
      auto t = grey(text(r.text, px(-1)));
      t.Margin({24, 0, 0, 0});
      return t;
    }
    case Style::section: {
      auto entry = r.entry;
      auto key = r.open;
      auto t = text("", px(), false);
      auto l = link(dark, [this, entry, key] { m_.open_section(entry, key); });
      l.Inlines().Append(run(r.label));
      t.Inlines().Append(l);
      t.Inlines().Append(docs::LineBreak());
      auto caption = run(r.text);
      caption.FontSize(px(-2));
      caption.Foreground(secondary(dark));
      t.Inlines().Append(caption);
      return t;
    }
    case Style::text: return text(r.text, px(1));
    case Style::markdown: {
      auto t = text("", px());
      add(t.Inlines(), markdown_spans(r.text));
      return t;
    }
    case Style::english: return grey(text(r.text, px(-1)));
    case Style::error: {
      auto t = text(r.text, px());
      t.Foreground(critical(dark));
      return t;
    }
    case Style::progress: {
      controls::StackPanel p;
      p.Orientation(controls::Orientation::Horizontal);
      p.Spacing(8);
      controls::ProgressRing ring;
      ring.IsActive(true);
      ring.Width(16), ring.Height(16);
      p.Children().Append(ring);
      p.Children().Append(grey(text(r.text, px(), false)));
      return p;
    }
    default: return grey(text(r.text, px(), false));
  }
}

// The bottom row (InkPeek's): ←, the method, a pager; copy, the badge, ☆, the pin.
xaml::FrameworkElement CardWidget::bar(const CardView &v) {
  bool dark = root_.ActualTheme() == xaml::ElementTheme::Dark;
  controls::Grid g;
  for (auto width : {xaml::GridLengthHelper::Auto(), xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star), xaml::GridLengthHelper::Auto()}) {
    controls::ColumnDefinition c;
    c.Width(width);
    g.ColumnDefinitions().Append(c);
  }
  if (!embedded_) {
    // A footer as Windows 11's dialogs have: a shade darker, a hairline over it.
    g.Padding({8, 4, 8, 4});
    g.Background(brush(dark ? 0x40000000 : 0x0A000000));
    g.BorderThickness({0, 1, 0, 0});
    g.BorderBrush(brush(dark ? 0x15FFFFFF : 0x0F000000));
  }
  controls::StackPanel left, right;
  for (auto &p : {left, right}) p.Orientation(controls::Orientation::Horizontal), p.Spacing(2), p.VerticalAlignment(xaml::VerticalAlignment::Center);
  controls::Grid::SetColumn(right, 2);
  g.Children().Append(left);
  g.Children().Append(right);
  auto size = px(-2);
  // ← with where it goes as its tooltip.
  if (!v.back.empty()) left.Children().Append(command(0xE72B, v.back, [this] { m_.back(); }));
  if (v.menu) {
    controls::MenuFlyout menu;
    for (auto &m : v.methods) {
      auto method = m.method;
      bool ai = m.ai;
      menu.Items().Append(menu_item(m.title, [this, method, ai] { ai ? m_.choose_ai() : m_.choose(method); }, m.enabled));
    }
    auto b = menu_button(v.menu_title, menu, size);
    tip(b, v.menu_help);
    left.Children().Append(b);
  }
  if (!v.pager.empty()) {
    auto prev = command(0xE76B, "", [this] { m_.turn(-1); }, nullptr, 12);
    prev.IsEnabled(v.prev);
    left.Children().Append(prev);
    auto pager = text(v.pager, size, false);
    pager.TextWrapping(xaml::TextWrapping::NoWrap);
    pager.VerticalAlignment(xaml::VerticalAlignment::Center);
    pager.Foreground(secondary(dark));
    left.Children().Append(pager);
    auto next = command(0xE76C, "", [this] { m_.turn(1); }, nullptr, 12);
    next.IsEnabled(v.next);
    left.Children().Append(next);
  }
  // A pager's row has no room for copy (and a page of words has nothing to copy).
  if (!v.copy.empty() && v.pager.empty()) {
    auto copied = v.copy;
    auto b = command(0xE8C8, app::text("card_copy"), nullptr);
    // Copied: a check mark for a moment, so the click is seen.
    b.Click([copied](IInspectable const &sender, auto &&) {
      copy_text(copied);
      auto icon = sender.as<controls::Button>().Content().as<controls::FontIcon>();
      icon.Glyph(L"\xE73E");
      auto t = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
      t.IsRepeating(false);
      t.Interval(std::chrono::milliseconds(1500));
      t.Tick([icon, t](auto &&, auto &&) {  // the timer holds itself until it fires
        icon.Glyph(L"\xE8C8");
        t.Stop();
      });
      t.Start();
    });
    right.Children().Append(b);
  }
  if (!v.books.empty() || !v.answers.empty()) {
    controls::MenuFlyout menu;
    auto group = [&](const char *title, const std::vector<LangItem> &items, bool books) {
      if (items.empty()) return;
      if (menu.Items().Size()) menu.Items().Append(controls::MenuFlyoutSeparator());
      menu.Items().Append(menu_item(app::text(title), nullptr, false));
      for (auto &x : items) {
        controls::RadioMenuFlyoutItem i;
        i.Text(h(x.title));
        i.GroupName(books ? L"books" : L"answers");
        i.IsChecked(x.on);
        auto code = x.code;
        i.Click([this, code, books](auto &&, auto &&) { books ? m_.relookup(code, "") : m_.relookup("", code); });
        menu.Items().Append(i);
      }
    };
    group("card_text_in", v.books, true);
    group("card_into", v.answers, false);
    right.Children().Append(menu_button(v.badge, menu, size));
  } else if (!v.badge.empty()) {
    auto badge = text(v.badge, size, false);
    badge.VerticalAlignment(xaml::VerticalAlignment::Center);
    badge.Margin({8, 0, 8, 0});
    right.Children().Append(badge);
  }
  if (v.can_save) right.Children().Append(command(v.saved ? 0xE735 : 0xE734, v.star_help, [this] { m_.toggle_saved(); }, v.saved ? accent(dark) : nullptr));
  if (!embedded_)
    right.Children().Append(command(m_.pinned ? 0xE840 : 0xE718, app::text("card_pin"), [this] { m_.toggle_pin(); }, m_.pinned ? accent(dark) : nullptr));
  return g;
}

// The text field (typing): Return looks it up; on an empty card the recent look-ups under it (a click, or ↑↓ and
// Return, opens one).
xaml::FrameworkElement CardWidget::field() {
  controls::StackPanel p;
  p.Spacing(6);
  controls::TextBox e;
  e.Text(h(typed_));
  e.PlaceholderText(tr("card_input_hint"));
  e.FontSize(px());
  e.TextChanged([this](IInspectable const &sender, auto &&) { typed_ = winrt::to_string(sender.as<controls::TextBox>().Text()); });
  e.KeyDown([this](auto &&, xaml::Input::KeyRoutedEventArgs const &k) {
    if (k.Key() != winrt::Windows::System::VirtualKey::Enter) return;
    k.Handled(true);
    auto pick = typed_;
    if (recents_ && trimmed(typed_).empty() && recents_.SelectedItem())
      pick = winrt::to_string(winrt::unbox_value<winrt::hstring>(recents_.SelectedItem().as<controls::ListViewItem>().Tag()));
    if (submitted) submitted(pick);
  });
  p.Children().Append(e);
  field_ = e;
  if (!m_.has_card() && !m_.looking()) {
    auto seen = m_.recents();
    if (!seen.empty()) {
      controls::ListView list;
      list.SelectionMode(controls::ListViewSelectionMode::Single);
      list.IsTabStop(false);
      list.Margin({-4, 0, -4, 0});
      for (auto &s : seen) {
        controls::ListViewItem i;
        i.Content(winrt::box_value(h(app::menu_line(s.meaning.empty() ? s.text : s.text + " — " + s.meaning))));
        i.Tag(winrt::box_value(h(s.text)));
        i.FontSize(px(-1));
        i.MinHeight(30);
        i.Padding({8, 0, 8, 0});
        i.IsTabStop(false);
        auto t = s.text;
        i.Tapped([this, t](auto &&, auto &&) {
          if (submitted) submitted(t);
        });
        list.Items().Append(i);
      }
      p.Children().Append(list);
      recents_ = list;
      // ↑↓ in the field: through the recent look-ups.
      e.PreviewKeyDown([this](auto &&, xaml::Input::KeyRoutedEventArgs const &k) {
        using winrt::Windows::System::VirtualKey;
        if (!recents_ || (k.Key() != VirtualKey::Down && k.Key() != VirtualKey::Up)) return;
        int n = static_cast<int>(recents_.Items().Size());
        recents_.SelectedIndex(std::clamp(recents_.SelectedIndex() + (k.Key() == VirtualKey::Down ? 1 : -1), -1, n - 1));
        k.Handled(true);
      });
    }
  }
  if (!m_.has_card()) e.Loaded([](IInspectable const &sender, auto &&) { sender.as<controls::TextBox>().Focus(xaml::FocusState::Programmatic); });
  return p;
}

}  // namespace lexipeek::win
