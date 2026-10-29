#include "pch.h"
#include "packs_pane.hpp"
#include "settings_ui.hpp"

namespace lexipeek::win {

namespace controls = xaml::Controls;

PacksPane::PacksPane(CoreClient &client) : c_(client) {
  view_ = app::packs(c_.data(), c_.device);
  poll_ = page::every(300, [this] {
    auto total = dl_.get_total();
    if (bar_ && total > 0) bar_.Value(static_cast<double>(dl_.get_done()) / total);
  });
}

xaml::UIElement PacksPane::view() {
  controls::StackPanel body;
  body.Spacing(4);
  bar_ = nullptr;
  if (busy_) {
    bar_ = page::progress(busy_->empty() ? -1 : 0);
    auto total = dl_.get_total();
    if (!busy_->empty() && total > 0) bar_.Value(static_cast<double>(dl_.get_done()) / total);
    xaml::UIElement cancel{nullptr};
    if (!busy_->empty()) cancel = page::button(tr("cancel"), [this] { dl_.cancel(); });
    body.Children().Append(page::card(0xE896, busy_->empty() ? app::text("packs_checking") : *busy_, "", cancel, bar_));
  }
  for (auto *e : {&error_, &list_error_})
    if (!e->empty()) body.Children().Append(page::info(*e, controls::InfoBarSeverity::Error));
  controls::StackPanel top;
  top.Orientation(controls::Orientation::Horizontal);
  top.Spacing(8);
  auto dir = c_.data().empty() ? app::data_dir() : c_.data();
  auto show = page::link(tr("show_finder"), dir);
  show.VerticalAlignment(xaml::VerticalAlignment::Center);
  top.Children().Append(show);
  auto check = page::button(tr("packs_refresh"), [this] { refresh(); });
  check.IsEnabled(!busy_);
  top.Children().Append(check);
  body.Children().Append(page::card(0xE8F1, view_.summary, "", top));
  auto group = [&](const std::string &title, const std::vector<PackLine> &lines) {
    if (lines.empty()) return;
    body.Children().Append(page::heading(title, "", false));
    for (auto &l : lines) body.Children().Append(line(l));
  };
  group(view_.installed_title, view_.installed);
  group(view_.available_title, view_.available);
  if (!view_.empty.empty()) body.Children().Append(page::text(view_.empty, L"BodyTextBlockStyle", true));
  return body;
}

xaml::UIElement PacksPane::line(const PackLine &l) {
  controls::StackPanel right;
  right.Orientation(controls::Orientation::Horizontal);
  right.Spacing(4);
  if (!l.button.label.empty()) {
    auto a = l.button;
    auto b = page::button(h(a.label), [this, a] { run(a); });
    b.IsEnabled(!busy_);
    right.Children().Append(b);
  }
  if (!l.menu.empty()) {
    controls::MenuFlyout menu;
    for (auto &a : l.menu) {
      if (a.divider) menu.Items().Append(controls::MenuFlyoutSeparator());
      controls::MenuFlyoutItem x;
      x.Text(h(a.label));
      x.IsEnabled(a.enabled);
      x.Click([this, a](auto &&, auto &&) { run(a); });
      menu.Items().Append(x);
    }
    auto more = page::icon_button(0xE712, tr("more"), nullptr);
    more.Flyout(menu);
    more.IsEnabled(!busy_);
    right.Children().Append(more);
  }
  return page::card(0xE8C1, l.title, l.detail, right);
}

void PacksPane::refresh() {
  busy_ = "";
  if (redraw) redraw();
  auto dir = c_.data();
  page::run<std::string>(c_, [dir] { return app::refresh(dir); }, [this](std::string e) {
    busy_.reset();
    list_error_ = e;
    view_ = app::packs(c_.data(), c_.device);
    if (redraw) redraw();
  });
}

void PacksPane::run(const PackAction &a) {
  if (busy_ || a.ids.empty()) return;
  busy_ = a.what;
  error_.clear();
  auto dir = c_.data();
  auto ids = a.ids;
  if (a.remove) {
    page::run<Status>(c_, [dir, ids] { return app::remove(dir, ids); }, [this](Status s) { finished(s); });
  } else {
    dl_ = app::Download();
    auto dl = dl_;
    poll_.Start();
    page::run<Status>(c_, [dir, ids, dl] { return app::install(dir, ids, dl); }, [this](Status s) { finished(s); });
  }
  if (redraw) redraw();
}

void PacksPane::finished(const Status &s) {
  poll_.Stop();
  busy_.reset();
  error_ = s.cancelled ? "" : s.error;
  view_ = app::packs(c_.data(), c_.device);
  c_.reload();
  if (redraw) redraw();
  if (changed) changed();
}

}  // namespace lexipeek::win
