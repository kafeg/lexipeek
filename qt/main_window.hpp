// The LexiPeek window (mac MainWindow): Look up (its own card) · Words · Phrases · History (Notes) · Help.
#pragma once
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
  enum Page { lookup, words, phrases, history, help };
  explicit MainWindow(CoreClient &client);
  CardModel card;
  void show_page(int page);
  void look(const QString &text);
  void refresh();  // Notes and the memory line again
  std::string lookup_key, input_key;  // the hotkeys' labels for the hints
  Source source = Source::input;      // Source::cli writes nothing (snapshots)

 signals:
  void openSettings(int section, int tab);  // section -1: the one shown last
  void openAbout();

 private:
  QWidget *lookup_page();
  QWidget *notes_page();
  void draw_notes();
  void draw_help();
  CoreClient &c_;
  QListWidget *side_ = nullptr;
  QStackedWidget *pages_ = nullptr;
  QLineEdit *query_ = nullptr;
  QLabel *hint_ = nullptr, *memory_ = nullptr, *said_ = nullptr;
  QWidget *notes_ = nullptr, *help_ = nullptr;
  NotesTab tab_ = NotesTab::words;
  int page_ = 0;
};

}  // namespace lexipeek::qt
