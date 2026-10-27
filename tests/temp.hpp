// A test's temporary folder away: on Windows a store still open keeps its files, so a failure is not the test's.
#pragma once
#include <filesystem>
#include <system_error>

inline void wipe(const std::filesystem::path &p) {
  std::error_code ec;
  std::filesystem::remove_all(p, ec);
}
