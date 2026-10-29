// One card's requests for a shell, once for every OS: CardNav's steps carried out on the core's queues (look-ups and
// sections one at a time, translations one at a time for the process, AI answers at once). Polled on the shell's UI
// thread (Swift takes no callbacks): a newer request turns its older ones into no-ops, and a card's ← or new look-up
// stops only its own translation.
#pragma once
#include <memory>
#include <string>
#include "lexipeek/app.hpp"

namespace lexipeek::app {

class CardSession {
 public:
  static CardSession make(const Engine &e);

  // The AI key for this card's answers ("" none): the shell's credential store, read when the card opens.
  void set_key(const std::string &key);
  void reset();
  void look(const std::string &text, Source source, const std::string &lang = "", const std::string &answer = "");
  void open(const std::string &word);
  void relookup(const std::string &lang, const std::string &answer);
  void back();
  void open_section(int entry, const std::string &key);
  void turn(int by);
  void open_entry(int entry);
  void select(CardTab tab);
  void next_tab(int by);
  void full_entries();
  void use(Method m);  // this card only; Settings → Translation's method is the shell's to save
  void choose_ai();
  void toggle_saved();
  void recheck_saved();  // Notes changed elsewhere: the ☆ again
  void cancel();         // the card closed: its translation stops, what is on its way is dropped (the card stays)
  // Read aloud (M9): the card's ▶ (row -1) or a row's; a press on the one being read stops it. One reading for the app.
  void read(int row = -1);
  void stop_reading();
  // The WAV to play now, once ("" none); played(): it ended. The shell plays while reading() and stops when it is not.
  std::string take_play();
  void played();
  bool reading() const;

  // What arrived since the last poll is applied; true when the card changed (draw it again).
  bool poll();
  // A look-up arrived or ☆ changed since the last call: Notes (history, words) changed.
  bool notes_changed();
  bool busy() const;     // something on its way: keep polling
  bool looking() const;  // a look-up asked, no card yet
  bool has_card() const;
  CardNav nav() const;
  CardView view() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace lexipeek::app
