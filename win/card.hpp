// The card: CardModel carries out the core's CardNav steps (polls its CardSession); CardWidget draws the core's CardView
// with WinUI's controls (qt/card's, mac CardView's). The pop-up (popup.hpp) and the LexiPeek window both use them.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core_client.hpp"
#include "lexipeek/session.hpp"
#include "ui.hpp"

namespace lexipeek::win {

// Notes changed (a look-up, ☆): every card and Notes hear it.
class NotesBus {
 public:
  int listen(std::function<void()> f);
  void forget(int id);
  void changed();

 private:
  std::map<int, std::function<void()>> listeners_;
  int next_ = 0;
};
NotesBus &notes_bus();

class CardModel {
 public:
  explicit CardModel(CoreClient &client);
  ~CardModel();
  CardModel(const CardModel &) = delete;

  CardNav nav;  // the session's, as of the last poll (read only)
  bool has_card() const { return nav.has_card(); }
  const CardView &look() const { return look_; }
  bool looking() const { return session_.looking(); }
  std::vector<Seen> recents() const { return c_.engine.recents(app::recents_shown()); }  // under the input field
  bool busy() const { return session_.busy(); }  // a translation, an AI answer, a look-up or a section on its way
  bool pinned = false;  // the pop-up card stays open on an outside click
  std::string unread;   // the app whose selection could not be read ("" = none)
  bool input = false;   // the card shows its text field (typing)
  Source source = Source::input;
  // The AI key for a request ("" none): the Credential Manager's.
  std::function<std::string()> key = [] { return std::string(); };
  // Drawn again: each listener (a widget, the window) until it forgets (the model may outlive them).
  int listen(std::function<void()> f);
  void forget(int id);

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
  void full_entries();    // as Card → Article's full entries (snapshots)
  void choose(Method m);  // picked in the card: also Settings → Translation's
  void use(Method m);     // only this card (snapshots)
  void choose_ai();
  void toggle_saved();
  void toggle_pin();
  void cancel();  // the card closed: its translation stops

 private:
  void acted();  // after an action: draw, and poll while something is on its way
  void relook();
  CoreClient &c_;
  app::CardSession session_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer poll_{nullptr};
  CardView look_;
  int notes_ = -1, next_ = 0;
  std::map<int, std::function<void()>> changed_;
};

class CardWidget {
 public:
  // embedded: in the LexiPeek window (its width, the bar after the text, no pin).
  CardWidget(CardModel &model, bool embedded = false);
  CardWidget(const CardWidget &) = delete;
  ~CardWidget();
  xaml::FrameworkElement root() const { return root_; }
  void rebuild();
  void show_whole(bool on);  // a cut title whole (a click on it)
  void clear_input() { typed_.clear(); }  // a new card: an empty field
  void focus_field();
  bool scroll_to_end();  // the body scrolled to its end (snapshots); false when it does not scroll
  double screen = 1000;  // the screen's height, DIPs: the body takes CardView::height % of it at most
  bool solid = false;    // its own background (no window backdrop behind it: snapshots, Windows 10)

  std::function<void(int section, int tab)> open_pane;  // a fix button's Settings pane
  std::function<void(const std::string &)> submitted;   // the text field's Return, a recent look-up picked
  std::function<void()> drawn;                          // after a rebuild (the pop-up fits its height)

 private:
  xaml::FrameworkElement header(const CardView &v);
  xaml::FrameworkElement tabs(const CardView &v);
  xaml::FrameworkElement row(const CardRow &r);
  xaml::FrameworkElement bar(const CardView &v);
  xaml::FrameworkElement field();
  double px(int delta = 0) const;  // the card's text size, DIPs
  CardModel &m_;
  bool embedded_;
  xaml::Controls::UserControl root_;
  xaml::Controls::ScrollViewer body_{nullptr};
  xaml::Controls::TextBox field_{nullptr};
  xaml::Controls::ListView recents_{nullptr};
  std::string typed_, place_;
  double limit_ = 450;
  bool whole_ = false, cut_ = false, pending_ = false;
  int listening_ = -1;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // a redraw queued after it is gone does nothing
  CardView v_;
};

}  // namespace lexipeek::win
