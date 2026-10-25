// Pure parts of translation (ported from InkPeek's mt/Engine.kt and mt/Llm.kt), tested without models.
#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace lexipeek::mt {

// Mozilla model directions a from → to translation needs, in order (through English when neither side is English).
std::vector<std::pair<std::string, std::string>> steps(const std::string &from, const std::string &to);

// Marian options for one model folder (model, lex shortlist, vocab or src / trg vocab); "" when a file is missing.
std::string bergamot_config(const std::filesystem::path &dir);

// Sentences of a text as InkPeek's Engine.sentences: UAX #29 boundaries, each piece trimmed (a blank line: "").
std::vector<std::string> sentences(const std::string &text);
// Boundaries in code points, from 0 to the length.
std::vector<size_t> sentence_breaks(const std::u32string &text);
std::u32string utf32(const std::string &s);
std::string utf8(const std::u32string &s);
// Trimmed of whitespace as Kotlin's String.trim() (Unicode spaces too).
std::string trim_space(const std::string &s);

// Whole sentences joined into pieces of at most max characters (a longer sentence is a piece of its own).
std::vector<std::string> chunks(const std::string &text, size_t max = 1200);

// Length in UTF-16 units, as Kotlin's String.length: InkPeek's limits are in these.
size_t utf16_length(const std::string &s);

std::string trim(const std::string &s);

// The prompt from a template with {from}, {to}, {text}.
std::string fill(std::string prompt, const std::string &from, const std::string &to, const std::string &text);

}  // namespace lexipeek::mt
