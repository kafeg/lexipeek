// The Windows 11 look (Fluent) for every window: palettes, one style sheet, the system's icon glyphs, and the few
// controls Qt has no Fluent form of (a toggle switch, a settings card, a text tab bar).
#pragma once
#include <QAbstractButton>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QToolButton>
#include <QWidget>
#include <optional>

namespace lexipeek::qt::fluent {

// Segoe Fluent Icons (Windows 11) / Segoe MDL2 Assets (Windows 10) code points.
enum class Glyph : char16_t {
  back = 0xE72B, chevron = 0xE70D, copy = 0xE8C8, star = 0xE734, star_filled = 0xE735, pin = 0xE718, pinned = 0xE840,
  search = 0xE721, history = 0xE81C, comment = 0xE90A, help = 0xE897, settings = 0xE713, info = 0xE946, globe = 0xE774,
  card = 0xE8A1, robot = 0xE99A, keyboard = 0xE765, close = 0xE711, up = 0xE70E, remove = 0xE74D, more = 0xE712
};

// The theme now (Fusion, the palette, the style sheet), again whenever the system's scheme changes.
void apply();
// Light or dark whatever the system says (snapshots); nullopt: the system's again.
void force(std::optional<bool> dark);
bool dark();
QColor accent();
QColor secondary();  // secondary text
// The glyph's font at a pixel size (falls back to the text font where Windows' icon fonts are missing).
QFont icons(int px);
QString icon_font();  // the family in use, "" when the system has neither
QString glyph(Glyph g);
// The glyph as an icon in a colour (the theme's text when none, at each paint).
QIcon icon(Glyph g, QColor color = {});

// A rounded panel around a row (Windows Settings' "settings card").
QFrame *card(QWidget *content);
QFrame *card(QLayout *content);
// A small icon button (a row's ▲ ▼ ✕ ⋯): the glyph, its tooltip.
QToolButton *icon_button(Glyph g, const QString &tip);

// An on/off switch.
class Toggle : public QAbstractButton {
  Q_OBJECT
 public:
  explicit Toggle(QWidget *parent = nullptr);
  QSize sizeHint() const override { return {40, 20}; }

 protected:
  void paintEvent(QPaintEvent *) override;
};

// A text tab: the open one in the text's colour with the accent's short line under it, the others secondary.
class Tab : public QLabel {
  Q_OBJECT
 public:
  Tab(const QString &text, bool open, QWidget *parent = nullptr);

 signals:
  void clicked();

 protected:
  void mouseReleaseEvent(QMouseEvent *e) override;
  void paintEvent(QPaintEvent *e) override;

 private:
  bool open_;
};

// Text tabs in a row (Windows Settings' SelectorBar).
class TabBar : public QWidget {
  Q_OBJECT
 public:
  TabBar(const QStringList &tabs, int current, QWidget *parent = nullptr);

 signals:
  void picked(int index);
};

}  // namespace lexipeek::qt::fluent
