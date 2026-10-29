// Languages you learn (InkPeek's Study, settings.json "study"): per answer language, a text already in it is shown in them.
#pragma once
#include <string>
#include <vector>
#include "lexipeek/packs.hpp"

namespace lexipeek {

inline constexpr std::size_t study_max = 2;

// The stored list: codes ([a-z]{2,3}), each once, two at most, in order.
std::vector<std::string> study_parse(const std::vector<std::string> &codes);
// Ticking adds at the end (not past two); unticking removes.
std::vector<std::string> study_toggle(const std::vector<std::string> &langs, const std::string &code, bool on);
// The languages offered for answer: those with a pack answer → X among pairs, each once, by code.
std::vector<std::string> study_offered(const std::string &answer, const std::vector<LangPair> &pairs);
// What the card uses, in the stored order: those whose pack answer → X is among pairs (installed).
std::vector<std::string> study_effective(const std::vector<std::string> &stored, const std::string &answer, const std::vector<LangPair> &pairs);
// The card's direction: "ru → de, en".
std::string study_direction(const std::string &answer, const std::vector<std::string> &langs);
// The translated words of a pack line's meaning ("Haus, Gebäude (alt), Dynastie"): each once, notes in brackets dropped.
std::vector<std::string> study_words(const std::string &meaning, std::size_t max = 5);

}  // namespace lexipeek
