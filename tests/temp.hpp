// Shared by the tests: a temporary folder away, a wait for a condition.
#pragma once
#include <chrono>
#include <filesystem>
#include <system_error>
#include <thread>

// On Windows a store still open keeps its files, so a failure is not the test's.
inline void wipe(const std::filesystem::path &p) {
  std::error_code ec;
  std::filesystem::remove_all(p, ec);
}

// Polls done() until it is true or the time is up: a loaded CI runner is slow, so no fixed sleep decides a test.
template <class F>
bool until(F done, int seconds = 30) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (!done()) {
    if (std::chrono::steady_clock::now() > end) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}
