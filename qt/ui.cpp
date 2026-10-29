#include "ui.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QGuiApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
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

// Secondary text: the palette's role, so it follows a theme change without a redraw.
void grey(QWidget *w) { w->setForegroundRole(QPalette::PlaceholderText); }

QLabel *link(const QString &s, std::function<void()> click) {
  auto *l = new QLabel("<a href=\"#\">" + s.toHtmlEscaped() + "</a>");  // the palette's Link colour
  l->setTextFormat(Qt::RichText);
  QObject::connect(l, &QLabel::linkActivated, [click = std::move(click)] { click(); });
  return l;
}

bool ask(QWidget *parent, const QString &title, const QString &detail, const QString &yes, const QString &no) {
  QMessageBox box(QMessageBox::Question, "LexiPeek", title, QMessageBox::NoButton, parent);
  if (!detail.isEmpty()) box.setInformativeText(detail);
  auto *y = box.addButton(yes, QMessageBox::AcceptRole);
  box.addButton(no, QMessageBox::RejectRole);
  box.exec();
  return box.clickedButton() == static_cast<QAbstractButton *>(y);
}

void clear(QLayout *l) {
  while (auto *item = l->takeAt(0)) {
    if (auto *inner = item->layout()) clear(inner);
    // Later: the widget may be the one whose signal is redrawing.
    if (auto *w = item->widget()) w->hide(), w->deleteLater();
    delete item;
  }
}

QScrollArea *scroll(QWidget *inner) {
  auto *s = new QScrollArea;
  s->setWidget(inner);
  s->setWidgetResizable(true);
  s->setFrameShape(QFrame::NoFrame);
  s->viewport()->setAutoFillBackground(false);
  inner->setAutoFillBackground(false);  // setWidget turned it on
  return s;
}

void keep_scroll(QScrollArea *area, int value) {
  QPointer<QScrollArea> a(area);
  QTimer::singleShot(0, area, [a, value] {
    if (a) a->verticalScrollBar()->setValue(value);
  });
}

}  // namespace lexipeek::qt::ui
