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
};

// One line of the card's body.
struct CardRow {
  enum class Style {
    note,      // secondary text: "Already in Russian."
    progress,  // a spinner and its text: "Translating…"
    text,      // selectable text: the translation
    error,     // red: a failed translation
    entry,     // a dictionary's entry: label (the dictionary), text (its short line) or html (the full article)
    gloss      // a word of a phrase: label bold ("mache … zu (zumachen)"), text secondary (its meaning)
  };
  Style style = Style::note;
  std::string label, text, html;
};

struct MethodItem {
  Method method = Method::fast;
  std::string title;  // "Direct · OPUS-MT" or "Direct · OPUS-MT — reason"
  bool enabled = false;
};

struct CardView {
  HeaderStyle style = HeaderStyle::passage;
  std::string title;     // the header ("das Haus (Hauses, pl. Häuser)") or the text
  std::string subtitle;  // "→ Traum": the base form
  std::string tooltip;   // the text as asked
  std::string badge;     // "de → ru", "Russian" (already in it), ""
  bool wide = false;     // a passage: a wider card
  bool tabs = false;     // Dictionary · Translate are shown
  CardTab tab = CardTab::translate;  // the one shown
  std::vector<CardRow> rows;
  std::string entry_toggle;  // "Full entry ⏎", "Short ⏎", "" (none)
  bool menu = false;         // the methods' menu is shown
  std::string menu_title;    // the current method's name or "No method available"
  std::vector<MethodItem> methods;
  std::string looks;  // "Looked up 3×", ""
};

// The tab a new card opens on: Dictionary for a word with entries, else Translate.
CardTab first_tab(const Card &c);
// A new card's state: its first tab and its method (none when no method is available).
CardState first_state(const Card &c);
CardView card_view(const Card &c, const CardState &s);

}  // namespace lexipeek
