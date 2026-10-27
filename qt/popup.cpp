#include "popup.hpp"
#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QVBoxLayout>

namespace lexipeek::qt {

namespace {
constexpr int escape_key = 100, pin_key = 101, gap = 6;

// Under the anchor (the selection or the cursor), above it when there is no room, inside the screen.
QPoint place(QSize size, QRect anchor, QRect screen) {
  int x = anchor.left(), y = anchor.bottom() + gap;
  if (y + size.height() > screen.bottom()) y = anchor.top() - gap - size.height();
  x = std::min(std::max(x, screen.left()), screen.right() - size.width());
  y = std::min(std::max(y, screen.top()), screen.bottom() - size.height());
  return {x, y};
}
}  // namespace

CardPopup::CardPopup(CoreClient &client) : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), model(client) {
  setAttribute(Qt::WA_ShowWithoutActivating);
  setObjectName("popup");
  setStyleSheet("#popup { border: 1px solid palette(mid); }");
  auto *box = new QVBoxLayout(this);
  box->setContentsMargins(0, 0, 0, 0);
  card = new CardWidget(model);
  box->addWidget(card);
  connect(&model, &CardModel::changed, this, [this] { QMetaObject::invokeMethod(this, [this] { fit(); }, Qt::QueuedConnection); });
  connect(card, &CardWidget::submitted, this, [this](const QString &t) {
    if (t.trimmed().isEmpty()) return;
    model.input = false;
    model.source = Source::input;
    model.lookup(t.toStdString());
  });
  // A click anywhere outside closes it (the focused app keeps its click).
  outside_.setInterval(50);
  connect(&outside_, &QTimer::timeout, this, [this] {
    bool down = platform::mouse_down();
    if (down && !was_down_ && !model.pinned && !frameGeometry().contains(QCursor::pos())) close_card();
    was_down_ = down;
  });
}

void CardPopup::show_card(const std::optional<std::string> &text, Source source, const QRect &bounds, bool focus) {
  close_card();
  shown = text.value_or("");
  model.reset();
  model.input = !text;
  auto anchor = bounds.isEmpty() ? QRect(QCursor::pos(), QSize(0, 0)) : bounds;
  auto *s = QGuiApplication::screenAt(anchor.center());
  screen_ = (s ? s : QGuiApplication::primaryScreen())->availableGeometry();
  card->rebuild();
  adjustSize();
  move(place(size(), anchor, screen_));
  model.source = source;
  if (text) model.lookup(*text);
  show();
  platform::round(this);
  if (focus) activateWindow(), raise();
  platform::register_hotkey(escape_key, Hotkey{"Esc", false, false, false, false}, [this] { close_card(); });
  platform::register_hotkey(pin_key, Hotkey{"P", false, false, true, false}, [this] { model.toggle_pin(); });
  was_down_ = platform::mouse_down();
  outside_.start();
}

void CardPopup::close_card() {
  if (!isVisible()) return;
  hide();
  outside_.stop();
  platform::unregister_hotkey(escape_key);
  platform::unregister_hotkey(pin_key);
  model.pinned = false;
}

// Its height follows the card; the top edge stays, inside the screen.
void CardPopup::fit() {
  if (!isVisible()) return;
  adjustSize();
  auto g = geometry();
  if (g.bottom() > screen_.bottom()) g.moveBottom(screen_.bottom());
  if (g.right() > screen_.right()) g.moveRight(screen_.right());
  move(g.topLeft());
}

// With the focus (typing): ←/→ tabs, Return the full entry, Ctrl+S ☆, Backspace back.
void CardPopup::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape) return close_card();
  if (!model.has_card()) return QWidget::keyPressEvent(e);
  if (e->key() == Qt::Key_S && e->modifiers() == Qt::ControlModifier) return model.toggle_saved();
  switch (e->key()) {
    case Qt::Key_Left: return model.next_tab(-1);
    case Qt::Key_Right: return model.next_tab(1);
    case Qt::Key_Return:
    case Qt::Key_Enter: return model.toggle_entry();
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
