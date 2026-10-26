// What a shell's screens show, worked out in the core: the shells only draw these.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
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

}  // namespace lexipeek
