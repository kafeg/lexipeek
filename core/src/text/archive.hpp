// libarchive by a path: wide on Windows, never the system's code page.
#pragma once
#include <archive.h>
#include <filesystem>

namespace lexipeek::text {

inline int open_archive(archive *a, const std::filesystem::path &p) {
#ifdef _WIN32
  return archive_read_open_filename_w(a, p.c_str(), 1 << 16);
#else
  return archive_read_open_filename(a, p.c_str(), 1 << 16);
#endif
}

}  // namespace lexipeek::text
