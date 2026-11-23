// Read aloud in the card (spec M9): what ▶ reads by the text and the tab, its menu, the rows' own ▶, its lines.
#include "views/read.hpp"
#include <sstream>
#include "lexipeek/l10n.hpp"
#include "text/text.hpp"

namespace lexipeek {

namespace {

// A headword without its grammar: "das Haus (Hauses, pl. Häuser)" → "das Haus"; none: the word.
std::string spoken(const std::string &header, const std::string &word) {
  auto h = header.substr(0, header.find(" ("));
  while (!h.empty() && h.back() == ' ') h.pop_back();
  return h.empty() ? word : h;
}

// The forms in a headword's brackets as they are read: "das Haus (Hauses, мн. Häuser)" → "Hauses, Häuser". Every pack
// writes the grammar inside them as abbreviations in its answer language ("мн.", "pl.", "Pl.", "m."), and a form never
// ends in a dot, so dropping the dotted words leaves the forms of every pair. A form that only repeats the headword's
// own last word is dropped too ("das Go (Go)"). The marker after the brackets ("сильн.", "strong") is already gone.
std::string forms_of(const std::string &header, const std::string &head) {
  auto at = header.find(" (");
  auto end = at == std::string::npos ? at : header.find(')', at);
  if (end == std::string::npos) return "";
  auto inside = header.substr(at + 2, end - at - 2);
  auto space = head.rfind(' ');
  auto last = text::lower(space == std::string::npos ? head : head.substr(space + 1));
  std::string out;
  std::stringstream pieces(inside);
  for (std::string one; std::getline(pieces, one, ',');) {
    std::string form;
    std::stringstream words(one);
    for (std::string w; words >> w;)
      if (w.back() != '.') form += (form.empty() ? "" : " ") + w;
    if (form.empty() || text::lower(form) == last) continue;
    out += (out.empty() ? "" : ", ") + form;
  }
  return out;
}

// What a word's ▶ says: its headword, with the forms in its brackets when the menu is set so.
std::string said(const std::string &header, const std::string &word, bool forms) {
  auto head = spoken(header, word);
  if (!forms) return head;
  auto f = forms_of(header, head);
  return f.empty() ? head : head + ", " + f;
}

// The tooltip of ▶ by the phase of its reading.
std::string help(bool reading, speech::Phase p) {
  if (!reading) return tr("read_help");
  return tr(p == speech::Phase::loading ? "read_loading" : p == speech::Phase::ready ? "read_stop" : "read_making");
}

}  // namespace

speech::Read card_read(const Card &c, const CardState &s, CardTab tab) {
  if (!c.prefs.read || c.text.empty()) return {};
  if (!c.study.empty()) {
    auto &l = c.study.front();
    if (!speech::speaks(l.lang)) return {};
    if (tab == CardTab::translate && !s.by_ai && !s.learned.empty() && s.learned.front().ok() && !s.learned.front().text.empty())
      return {s.learned.front().text, l.lang};
    if (tab == CardTab::dictionary && c.kind == Card::Kind::word && !l.words.empty() && s.section.key.empty() && s.entry_open < 0)
      return {said(l.words.front().header, l.words.front().base, s.read_forms), l.lang};
    return {};
  }
  if (c.lang.empty() || c.lang == c.answer || !speech::speaks(c.lang)) return {};
  return {c.kind == Card::Kind::word ? said(c.header, c.base.empty() ? c.text : c.base, s.read_forms) : c.text, c.lang};
}

speech::Read note_read(const Card &c, const CardState &s) {
  auto top = s;
  top.section = {}, top.entry_open = -1;
  top.read_forms = false;  // what ☆ keeps, and Anki's Audio with it, is the word itself
  auto r = card_read(c, top, CardTab::dictionary);
  return r.text.empty() ? card_read(c, top, CardTab::translate) : r;
}

void read_view(const Card &c, const CardState &s, CardView &v) {
  v.read = card_read(c, s, v.tab);
  v.reading = !v.read.text.empty() && s.reading == v.read;
  v.read_phase = v.reading ? s.read_phase : speech::Phase::idle;
  if (!v.read.text.empty()) v.read_help = help(v.reading, v.read_phase);
  // ▶'s menu, only where the choice means something: the two readings of this word differ.
  if (!v.read.text.empty()) {
    auto word = s, forms = s;
    word.read_forms = false, forms.read_forms = true;
    if (card_read(c, word, v.tab).text != card_read(c, forms, v.tab).text) {
      v.read_menu = {{tr("read_forms_word"), false, !s.read_forms}, {tr("read_forms_all"), true, s.read_forms}};
      if (s.read_forms) v.read_mark = tr("read_forms_mark");  // off the default: said by ▶ itself, not only in the menu
    }
  }
  // A language you learn: each translated word of a word's card has its own ▶.
  if (c.prefs.read && v.tab == CardTab::dictionary && c.kind == Card::Kind::word && !c.study.empty() && speech::speaks(c.study.front().lang))
    for (auto &r : v.rows)
      if (r.style == CardRow::Style::gloss && !r.link.empty())
        for (auto &w : c.study.front().words)
          if (w.base == r.link) {
            r.read = {spoken(w.header, w.base), c.study.front().lang};
            r.reading = s.reading == r.read;
            r.read_phase = r.reading ? s.read_phase : speech::Phase::idle;
            r.read_help = help(r.reading, r.read_phase);
          }
  if (!s.read_error.empty()) {
    v.rows.push_back({CardRow::Style::error, "", s.read_error, ""});
    if (s.read_fix) v.fix = tr("read_open_settings"), v.fix_section = SettingsSection::speech, v.fix_tab = 0;
  }
}

}  // namespace lexipeek
