#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <cstdlib>
#include <filesystem>

// Nothing in a test run falls back to the person's data dir.
int main(int argc, char **argv) {
  auto d = (std::filesystem::temp_directory_path() / "lexipeek-test-data").string();
#if defined(_WIN32)
  _putenv_s("LEXIPEEK_DATA", d.c_str());
#else
  setenv("LEXIPEEK_DATA", d.c_str(), 1);  // NOLINT(concurrency-mt-unsafe)
#endif
  return doctest::Context(argc, argv).run();
}
