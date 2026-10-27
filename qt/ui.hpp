// Small widgets every window uses.
#pragma once
#include <QLabel>
#include <QLayout>
#include <QToolButton>
#include <functional>
#include <string>

namespace lexipeek::qt::ui {

// A wrapping label: delta points from the app's font, grey the secondary colour.
QLabel *text(const QString &s, int delta = 0, bool grey = false, bool bold = false);
inline QLabel *text(const std::string &s, int delta = 0, bool grey = false, bool bold = false) {
  return text(QString::fromStdString(s), delta, grey, bold);
}
void grey(QWidget *w);
// A borderless button in the link colour.
QToolButton *link(const QString &s, std::function<void()> click);
// Everything in a layout, nested layouts and their widgets too.
void clear(QLayout *l);

}  // namespace lexipeek::qt::ui
