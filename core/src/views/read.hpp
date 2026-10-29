// Read aloud in the card (spec M9): ▶ and the rows' own, the loading line, a failure.
#pragma once
#include "lexipeek/views.hpp"

namespace lexipeek {

void read_view(const Card &c, const CardState &s, CardView &v);

}  // namespace lexipeek
