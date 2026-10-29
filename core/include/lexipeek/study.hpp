// The language you learn (InkPeek's Study, settings.json "study": a list of one, kept as a list): per answer language,
// a text already in it is shown in that language.
#pragma once
#include <string>
#include <vector>
#include "lexipeek/packs.hpp"

namespace lexipeek {

inline constexpr std::size_t study_max = 1;

// The stored list: the first code ([a-z]{2,3}) only (an older list of two keeps its first).
std::vector<std::string> study_parse(const std::vector<std::string> &codes);
// The languages offered for answer: those with a pack answer → X among pairs, each once, by code.
std::vector<std::string> study_offered(const std::string &answer, const std::vector<LangPair> &pairs);
// What the card uses, in the stored order: those whose pack answer → X is among pairs (installed).
std::vector<std::string> study_effective(const std::vector<std::string> &stored, const std::string &answer, const std::vector<LangPair> &pairs);
// The card's direction: "ru → de".
std::string study_direction(const std::string &answer, const std::vector<std::string> &langs);
// The translated words of a pack line's meaning ("Haus, Gebäude (alt), Dynastie"): each once, notes in brackets dropped.
std::vector<std::string> study_words(const std::string &meaning, std::size_t max = 5);

}  // namespace lexipeek
