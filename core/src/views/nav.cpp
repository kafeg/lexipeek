// The card's navigation: pure transitions, the shell does the look-ups they ask for.
#include <tuple>
#include "lexipeek/views.hpp"

namespace lexipeek {

void CardNav::reset() { *this = CardNav(); }

CardStep CardNav::look(const std::string &text, Source source, bool keep, const std::string &lang, const std::string &answer) {
  if (!keep) trail_.clear();
  before_ = trail_.empty() ? "" : trail_.back().first.text;
  state = CardState();
  CardStep s;
  s.kind = CardStep::Kind::lookup, s.text = text, s.source = source, s.lang = lang, s.answer = answer;
  return s;
}

void CardNav::arrived(const Card &c) {
  card_ = c, has_ = true;
  state = first_state(c);
  state.before = before_;
}

CardStep CardNav::open(const std::string &word) {
  if (!has_) return {};
  trail_.emplace_back(card_, state);
  return look(word, Source::input, true);
}

CardStep CardNav::relookup(const std::string &lang, const std::string &answer) {
  if (!has_) return {};
  return look(card_.text, card_.source, true, lang.empty() ? card_.lang : lang, answer.empty() ? card_.answer : answer);
}

CardStep CardNav::back() {
  // A section or one dictionary's entry shows only on Dictionary: elsewhere ← goes to the card before.
  bool dict = state.tab == CardTab::dictionary;
  if (dict && !state.section.key.empty()) {
    state.section = SectionPage();
    return {};
  }
  if (dict && state.entry_open >= 0) {
    state.entry_open = -1;
    return {};
  }
  if (trail_.empty()) return {};
  std::tie(card_, state) = trail_.back();
  trail_.pop_back();
  has_ = true;
  for (auto a : {ai::Ask::quick, ai::Ask::analysis, ai::Ask::context})
    if (card_answer(state, a).asking) state = with_answer(state, a, AiState());
  CardStep s;
  s.kind = CardStep::Kind::restore, s.translate = state.translating;
  return s;
}

void CardNav::open_entry(int entry) { state.entry_open = entry; }

CardStep CardNav::open_section(int entry, const std::string &key, int page) {
  if (!has_) return {};
  CardStep s;
  s.kind = CardStep::Kind::section, s.entry = entry, s.key = key, s.page = page;
  return s;
}

CardStep CardNav::turn(int by) {
  auto &p = state.section;
  if (state.tab != CardTab::dictionary) return {};
  if (p.key.empty()) {
    int to = state.entry_page + by;
    // Full entries: a dictionary per page; languages you learn: a language per page.
    int n = static_cast<int>(card_.study.empty() ? card_.entries.size() : card_.study.size());
    if ((state.full_entry || !card_.study.empty()) && state.entry_open < 0 && to >= 0 && to < n) state.entry_page = to;
    return {};
  }
  if (p.page + by < 0 || p.page + by >= p.pages) return {};
  return open_section(p.entry, p.key, p.page + by);
}

void CardNav::pick_method(Method m) {
  state.by_ai = false;
  translating(m);
}

void CardNav::pick_ai() { state.by_ai = true; }

void CardNav::translating(Method m) {
  state.has_method = true, state.method = m, state.translating = true;
  state.learned.clear();
}

void CardNav::learned(int n, Method m, const Translation &t) {
  if (state.has_method && state.method == m && n == static_cast<int>(state.learned.size())) state.learned.push_back(t);
}

void CardNav::translated(Method m, const Translation &t) {
  if (!state.has_method || state.method != m) return;
  state.translation = t, state.translated = true, state.translating = false;
}

void CardNav::next_tab(int by) {
  if (!has_) return;
  auto items = card_view(card_, state).tab_items;
  if (items.size() < 2) return;
  int n = static_cast<int>(items.size()), at = 0;
  for (int i = 0; i < n; i++)
    if (items[static_cast<size_t>(i)].tab == state.tab) at = i;
  state.tab = items[static_cast<size_t>(((at + by) % n + n) % n)].tab;
}

bool CardNav::busy() const { return state.translating || state.quick.asking || state.analysis.asking || state.context.asking; }

std::string CardNav::place() const {
  if (!has_) return "";
  return card_.text + "|" + std::to_string(static_cast<int>(state.tab)) + "|" + (state.full_entry ? "1" : "0") + "|" +
         std::to_string(state.entry_open) + "|" + std::to_string(state.entry_page) + "|" + state.section.key + "|" +
         std::to_string(state.section.page) + "|" + (state.by_ai ? "1" : "0");
}

}  // namespace lexipeek
