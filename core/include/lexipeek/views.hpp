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

// The language's name in the app's language ("de" → "German", "Немецкий"); the catalog's English one, else the code.
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

// "Fast · Mozilla", "Direct · OPUS-MT", "AI model · Hy-MT2".
std::string method_title(Method m);

// The card: what the shell draws from a Card and its state.
enum class CardTab { dictionary, translate, analysis, context };

// One AI answer of the card (InkPeek's Answers): asked, then its answer or its error's text.
struct AiState {
  bool asking = false;
  bool answered = false;  // json or error holds the outcome
  bool ok = false;
  bool saved = false;     // it came from the store (offline)
  std::string json, error;
};
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
  bool by_ai = false;  // Translate shows the AI translation (a method picked in the card, or Card → Opening)
  AiState quick, analysis, context;
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
    english,   // Mozilla's English in between, smaller, selectable, under the translation
    markdown   // an AI answer's paragraph: light Markdown (**bold**, *italic*), "• " lists, selectable
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
  bool ai = false;    // the AI translation ("AI · claude-sonnet-5-5"), not an offline method
};

struct TabItem {
  CardTab tab = CardTab::translate;
  std::string title;  // "Dictionary", "Translate", "Analysis · AI"
};

struct CardView {
  HeaderStyle style = HeaderStyle::passage;
  std::string title;     // the header ("das Haus (Hauses, pl. Häuser)") or the text
  std::string subtitle;  // a word's small line, InkPeek's: "form: Häusern\namong the 1,000 most frequent words · looked up 3 times"
  std::string tooltip;   // the text as asked
  std::string badge;     // "de → ru", "Russian" (already in it), "French" (no pack from it), ""
  bool wide = false;     // a passage: a wider card
  bool tabs = false;     // more than one tab: tab_items are shown
  std::vector<TabItem> tab_items;    // Dictionary · Translate · Analysis · AI · Context · AI, those the card has
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
  int width = 400;        // the card's look (Settings → Card → Appearance): points
  int text = 13;          // the body's text size, points; the header a little larger
  std::string font;       // "" the system's, "serif", "rounded", "mono"
  int height = 45;        // % of the screen at most
};

// The tab a new card opens on: Dictionary for a word with entries or a phrase set to open word by word, else Translate.
CardTab first_tab(const Card &c);
// A new card's state: its first tab, the full entry when set so, its method (none when no method is available).
CardState first_state(const Card &c);
CardView card_view(const Card &c, const CardState &s);
// The AI requests to send now, the shown one first (only on the person's action: its tab opened, AI translation picked,
// or Card → Opening set so); the others the card shows go with it when prefetch is on. A failed shown one asks again.
std::vector<ai::Ask> card_asks(const Card &c, const CardState &s);
AiState card_answer(const CardState &s, ai::Ask a);
// The state with an answer set (or asking marked).
CardState with_answer(const CardState &s, ai::Ask a, const AiState &answer);
// What ☆ saves as the line: the translation shown (the AI's brief one when by AI), "" when none.
std::string shown_translation(const CardState &s);
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

// Settings: InkPeek's sections as toolbar tabs, each with its sub-tabs; every pane's rows come from here.
enum class SettingsSection { languages, card, ai, notes, general };

// What the shell knows and the core does not: this Mac and the app.
struct SettingsState {
  bool trusted = false;   // Accessibility is granted
  bool has_pair = true;   // some pair is installed
  bool login = false;     // opens at login (the system's say)
  std::string version;    // "0.1 (5)"
  std::vector<std::string> answers;   // the packs' answer languages (codes)
  std::string input = "⌥A";           // the input hotkey's label
  bool checking = false;              // the update check is running
  std::string update;                 // its last answer ("" = not checked), update_check's text
  std::string update_version, update_url;  // a newer release and its page
  bool ai_key = false;                // the provider's API key is in the Keychain
  bool ai_testing = false;            // AI → Connection's test is running
  std::string ai_test;                // its last answer ("" = not tried)
};

// One row, InkPeek's Rows: a title, a one-line description, a control.
struct SettingRow {
  enum class Kind {
    heading,  // a group's title
    toggle,   // on: its state
    choice,   // choices, selected
    hotkey,   // a shortcut recorder: key "lookup" / "input"
    apps,     // a list of apps by bundle id: key "copy_apps" / "ignore_apps"
    button,   // value: the button's label; the shell does what key names ("setup", "access", "order", "updates")
    status,   // value: the state ("Allowed"), on: good
    link,     // value: a URL
    text,     // a line to type: value, placeholder (key "ai_url", "ai_command")
    secret,   // the API key: the shell's field and Save (the key never comes here); on: one is kept
    model     // the model: value (a line to type), placeholder its default; on: "Choose model…" from the provider's list
  };
  Kind kind = Kind::heading;
  std::string key = "", title = "", about = "";
  bool on = false, enabled = true;
  std::vector<std::string> choices = {};
  int selected = 0;
  std::string value = "";
  std::string placeholder = "";
  std::string edit = "";  // a prompt id: ✎ opens its editor (prompt_editor)
};

struct SettingsPage {
  std::vector<std::string> sections;  // "Languages", "Card", "Notes", "General"
  std::vector<std::string> tabs;      // the section's sub-tabs ("Packs", "Translation", "Other dictionaries"), none for General
  int tab = 0;
  std::string pane;  // a pane drawn by its own view: "packs", "other", "notes"; "" = rows
  std::vector<SettingRow> rows;
};

std::vector<std::string> settings_sections();
std::vector<std::string> settings_tabs(SettingsSection section);
// The tabs' names that do not change with the language ("packs", "other-dictionaries"): snapshot names.
std::vector<std::string> settings_tab_keys(SettingsSection section);
SettingsPage settings_view(SettingsSection section, int tab, const Settings &s, const SettingsState &state);
// A row's change → the settings ("" when the key is not the row's). Toggles and choices by the row's key and index.
void settings_toggle(Settings &s, const std::string &key, bool on);
void settings_choose(Settings &s, const std::string &key, int index, const SettingsState &state);
// The update check's answer from GitHub's latest release (status 404: none yet): its text; the release when it is newer.
struct UpdateCheck {
  std::string text;          // "LexiPeek 0.2 is out", "This is the latest version (0.1).", "No releases yet.", "Failed: …"
  std::string version, url;  // the newer release's version and page ("" = none)
};
UpdateCheck update_check(const std::string &version, long status, const std::string &body, const std::string &error);
// "0.10" > "0.9"; a leading "v" ignored.
bool newer_version(const std::string &release, const std::string &current);

// AI → Modules' prompt editor: one field per edited part (its id, a label or none, the text now and the original).
struct PromptField {
  std::string id, label, text, original;
};
struct PromptEditor {
  std::string title, note;
  std::vector<PromptField> fields;
  std::string cancel, reset, save;  // the buttons
};
PromptEditor prompt_editor(const Settings &s, const std::string &id);
// The editor's texts: an empty one or the original is "not edited" (a later version can improve it).
void settings_prompts(Settings &s, const std::vector<std::string> &ids, const std::vector<std::string> &texts);
// A text row's change (ai_url, ai_command, ai_model).
void settings_text(Settings &s, const std::string &key, const std::string &value);

// "1 language pair installed"; a pair's second line ("Full · Mozilla · OPUS-MT", "Basic · 84 MB").
std::string packs_summary(const std::vector<PairRow> &rows);
std::string pair_detail(const PairRow &r);

}  // namespace lexipeek
