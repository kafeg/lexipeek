// Read aloud in the card (spec M9): what ▶ reads by the text and the tab; the rows' own ▶; its lines.
#include "views/read.hpp"
#include "lexipeek/l10n.hpp"

namespace lexipeek {

speech::Read card_read(const Card &c, const CardState &s, CardTab tab) {
  if (!c.prefs.read || c.text.empty()) return {};
  if (!c.study.empty()) {
    auto &l = c.study.front();
    if (!speech::speaks(l.lang)) return {};
    if (tab == CardTab::translate && !s.by_ai && !s.learned.empty() && s.learned.front().ok() && !s.learned.front().text.empty())
      return {s.learned.front().text, l.lang};
    if (tab == CardTab::dictionary && c.kind == Card::Kind::word && !l.words.empty() && s.section.key.empty() && s.entry_open < 0)
      return {l.words.front().base, l.lang};
    return {};
  }
  if (c.lang.empty() || c.lang == c.answer || !speech::speaks(c.lang)) return {};
  return {c.text, c.lang};
}

void read_view(const Card &c, const CardState &s, CardView &v) {
  v.read = card_read(c, s, v.tab);
  v.reading = !v.read.text.empty() && s.reading == v.read;
  if (!v.read.text.empty()) v.read_help = tr(v.reading ? "read_stop" : "read_help");
  // A language you learn: each translated word of a word's card has its own ▶.
  if (c.prefs.read && v.tab == CardTab::dictionary && c.kind == Card::Kind::word && !c.study.empty() && speech::speaks(c.study.front().lang))
    for (auto &r : v.rows)
      if (r.style == CardRow::Style::gloss && !r.link.empty())
        for (auto &w : c.study.front().words)
          if (w.base == r.link) {
            r.read = {w.base, c.study.front().lang};
            r.reading = s.reading == r.read;
          }
  if (s.read_loading) v.rows.push_back({CardRow::Style::progress, "", tr("read_loading"), ""});
  if (!s.read_error.empty()) {
    v.rows.push_back({CardRow::Style::error, "", s.read_error, ""});
    if (s.read_fix) v.fix = tr("read_open_settings"), v.fix_section = SettingsSection::speech, v.fix_tab = 0;
  }
}

}  // namespace lexipeek
