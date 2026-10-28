#include "fluent.hpp"
#include "platform.hpp"
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QIconEngine>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QVBoxLayout>

namespace lexipeek::qt::fluent {

namespace {

// Windows 11's colours (its design tokens), light and dark.
struct Theme {
  QColor window, layer, field, stroke, control, bottom, text, secondary, accent, on_accent, hover, selected;
};
const Theme light{"#f3f3f3", "#fbfbfb", "#ffffff", "#e5e5e5", "#d1d1d1", "#8a8a8a", "#1b1b1b",
                  "#5d5d5d", "#005fb8", "#ffffff", "#ececec", "#e6e6e6"};
const Theme night{"#202020", "#2b2b2b", "#2d2d2d", "#3a3a3a", "#3b3b3b", "#9a9a9a", "#ffffff",
                  "#c5c5c5", "#60cdff", "#000000", "#333333", "#2d2d2d"};

const Theme &theme() { return dark() ? night : light; }

QString icon_family() {
  static const QString f = [] {
    auto all = QFontDatabase::families();
    for (auto name : {"Segoe Fluent Icons", "Segoe MDL2 Assets"})
      if (all.contains(name)) return QString(name);
    return QString();
  }();
  return f;
}

// Without Windows' icon fonts (the snapshots elsewhere): text characters of the same meaning.
QString fallback(Glyph g) {
  switch (g) {
    case Glyph::back: return "←";
    case Glyph::chevron: return "▾";
    case Glyph::copy: return "⧉";
    case Glyph::star: return "☆";
    case Glyph::star_filled: return "★";
    case Glyph::pin: case Glyph::pinned: return "⊙";
    case Glyph::search: return "⌕";
    case Glyph::history: return "◷";
    case Glyph::comment: return "❝";
    case Glyph::help: return "?";
    case Glyph::settings: return "⚙";
    case Glyph::info: return "ⓘ";
    case Glyph::globe: return "◍";
    case Glyph::card: return "▭";
    case Glyph::robot: return "✦";
    case Glyph::keyboard: return "⌨";
    case Glyph::close: case Glyph::remove: return "✕";
    case Glyph::up: return "▴";
    case Glyph::left: return "‹";
    case Glyph::right: return "›";
    case Glyph::more: return "⋯";
  }
  return "";
}

// The combo box's arrow: the system's chevron glyph as a picture (a style sheet takes no font glyph).
QString chevron_file(const Theme &t) {
  auto path = QDir(QDir::tempPath()).filePath(QString("lexipeek-chevron-%1.png").arg(t.text.name().mid(1)));
  QPixmap pm(24, 24);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setFont(icons(16));
  p.setPen(t.text);
  p.drawText(pm.rect(), Qt::AlignCenter, glyph(Glyph::chevron));
  p.end();
  pm.save(path);
  return QDir::fromNativeSeparators(path);
}

QString style_sheet(const Theme &t) {
  QString s = R"(
QToolTip { color: TEXT; background: LAYER; border: 1px solid STROKE; padding: 4px 8px; border-radius: 4px; }
QPushButton { background: LAYER; border: 1px solid CONTROL; border-radius: 4px; padding: 5px 14px; min-height: 20px; }
QPushButton:hover { background: HOVER; }
QPushButton:pressed { background: SELECTED; }
QPushButton:disabled { color: SECONDARY; }
QPushButton:default { background: ACCENT; color: ONACCENT; border: 1px solid ACCENT; }
QLineEdit, QKeySequenceEdit QLineEdit, QPlainTextEdit, QComboBox {
  background: FIELD; border: 1px solid CONTROL; border-bottom: 1px solid BOTTOM; border-radius: 4px; padding: 4px 8px;
  selection-background-color: ACCENT; selection-color: ONACCENT; }
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border-bottom: 2px solid ACCENT; }
QComboBox::drop-down { border: none; width: 28px; }
QComboBox::down-arrow { image: url(CHEVRON); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: LAYER; border: 1px solid STROKE; selection-background-color: SELECTED; selection-color: TEXT; outline: 0; }
QMenu { background: LAYER; border: 1px solid STROKE; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 6px 28px 6px 12px; border-radius: 4px; }
QMenu::item:selected { background: HOVER; }
QMenu::item:disabled { color: SECONDARY; }
QMenu::separator { height: 1px; background: STROKE; margin: 4px 8px; }
QFrame#flyout { background: WINDOW; border: 1px solid STROKE; border-radius: 8px; }
QFrame#flyout QScrollArea, QFrame#flyout QScrollArea > QWidget > QWidget { background: transparent; }
QWidget#bar { background: LAYER; border-top: 1px solid STROKE; border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; }
QToolButton[command="true"] { border: none; border-radius: 4px; min-width: 30px; min-height: 28px; padding: 0 4px; }
QToolButton[command="true"]:hover { background: HOVER; }
QToolButton[command="true"]:pressed { background: SELECTED; }
QToolButton[command="true"]::menu-indicator { image: none; width: 0; }
QFrame#card { background: LAYER; border: 1px solid STROKE; border-radius: 6px; }
QListWidget#nav { background: transparent; border: none; outline: 0; }
QListWidget#nav::item { padding: 8px 10px; margin: 1px 0; border-radius: 4px; border-left: 3px solid transparent; }
QListWidget#nav::item:hover { background: HOVER; }
QListWidget#nav::item:selected { background: SELECTED; color: TEXT; border-left: 3px solid ACCENT; }
QTreeWidget { background: transparent; border: none; alternate-background-color: LAYER; }
QTreeWidget::item { padding: 4px; }
QTreeWidget::item:selected { background: SELECTED; color: TEXT; }
QProgressBar { background: STROKE; border: none; border-radius: 2px; max-height: 4px; }
QProgressBar::chunk { background: ACCENT; border-radius: 2px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: BOTTOM; border-radius: 3px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QCheckBox::indicator, QRadioButton::indicator { width: 18px; height: 18px; }
)";
  s.replace("TEXT", t.text.name()).replace("SECONDARY", t.secondary.name()).replace("LAYER", t.layer.name());
  s.replace("FIELD", t.field.name()).replace("STROKE", t.stroke.name()).replace("CONTROL", t.control.name());
  s.replace("BOTTOM", t.bottom.name()).replace("ONACCENT", t.on_accent.name()).replace("ACCENT", t.accent.name());
  s.replace("HOVER", t.hover.name()).replace("SELECTED", t.selected.name()).replace("WINDOW", t.window.name());
  s.replace("CHEVRON", chevron_file(t));
  return s;
}

std::optional<bool> forced;

}  // namespace

bool dark() { return forced ? *forced : QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark; }
QColor accent() { return theme().accent; }
QColor secondary() { return theme().secondary; }

void apply() {
  static bool connected = false;
  if (!connected) QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, [] { apply(); }), connected = true;
  if (QApplication::style()->name() != "fusion") QApplication::setStyle(QStyleFactory::create("fusion"));
  auto &t = theme();
  QPalette p;
  p.setColor(QPalette::Window, t.window);
  p.setColor(QPalette::WindowText, t.text);
  p.setColor(QPalette::Base, t.field);
  p.setColor(QPalette::AlternateBase, t.layer);
  p.setColor(QPalette::Text, t.text);
  p.setColor(QPalette::Button, t.layer);
  p.setColor(QPalette::ButtonText, t.text);
  p.setColor(QPalette::PlaceholderText, t.secondary);
  p.setColor(QPalette::Highlight, t.accent);
  p.setColor(QPalette::HighlightedText, t.on_accent);
  p.setColor(QPalette::Link, t.accent);
  p.setColor(QPalette::Mid, t.stroke);
  p.setColor(QPalette::ToolTipBase, t.layer);
  p.setColor(QPalette::ToolTipText, t.text);
  p.setColor(QPalette::Disabled, QPalette::Text, t.secondary);
  p.setColor(QPalette::Disabled, QPalette::WindowText, t.secondary);
  p.setColor(QPalette::Disabled, QPalette::ButtonText, t.secondary);
  QApplication::setPalette(p);
  qApp->setStyleSheet(style_sheet(t));
}

void force(std::optional<bool> on) {
  forced = on;
  apply();
}

QString icon_font() { return icon_family(); }

QFont icons(int px) {
  QFont f(icon_family().isEmpty() ? QApplication::font().family() : icon_family());
  f.setPixelSize(px);
  return f;
}

QString glyph(Glyph g) { return icon_family().isEmpty() ? fallback(g) : QString(QChar(static_cast<char16_t>(g))); }

namespace {

// A glyph's picture of px device pixels in a colour: drawn by the system where it can (Qt leaves the icon font's code
// points blank), else as text.
QImage glyph_image(Glyph g, int px, QColor color) {
  static QHash<QPair<int, int>, QImage> masks;
  auto key = qMakePair(static_cast<int>(g), px);
  if (!masks.contains(key)) masks[key] = icon_family().isEmpty() ? QImage() : platform::glyph_mask(icon_family(), static_cast<char16_t>(g), px);
  auto &mask = masks[key];
  QImage out(px, px, QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::transparent);
  QPainter p(&out);
  if (!mask.isNull()) {
    out.fill(color);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, mask);
  } else {
    auto f = QApplication::font();
    f.setPixelSize(px);
    p.setFont(f);
    p.setPen(color);
    p.drawText(out.rect(), Qt::AlignCenter, fallback(g));
  }
  return out;
}

// Drawn when shown, in the theme's colours then: a theme change needs no new icons.
class GlyphEngine : public QIconEngine {
 public:
  GlyphEngine(Glyph g, QColor color) : g_(g), color_(color) {}
  void paint(QPainter *p, const QRect &r, QIcon::Mode mode, QIcon::State) override {
    auto c = color_.isValid() ? color_ : theme().text;
    if (mode == QIcon::Disabled) c = theme().secondary, c.setAlphaF(0.5f);
    qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1;
    int side = std::min(r.width(), r.height());
    auto img = glyph_image(g_, static_cast<int>(side * dpr), c);
    p->drawImage(QRectF(r.x() + (r.width() - side) / 2.0, r.y() + (r.height() - side) / 2.0, side, side), img);
  }
  QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override { return scaledPixmap(size, mode, state, 1); }
  QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
    QPixmap pm(size * scale);
    pm.setDevicePixelRatio(scale);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paint(&p, QRect(QPoint(), size), mode, state);
    return pm;
  }
  QIconEngine *clone() const override { return new GlyphEngine(*this); }

 private:
  Glyph g_;
  QColor color_;
};

}  // namespace

QIcon icon(Glyph g, QColor color) { return QIcon(new GlyphEngine(g, color)); }

QFrame *card(QWidget *content) {
  auto *f = new QFrame;
  f->setObjectName("card");
  auto *l = new QVBoxLayout(f);
  l->setContentsMargins(16, 12, 16, 12);
  l->addWidget(content);
  return f;
}

QFrame *card(QLayout *content) {
  auto *w = new QWidget;
  content->setContentsMargins(0, 0, 0, 0);
  w->setLayout(content);
  return card(w);
}

QToolButton *icon_button(Glyph g, const QString &tip) {
  auto *b = new QToolButton;
  b->setIcon(icon(g));
  b->setIconSize({14, 14});
  b->setToolTip(tip);
  b->setProperty("command", true);
  b->setCursor(Qt::PointingHandCursor);
  return b;
}

Toggle::Toggle(QWidget *parent) : QAbstractButton(parent) {
  setCheckable(true);
  setCursor(Qt::PointingHandCursor);
  setFixedSize(sizeHint());
}

void Toggle::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  auto &t = theme();
  QRectF track(0.5, 0.5, width() - 1, height() - 1);
  p.setOpacity(isEnabled() ? 1 : 0.4);
  if (isChecked()) {
    p.setPen(Qt::NoPen);
    p.setBrush(t.accent);
  } else {
    p.setPen(QPen(t.secondary, 1));
    p.setBrush(Qt::NoBrush);
  }
  p.drawRoundedRect(track, height() / 2.0, height() / 2.0);
  qreal d = underMouse() ? 14 : 12;
  qreal x = isChecked() ? width() - d - (height() - d) / 2 : (height() - d) / 2;
  p.setPen(Qt::NoPen);
  p.setBrush(isChecked() ? t.on_accent : t.secondary);
  p.drawEllipse(QRectF(x, (height() - d) / 2, d, d));
}

Tab::Tab(const QString &text, bool open, QWidget *parent) : QLabel(text, parent), open_(open) {
  setTextFormat(Qt::PlainText);
  setCursor(Qt::PointingHandCursor);
  setContentsMargins(0, 2, 0, 7);
  auto p = palette();
  p.setColor(QPalette::WindowText, open ? theme().text : theme().secondary);
  setPalette(p);
}

void Tab::mouseReleaseEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) emit clicked();
}

void Tab::paintEvent(QPaintEvent *e) {
  QLabel::paintEvent(e);
  if (!open_) return;
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(theme().accent);
  qreal w = std::min<qreal>(16, width());
  p.drawRoundedRect(QRectF((width() - w) / 2, height() - 3, w, 3), 1.5, 1.5);
}

TabBar::TabBar(const QStringList &tabs, int current, QWidget *parent) : QWidget(parent) {
  auto *l = new QHBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(20);
  for (int i = 0; i < tabs.size(); ++i) {
    auto *b = new Tab(tabs[i], i == current);
    connect(b, &Tab::clicked, this, [this, i] { emit picked(i); });
    l->addWidget(b);
  }
  l->addStretch();
}

}  // namespace lexipeek::qt::fluent
