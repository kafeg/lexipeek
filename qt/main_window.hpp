// The LexiPeek window (mac MainWindow): Look up (its own card) · Words · Phrases · History (Notes) · Help's topics.
#pragma once
#include <QVBoxLayout>
#include <QWidget>
#include "card.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;

namespace lexipeek::qt {

class MainWindow : public QWidget {
  Q_OBJECT
 public:
  enum Page { lookup, words, phrases, history, help_head, help };  // help + i: Help's topic i
  explicit MainWindow(CoreClient &client);
  CardModel card;
  void show_page(int page);
  void look(const QString &text);
  void refresh();  // Notes and the memory line again
  std::string lookup_key, input_key;  // the hotkeys' labels for the hints
  Source source = Source::input;

 signals:
  void openSettings(int section, int tab);  // section -1: the one shown last
  void openAbout();
  void openFirstRun();

 protected:
  void changeEvent(QEvent *e) override;
  void hideEvent(QHideEvent *e) override;  // closed: its card's work stops
  void showEvent(QShowEvent *e) override;  // opened: the look-up field takes the keys

 private:
  QWidget *lookup_page();
  QWidget *notes_page();
  void draw_notes();
  void draw_help();
  void icons();
  CoreClient &c_;
  QListWidget *side_ = nullptr, *foot_ = nullptr;
  QStackedWidget *pages_ = nullptr;
  QLineEdit *query_ = nullptr;
  QLabel *hint_ = nullptr, *memory_ = nullptr, *said_ = nullptr;
  QWidget *notes_ = nullptr, *help_ = nullptr;
  QVBoxLayout *banner_ = nullptr;  // ⚠ while no language pack is installed
  NotesTab tab_ = NotesTab::words;
  int page_ = 0, topic_ = 0;
};

}  // namespace lexipeek::qt
