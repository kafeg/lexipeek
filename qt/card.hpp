// The card: CardModel carries out the core's CardNav steps; CardWidget draws the core's CardView (as mac/CardView).
#pragma once
#include <QListWidget>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <QTimer>
#include "core_client.hpp"
#include "lexipeek/session.hpp"

class QVBoxLayout;

namespace lexipeek::qt {

class CardModel : public QObject {
  Q_OBJECT
 public:
  explicit CardModel(CoreClient &client);

  CardNav nav;  // the session's, as of the last poll (read only)
  bool has_card() const { return nav.has_card(); }
  CardView look() const { return look_; }
  bool looking() const { return session_.looking(); }
  std::vector<Seen> recents() const { return c_.engine.recents(app::recents_shown()); }  // under the input field
  bool busy() const { return session_.busy(); }  // a translation, an AI answer, a look-up or a section on its way
  bool pinned = false;  // the pop-up card stays open on an outside click
  std::string unread;   // the app whose selection could not be read ("" = none)
  bool input = false;   // the card shows its text field (typing)
  Source source = Source::input;
  // The AI key for a request ("" none): the platform's credential store.
  std::function<std::string()> key = [] { return std::string(); };

  void reset();
  void lookup(const std::string &text, const std::string &lang = "", const std::string &answer = "");
  void open(const std::string &word);
  void relookup(const std::string &lang, const std::string &answer);
  void back();
  void open_section(int entry, const std::string &section);
  void turn(int by);
  void open_entry(int entry);
  void select(CardTab tab);
  void next_tab(int by);
  void full_entries();  // as Card → Article's full entries (snapshots)
  void choose(Method m);  // picked in the card: also Settings → Translation's
  void use(Method m);     // only this card (snapshots)
  void choose_ai();
  void toggle_saved();
  void toggle_pin();

 signals:
  void changed();

 private:
  void acted();  // after an action: draw, and poll while something is on its way
  void relook();
  CoreClient &c_;
  app::CardSession session_;
  QTimer poll_;
  CardView look_;
};

class CardWidget : public QWidget {
  Q_OBJECT
 public:
  CardWidget(CardModel &model, bool embedded = false, QWidget *parent = nullptr);
  void rebuild();
  void show_whole(bool on);  // a cut title whole (a click on it)

 signals:
  void openPane(int section, int tab);  // a failed AI answer's settings
  void submitted(const QString &text);  // the text field's Return

 private:
  QWidget *row(const CardRow &r, const CardView &v);
  QWidget *header(const CardView &v);
  QWidget *tabs(const CardView &v);
  QWidget *bar(const CardView &v);
  QFont font(int delta = 0, bool bold = false) const;
  bool eventFilter(QObject *o, QEvent *e) override;
  void changeEvent(QEvent *e) override;
  QWidget *field();
  CardModel &m_;
  bool embedded_;
  QVBoxLayout *box_;
  QString typed_;
  QPointer<QListWidget> recents_;  // the recent look-ups under an empty card's field

 public:
  void clear_input() { typed_.clear(); }  // a new card: an empty field

 private:
  bool whole_ = false, pending_ = false;
  std::string place_;
  CardView v_;
};

}  // namespace lexipeek::qt
