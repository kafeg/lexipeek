// Swift's door to the core's facade (lexipeek/app.hpp, views.hpp): only names for template instances Swift has to write.
#pragma once
#include <string>
#include <vector>
#include "lexipeek/app.hpp"

namespace lexipeek::mac {

using Strings = std::vector<std::string>;
using Models = std::vector<ai::Model>;

}  // namespace lexipeek::mac
