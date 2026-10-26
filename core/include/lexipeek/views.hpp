// What a shell's screens show, worked out in the core: the shells only draw these.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "lexipeek/core.hpp"
#include "lexipeek/packs.hpp"

namespace lexipeek {

// The language's English name ("de" → "German"); the code when unknown.
std::string language_name(const std::string &code);

// One downloadable part of a pair: its dictionary, a Mozilla model, langid, the OPUS-MT or the AI model.
struct PackPart {
  std::string id, kind;
  std::string title;  // "Dictionary", "Mozilla de → en", "Language id", "OPUS-MT", "Hy-MT2 1.8B"
  std::int64_t size = 0;
  bool installed = false, update = false;
  std::string cannot;  // why this machine cannot run it ("" = it can)
};

// A row of Settings → Packs: a language pair and what it needs.
struct PairRow {
  std::string from, to;
  std::string title;  // "German → Russian"
  std::string level;  // "full", "basic", "experimental", ""
  bool installed = false;      // the dictionary and the Mozilla models are there
  std::int64_t missing = 0;    // bytes still to download for that
  std::vector<PackPart> base;  // dictionary, Mozilla models, langid
  std::optional<PackPart> direct, llm;
};

// Installed pairs first, then by level and title.
std::vector<PairRow> pack_rows(const Manifest &m, const std::map<std::string, std::string> &installed, const std::optional<Device> &device = {});
// What a pair needs that is not installed yet (ids).
std::vector<std::string> missing_ids(const Manifest &m, const std::map<std::string, std::string> &installed, const LangPair &pair);
// What removing a pair frees: its parts no other installed pair needs (ids).
std::vector<std::string> freed_ids(const Manifest &m, const std::map<std::string, std::string> &installed, const LangPair &pair);
// The answer languages the packs offer (codes), by their English names.
std::vector<std::string> answer_languages(const Manifest &m);

// The card: what the shell draws from a Card and its state.
enum class CardTab { dictionary, translate };
enum class HeaderStyle { word, phrase, passage };

// The card's state in the shell; plain fields (no optionals) for Swift.
struct CardState {
  CardTab tab = CardTab::translate;
  bool has_method = false;  // a method is chosen (method)
  Method method = Method::fast;
  bool translating = false;
  bool translated = false;  // translation holds the answer
  Translation translation;
  bool full_entry = false;
  SectionPage section;  // the entry's section open on Dictionary (key "": none)
  std::string before;   // the text this card was opened from ("" = none): "←" goes back to it
  bool saved = false;  // ★: the card's text is in Notes
};

// One line of the card's body.
struct CardRow {
  enum class Style {
    note,      // secondary text: "Already in Russian."
    progress,  // a spinner and its text: "Translating…"
    text,      // selectable text: the translation
    error,     // red: a failed translation
    entry,     // a dictionary's entry: label (the dictionary), text (its short line) or html (the full article)
    gloss,     // a word of a phrase, of a section: label bold ("mache … zu: zumachen (…)"), text secondary (its meaning)
    heading,   // bold text: another headword's article, an open section's title
    sense,     // a sense of an article: label its number ("1."), text its meaning
    example,   // an example under a sense, indented: "◆ Ich gehe nach Hause — Я иду домой."
    section,   // a section under an article: label its preview (two lines at most), text its caption ("Related words · 133 ›")
    english    // Mozilla's English in between, smaller, selectable, under the translation
  };
  Style style = Style::note;
  std::string label, text, html;
  std::string link = "";  // a word: a click opens it in this card ("←" comes back)
  std::string open = "";  // a section's key: a click opens it (section rows)
  int entry = 0;     // the entry the section belongs to
};

struct MethodItem {
  Method method = Method::fast;
  std::string title;  // "Direct · OPUS-MT" or "Direct · OPUS-MT — reason"
  bool enabled = false;
};

struct CardView {
  HeaderStyle style = HeaderStyle::passage;
  std::string title;     // the header ("das Haus (Hauses, pl. Häuser)") or the text
  std::string subtitle;  // a word's small line, InkPeek's: "form: Häusern\namong the 1,000 most frequent words · looked up 3 times"
  std::string tooltip;   // the text as asked
  std::string badge;     // "de → ru", "Russian" (already in it), "French" (no pack from it), ""
  bool wide = false;     // a passage: a wider card
  bool tabs = false;     // Dictionary · Translate are shown
  CardTab tab = CardTab::translate;  // the one shown
  std::vector<CardRow> rows;
  std::string entry_toggle;  // "Full entry ⏎", "Short ⏎", "" (none)
  bool menu = false;         // the methods' menu is shown
  std::string menu_title;    // the current method's name or "No method available"
  std::vector<MethodItem> methods;
  std::string back;   // "← Ich mache die Tür zu", "← Entry" (from a section), "" (none)
  std::string pager;  // a section's page: "21–40 of 63", "" (one page)
  bool prev = false, next = false;  // the section has a page before, after
  bool footer = false;              // anything in the footer (else it is not drawn)
  std::string star;       // "☆", "★" (saved), "" (nothing to save)
  std::string star_help;  // "Save to Notes", "Remove from Notes"
};

// The tab a new card opens on: Dictionary for a word with entries, else Translate.
CardTab first_tab(const Card &c);
// A new card's state: its first tab and its method (none when no method is available).
CardState first_state(const Card &c);
CardView card_view(const Card &c, const CardState &s);
// The frequency band of a word's rank (InkPeek's Facts): "among the 1,000 most frequent words" (1,000 / 5,000 / 20,000), "".
std::string frequency(int rank);

// Notes: saved words, saved phrases (a passage counts as one), the history; newest first.
enum class NotesTab { words, phrases, history };

struct NotesRow {
  std::string text;   // what opens the card
  std::string line;   // one short line: the saved meaning, else the newest answer, else "—"
  std::string looks;  // History: "3×"; "" elsewhere
};

struct NotesView {
  std::vector<std::string> tabs;  // "Words (3)", "Phrases (1)", "History (12)"
  NotesTab tab = NotesTab::words;
  std::vector<NotesRow> rows;
  std::string empty;   // shown instead of rows when there are none
  bool clear = false;  // "Clear history" under the list
};

NotesView notes_view(const Store &s, NotesTab tab, const std::string &answer);
// ✕ of a row: unsave, or forget the look.
std::string remove_note(Store &s, NotesTab tab, const std::string &text);

// The first run, a window of its own: Accessibility → the first pair (answer language, book language) → its
// download → the hotkey. The shell keeps this state and draws first_run_view.
enum class FirstStep { access, list, pair, download, done };

struct FirstRunState {
  bool trusted = false;       // Accessibility is granted
  bool access_later = false;  // "Later" on the Accessibility step
  std::string system;         // the system's language ("de")
  std::string answer, book;   // picked ("" = the default)
  bool fetching = false;      // the packs list is being fetched
  std::string list_error;     // fetching it failed
  bool downloading = false;
  std::int64_t done = 0, total = 0;
  std::string error;         // the download failed
  std::string hotkey = "⌥D", input = "⌥A";  // the hotkeys' labels
};

struct Choice {
  std::string code, title;  // "de", "German"
  std::string detail;       // "Full · 84 MB", "Basic · installed"
};

struct FirstRunView {
  FirstStep step = FirstStep::access;
  int number = 1, steps = 3;  // "Step 1 of 3"
  std::string title, text, note;
  std::string button, secondary;  // the main and the other button ("" = none)
  bool button_enabled = true;
  std::string answer_label = "Translate into", book_label = "Texts in";
  std::vector<Choice> answers, books;
  std::string answer, book;  // the picked ones (the defaults filled in)
  bool progress = false;     // a progress bar
  double fraction = -1;      // 0…1, -1 = indeterminate
  std::string progress_text;  // "12 MB of 84 MB"
  std::string error;
};

// "84 MB", "1.2 GB".
std::string size_text(std::int64_t bytes);
// Some pair has its dictionary and Mozilla models.
bool has_pair(const Manifest &m, const std::map<std::string, std::string> &installed);
// The first-run window opens at launch: no pair yet, or Accessibility not granted and the window never finished.
bool first_run_at_launch(bool has_pair, bool trusted, bool finished);
// The answer language by default: the system's when the packs offer it, else English.
std::string first_answer(const Manifest &m, const std::string &system);
FirstRunView first_run_view(const Manifest &m, const std::map<std::string, std::string> &installed, const FirstRunState &s);

}  // namespace lexipeek
