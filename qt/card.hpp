// The card: CardModel carries out the core's CardNav steps; CardWidget draws the core's CardView (as mac/CardView).
#pragma once
#include <QWidget>
#include <functional>
#include "core_client.hpp"

class QVBoxLayout;

namespace lexipeek::qt {

class CardModel : public QObject {
  Q_OBJECT
 public:
  explicit CardModel(CoreClient &client);

  CardNav nav;
  bool has_card() const { return nav.has_card(); }
  CardView look() const { return look_; }
  bool looking() const { return request_ != 0 && !nav.has_card(); }
  bool busy() const { return nav.busy(); }
  // Source::cli writes nothing (snapshots).
  Source source = Source::input;
  // The AI key for a request ("" none): the platform's credential store.
  std::function<std::string()> key = [] { return std::string(); };

  void reset();
  void lookup(const std::string &text);
  void open(const std::string &word);
  void relookup(const std::string &lang, const std::string &answer);
  void back();
  void open_section(int entry, const std::string &section);
  void turn(int by);
  void open_entry(int entry);
  void select(CardTab tab);
  void next_tab(int by);
  void toggle_entry();
  void choose(Method m);
  void choose_ai();
  void toggle_saved();

 signals:
  void changed();

 private:
  void run(const CardStep &step);
  void translate(Method m);
  void ask_ai();
  void relook();
  CoreClient &c_;
  CardLane lane_;
  int request_ = 0;
  CardView look_;
};

class CardWidget : public QWidget {
  Q_OBJECT
 public:
  CardWidget(CardModel &model, bool embedded = false, QWidget *parent = nullptr);
  void rebuild();

 signals:
  void openPane(int section, int tab);  // a failed AI answer's settings

 private:
  QWidget *row(const CardRow &r, const CardView &v);
  QWidget *header(const CardView &v);
  QWidget *tabs(const CardView &v);
  QWidget *bar(const CardView &v);
  QFont font(int delta = 0, bool bold = false) const;
  CardModel &m_;
  bool embedded_;
  QVBoxLayout *box_;
  CardView v_;
};

}  // namespace lexipeek::qt
