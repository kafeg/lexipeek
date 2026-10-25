#pragma once

namespace lexipeek::cli {

// now: the footprint (macOS phys_footprint, else resident set); peak: the peak resident set. MB.
struct Memory {
  double now_mb = 0, peak_mb = 0;
};

Memory memory();

}  // namespace lexipeek::cli
