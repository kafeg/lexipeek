// A thin libcurl wrapper: a small GET into memory, a resumable download into a file (Range), progress and cancel.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include "lexipeek/packs.hpp"

namespace lexipeek::http {

// The body of [url] into [body]; "" or the error; [status] the HTTP status when asked.
std::string get(const std::string &url, std::string &body, long *status = nullptr);

// [url] appended to [file] from its current size (Range); a server that ignores the range starts it over.
// progress gets the bytes in the file and [total].
Status download(const std::string &url, const std::filesystem::path &file, std::int64_t total, const Progress &progress, const Cancel &cancel);

}  // namespace lexipeek::http
