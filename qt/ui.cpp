#include "ui.hpp"
#include <QApplication>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

namespace lexipeek::qt::ui {

QLabel *text(const QString &s, double delta, bool grey_, bool bold) {
  auto *l = new QLabel(s);
  l->setWordWrap(true);
  auto f = l->font();
  f.setPointSizeF(f.pointSizeF() + delta);
  f.setBold(bold);
  l->setFont(f);
  if (grey_) grey(l);
  return l;
}

// Secondary: the text's own colour, fainter (the same in light and dark, not the style's placeholder tint).
void grey(QWidget *w) {
  auto p = w->palette();
  auto c = QApplication::palette().color(QPalette::WindowText);  // the theme now (a widget's may lag a theme change)
  c.setAlphaF(0.6f);
  p.setColor(QPalette::WindowText, c);
  w->setPalette(p);
}

QLabel *link(const QString &s, std::function<void()> click) {
  auto colour = QApplication::palette().color(QPalette::Link).name();
  auto *l = new QLabel("<a href=\"#\" style=\"color: " + colour + "\">" + s.toHtmlEscaped() + "</a>");
  l->setTextFormat(Qt::RichText);
  QObject::connect(l, &QLabel::linkActivated, [click = std::move(click)] { click(); });
  return l;
}

void clear(QLayout *l) {
  while (auto *item = l->takeAt(0)) {
    if (auto *inner = item->layout()) clear(inner);
    delete item->widget();
    delete item;
  }
}

void keep_scroll(QScrollArea *area, int value) {
  QPointer<QScrollArea> a(area);
  QTimer::singleShot(0, area, [a, value] {
    if (a) a->verticalScrollBar()->setValue(value);
  });
}

}  // namespace lexipeek::qt::ui
