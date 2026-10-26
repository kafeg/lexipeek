// Swift's door to the core's facade (lexipeek/app.hpp, views.hpp). Only what Swift's C++ interop cannot express is
// here: names for template instances, and reading a std::optional.
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "lexipeek/app.hpp"

namespace lexipeek::mac {

using Strings = std::vector<std::string>;
using Rows = std::vector<PairRow>;
using Recents = std::vector<Seen>;
using DictRows = std::vector<DictRow>;
using FolderRows = std::vector<FolderRow>;
using CatalogRows = std::vector<CatalogRow>;
using SourceRows = std::vector<SourceRow>;
using SettingRows = std::vector<SettingRow>;

inline bool has_part(const std::optional<PackPart> &p) { return p.has_value(); }
inline PackPart part(const std::optional<PackPart> &p) { return p.value_or(PackPart{}); }

}  // namespace lexipeek::mac
