#include "selection_icon.hpp"
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QStyleHints>
#include <QtConcurrent/QtConcurrentRun>
#include "core_client.hpp"

namespace lexipeek::qt {

SelectionIcon::SelectionIcon() : QToolButton(nullptr) {
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setIcon(QIcon(":/icons/icon_32.png"));
  setIconSize(QSize(20, 20));
  setFixedSize(28, 28);
  setAutoRaise(true);
  setToolTip(t("lookup_service"));
  hide_.setSingleShot(true);
  connect(&hide_, &QTimer::timeout, this, &SelectionIcon::close_icon);
  connect(this, &QToolButton::clicked, this, [this] {
    close_icon();
    emit open(found_);
  });
  connect(&reading_, &QFutureWatcher<std::optional<platform::Selected>>::finished, this, [this] {
    if (auto s = reading_.result()) {
      s->bounds = platform::logical(s->bounds);
      show_at(*s, up_at_);
    }
  });
}

void SelectionIcon::set_enabled(bool on, std::vector<std::string> off_apps) {
  off_apps_ = std::move(off_apps);
  close_icon();
  platform::watch_input(on ? std::function<void(platform::Input)>([this](platform::Input i) { input(i); }) : nullptr);
}

void SelectionIcon::input(platform::Input i) {
  auto at = QCursor::pos();
  if (i == platform::Input::down) {
    down_at_ = at;
    if (!geometry().contains(at)) close_icon();
    return;
  }
  if (i == platform::Input::other) return close_icon();
  // Up: a drag or a double click in another app is a new selection.
  auto now = QDateTime::currentMSecsSinceEpoch();
  bool twice = now - last_up_ < QGuiApplication::styleHints()->mouseDoubleClickInterval();
  last_up_ = now;
  bool dragged = (at - down_at_).manhattanLength() > 6;
  // A drag on a window of ours (moving the card) is not a selection in another app.
  auto ours = [](QPoint p) {
    for (auto *w : QApplication::topLevelWidgets())
      if (w->isVisible() && w->frameGeometry().contains(p)) return true;
    return false;
  };
  if (!(dragged || twice) || platform::front_is_mine() || ours(at) || ours(down_at_)) return;
  auto app = platform::front_app();
  if (std::find(off_apps_.begin(), off_apps_.end(), app) != off_apps_.end()) return;
  up_at_ = at;
  // The app updates its selection after the mouse-up.
  QTimer::singleShot(60, this, [this] {
    if (!reading_.isRunning()) reading_.setFuture(QtConcurrent::run(platform::selection));
  });
}

void SelectionIcon::show_at(const platform::Selected &s, QPoint at) {
  found_ = s;
  auto *screen = QGuiApplication::screenAt(at);
  auto g = (screen ? screen : QGuiApplication::primaryScreen())->availableGeometry();
  move(std::min(at.x() + 8, g.right() - width()), std::max(at.y() - 10 - height(), g.top()));
  show();
  platform::round(this);
  hide_.start(4000);
}

void SelectionIcon::close_icon() {
  hide_.stop();
  hide();
}

}  // namespace lexipeek::qt
