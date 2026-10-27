// The pop-up card (mac CardController): at the selection or the cursor, the focused app keeps the focus; Esc, an
// outside click (unless pinned) or the hotkey again close it.
#pragma once
#include <QPoint>
#include <QTimer>
#include <QWidget>
#include "card.hpp"
#include "platform.hpp"

namespace lexipeek::qt {

class CardPopup : public QWidget {
  Q_OBJECT
 public:
  explicit CardPopup(CoreClient &client);
  CardModel model;
  CardWidget *card;
  // text nil: the text field (focused when typing was asked for); bounds: the selection's, else the cursor.
  void show_card(const std::optional<std::string> &text, Source source, const QRect &bounds, bool focus);
  void close_card();
  bool open() const { return isVisible(); }
  std::string shown;  // the text the open card was asked for
  bool pin_moved = false;  // a dragged card stays open, as pinned (Settings → General)

 protected:
  void keyPressEvent(QKeyEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;

 private:
  void fit();
  QRect screen_;
  QTimer outside_;
  bool was_down_ = false;
  QPoint drag_, start_;
};

}  // namespace lexipeek::qt
