#include "popup.hpp"
#include <QCursor>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScreen>
#include <QVBoxLayout>

namespace lexipeek::qt {

namespace {
constexpr int escape_key = 100, pin_key = 101, gap = 6, shadow = 16;

// Under the anchor (the selection or the cursor), above it when there is no room, inside the screen.
QPoint place(QSize size, QRect anchor, QRect screen) {
  int x = anchor.left(), y = anchor.bottom() + gap;
  if (y + size.height() > screen.bottom()) y = anchor.top() - gap - size.height();
  x = std::min(std::max(x, screen.left()), screen.right() - size.width());
  y = std::min(std::max(y, screen.top()), screen.bottom() - size.height());
  return {x, y};
}
}  // namespace

// A flyout as Windows 11's: the card in a frame with rounded corners and a hairline, a soft shadow around it (drawn
// here, the same on Windows 10).
CardPopup::CardPopup(CoreClient &client) : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), model(client) {
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_TranslucentBackground);
  auto *box = new QVBoxLayout(this);
  box->setContentsMargins(shadow, shadow - 4, shadow, shadow + 4);
  auto *frame = new QFrame;
  frame->setObjectName("flyout");
  auto *fl = new QVBoxLayout(frame);
  fl->setContentsMargins(1, 1, 1, 1);
  card = new CardWidget(model);
  fl->addWidget(card);
  box->addWidget(frame);
  auto *drop = new QGraphicsDropShadowEffect(frame);
  drop->setBlurRadius(28);
  drop->setOffset(0, 6);
  drop->setColor(QColor(0, 0, 0, 70));
  frame->setGraphicsEffect(drop);
  connect(&model, &CardModel::changed, this, [this] { QMetaObject::invokeMethod(this, [this] { fit(); }, Qt::QueuedConnection); });
  connect(card, &CardWidget::submitted, this, [this](const QString &t) {
    if (t.trimmed().isEmpty()) return;
    model.input = false;
    model.source = Source::input;
    model.lookup(t.toStdString());
  });
  // A click anywhere outside the card (not its shadow) closes it; the focused app keeps its click. Short: a tap is quick.
  outside_.setInterval(15);
  connect(&outside_, &QTimer::timeout, this, [this] {
    bool down = platform::mouse_down();
    auto card_rect = frameGeometry().adjusted(shadow, shadow, -shadow, -shadow);
    bool outside = down && !was_down_ && !model.pinned && !card_rect.contains(QCursor::pos());
    was_down_ = down;
    if (outside) return close_card();  // not keys() after it: Esc would stay taken
    keys(!model.pinned);
  });
}

void CardPopup::show_card(const std::optional<std::string> &text, Source source, const QRect &bounds, bool focus) {
  close_card();
  shown = text.value_or("");
  model.reset();
  card->clear_input();
  model.input = !text;
  auto anchor = bounds.isEmpty() ? QRect(QCursor::pos(), QSize(0, 0)) : bounds;
  auto *s = QGuiApplication::screenAt(anchor.center());
  screen_ = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
  card->rebuild();
  adjustSize();
  // The frame at the anchor; the window around it is larger by the shadow.
  auto inner = size() - QSize(2 * shadow, 2 * shadow);
  move(place(inner, anchor, screen_) - QPoint(shadow, shadow - 4));
  model.source = source;
  if (text) model.lookup(*text);
  show();
  if (focus) {
    platform::activate(this);
    // Once more after the card is drawn (its text field is made then): the focus in the field.
    QTimer::singleShot(50, this, [this] {
      platform::activate(this);
      if (auto *e = findChild<QLineEdit *>()) e->setFocus(Qt::ActiveWindowFocusReason);
    });
  }
  keys(true);
  was_down_ = platform::mouse_down();
  outside_.start();
}

void CardPopup::close_card() {
  if (!isVisible()) return;
  hide();
  outside_.stop();
  keys(false);
  model.pinned = false;
  model.cancel();
}

void CardPopup::keys(bool on) {
  if (on == keys_) return;
  keys_ = on;
  if (on) {
    platform::register_hotkey(escape_key, Hotkey{"Esc", false, false, false, false}, [this] { close_card(); });
    platform::register_hotkey(pin_key, Hotkey{"P", false, false, true, false}, [this] { model.toggle_pin(); });
  } else {
    platform::unregister_hotkey(escape_key);
    platform::unregister_hotkey(pin_key);
  }
}

// Its height follows the card; the top edge stays, inside the screen.
void CardPopup::fit() {
  if (!isVisible()) return;
  // While something loads it keeps its height (no jumping), then fits what it shows.
  int was = height();
  adjustSize();
  if (model.busy() && height() < was) resize(width(), was);
  auto g = geometry();
  if (g.bottom() > screen_.bottom()) g.moveBottom(screen_.bottom());
  if (g.right() > screen_.right()) g.moveRight(screen_.right());
  move(g.topLeft());
}

// With the focus (typing): ←/→ tabs, Ctrl+S ☆, Backspace back.
void CardPopup::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape) return close_card();
  if (!model.has_card()) return QWidget::keyPressEvent(e);
  if (e->key() == Qt::Key_S && e->modifiers() == Qt::ControlModifier) return model.toggle_saved();
  if (e->key() == Qt::Key_P && e->modifiers() == Qt::ControlModifier) return model.toggle_pin();
  switch (e->key()) {
    case Qt::Key_Left: return model.next_tab(-1);
    case Qt::Key_Right: return model.next_tab(1);
    case Qt::Key_Backspace: return model.back();
    default: QWidget::keyPressEvent(e);
  }
}

// A drag on the card's background moves it.
void CardPopup::mousePressEvent(QMouseEvent *e) {
  drag_ = e->globalPosition().toPoint() - pos();
  start_ = pos();
}

void CardPopup::mouseMoveEvent(QMouseEvent *e) {
  if (e->buttons() & Qt::LeftButton) move(e->globalPosition().toPoint() - drag_);
}

void CardPopup::mouseReleaseEvent(QMouseEvent *) {
  if ((pos() - start_).manhattanLength() > 4 && pin_moved && !model.pinned) model.toggle_pin();
}

}  // namespace lexipeek::qt
