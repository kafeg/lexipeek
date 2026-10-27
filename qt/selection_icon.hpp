// The icon at a new selection (Card → Opening, mac SelectionIcon): after a drag or a double click in another app the
// selection is read (UI Automation, never the clipboard) and a small icon appears by the cursor; a click opens the card.
#pragma once
#include <QFutureWatcher>
#include <QTimer>
#include <QToolButton>
#include "platform.hpp"

namespace lexipeek::qt {

class SelectionIcon : public QToolButton {
  Q_OBJECT
 public:
  SelectionIcon();
  void set_enabled(bool on, std::vector<std::string> off_apps);

 signals:
  void open(const platform::Selected &s);

 private:
  void input(platform::Input i);
  void show_at(const platform::Selected &s, QPoint at);
  void close_icon();
  QPoint down_at_, up_at_;
  qint64 last_up_ = 0;
  QTimer hide_;
  QFutureWatcher<std::optional<platform::Selected>> reading_;
  platform::Selected found_;
  std::vector<std::string> off_apps_;
};

}  // namespace lexipeek::qt
