#include "ui.hpp"

namespace lexipeek::qt::ui {

QLabel *text(const QString &s, int delta, bool grey_, bool bold) {
  auto *l = new QLabel(s);
  l->setWordWrap(true);
  auto f = l->font();
  f.setPointSizeF(f.pointSizeF() + delta);
  f.setBold(bold);
  l->setFont(f);
  if (grey_) grey(l);
  return l;
}

void grey(QWidget *w) {
  auto p = w->palette();
  p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
  w->setPalette(p);
}

QToolButton *link(const QString &s, std::function<void()> click) {
  auto *b = new QToolButton;
  b->setText(s);
  b->setAutoRaise(true);
  b->setCursor(Qt::PointingHandCursor);
  b->setStyleSheet("QToolButton { color: palette(link); border: none; padding: 0; }");
  QObject::connect(b, &QToolButton::clicked, std::move(click));
  return b;
}

void clear(QLayout *l) {
  while (auto *item = l->takeAt(0)) {
    if (auto *inner = item->layout()) clear(inner);
    delete item->widget();
    delete item;
  }
}

}  // namespace lexipeek::qt::ui
