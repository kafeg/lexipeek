#include "packs_pane.hpp"
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include "fluent.hpp"
#include "ui.hpp"

namespace lexipeek::qt {

PacksPane::PacksPane(CoreClient &client) : c_(client) {
  auto *inner = new QWidget;
  body_ = new QVBoxLayout(inner);
  body_->setContentsMargins(20, 12, 20, 16);
  body_->setSpacing(10);
  auto *scroll = ui::scroll(inner);
  auto *l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->addWidget(scroll);
  poll_ = new QTimer(this);
  connect(poll_, &QTimer::timeout, this, [this] {
    auto total = dl_.get_total();
    progress_ = total > 0 ? static_cast<double>(dl_.get_done()) / total : 0;
    draw();
  });
  view_ = app::packs(c_.data(), c_.device);
  draw();
}

void PacksPane::draw() {
  auto *area = findChild<QScrollArea *>();
  int scrolled = area->verticalScrollBar()->value();
  ui::clear(body_);
  if (busy_) {
    auto *row = new QHBoxLayout;
    row->addWidget(ui::text(busy_->empty() ? t("packs_checking").toStdString() : *busy_));
    auto *p = new QProgressBar;
    p->setRange(0, busy_->empty() ? 0 : 1000);
    p->setValue(static_cast<int>(progress_ * 1000));
    p->setTextVisible(false);
    row->addWidget(p, 1);
    if (!busy_->empty()) {
      auto *c = new QPushButton(t("cancel"));
      connect(c, &QPushButton::clicked, this, [this] { dl_.cancel(); });
      row->addWidget(c);
    }
    body_->addLayout(row);
  }
  for (auto *e : {&error_, &list_error_})
    if (!e->empty()) body_->addWidget(ui::text("⚠ " + *e, -1));
  auto *top = new QHBoxLayout;
  auto *col = new QVBoxLayout;
  col->addWidget(ui::text(view_.summary));
  auto *open = ui::link(t("show_finder"), [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(q(c_.data().empty() ? app::data_dir() : c_.data()))); });
  col->addWidget(open, 0, Qt::AlignLeft);
  top->addLayout(col, 1);
  auto *check = new QPushButton(t("check_t"));
  check->setEnabled(!busy_);
  connect(check, &QPushButton::clicked, this, &PacksPane::refresh);
  top->addWidget(check, 0, Qt::AlignTop);
  body_->addWidget(fluent::card(top));
  auto group = [this](const std::string &title, const std::vector<PackLine> &lines) {
    if (lines.empty()) return;
    body_->addSpacing(6);
    body_->addWidget(ui::text(title, 0, false, true));
    for (auto &l : lines) body_->addWidget(fluent::card(line(l)));
  };
  group(view_.installed_title, view_.installed);
  group(view_.available_title, view_.available);
  if (!view_.empty.empty()) body_->addWidget(ui::text(view_.empty, 0, true));
  body_->addStretch();
  ui::keep_scroll(area, scrolled);
}

QWidget *PacksPane::line(const PackLine &l) {
  auto *w = new QWidget;
  auto *row = new QHBoxLayout(w);
  row->setContentsMargins(0, 0, 0, 0);
  auto *col = new QVBoxLayout;
  col->setSpacing(1);
  col->addWidget(ui::text(l.title));
  col->addWidget(ui::text(l.detail, -1.5, true));
  row->addLayout(col, 1);
  if (!l.button.label.empty()) {
    auto *b = new QPushButton(q(l.button.label));
    b->setEnabled(!busy_);
    auto a = l.button;
    connect(b, &QPushButton::clicked, this, [this, a] { run(a); });
    row->addWidget(b);
  }
  if (!l.menu.empty()) {
    auto *b = fluent::icon_button(fluent::Glyph::more, {});
    b->setPopupMode(QToolButton::InstantPopup);
    b->setEnabled(!busy_);
    auto *menu = new QMenu(b);
    for (auto &a : l.menu) {
      if (a.divider) menu->addSeparator();
      auto *x = menu->addAction(q(a.label));
      x->setEnabled(a.enabled);
      connect(x, &QAction::triggered, this, [this, a] { run(a); });
    }
    b->setMenu(menu);
    row->addWidget(b);
  }
  return w;
}

void PacksPane::refresh() {
  busy_ = "";
  draw();
  auto dir = c_.data();
  c_.run<std::string>(c_.ai, [dir] { return app::refresh(dir); }, [this](std::string e) {
    busy_.reset();
    list_error_ = e;
    view_ = app::packs(c_.data(), c_.device);
    draw();
  }, this);
}

void PacksPane::run(const PackAction &a) {
  if (busy_ || a.ids.empty()) return;
  busy_ = a.what;
  progress_ = 0;
  error_.clear();
  auto dir = c_.data();
  auto ids = a.ids;
  if (a.remove) {
    c_.run<Status>(c_.ai, [dir, ids] { return app::remove(dir, ids); }, [this](Status s) { finished(s); }, this);
    return draw();
  }
  dl_ = app::Download();
  auto dl = dl_;
  poll_->start(300);
  c_.run<Status>(c_.ai, [dir, ids, dl] { return app::install(dir, ids, dl); }, [this](Status s) { finished(s); }, this);
  draw();
}

void PacksPane::finished(const Status &s) {
  poll_->stop();
  busy_.reset();
  error_ = s.cancelled ? "" : s.error;
  view_ = app::packs(c_.data(), c_.device);
  c_.reload();
  draw();
  emit changed();
}

}  // namespace lexipeek::qt
