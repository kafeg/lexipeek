#include "packs/http.hpp"
#include "text/text.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <memory>
#include <mutex>

namespace lexipeek::http {

namespace {

struct Curl {
  CURL *h;
  Curl() : h(nullptr) {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    h = curl_easy_init();
  }
  ~Curl() { curl_easy_cleanup(h); }
  Curl(const Curl &) = delete;
  Curl &operator=(const Curl &) = delete;
};

void common(CURL *h, const std::string &url) {
  curl_easy_setopt(h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
  // A stalled connection fails after a minute below 1 byte/s; the .part stays for the next try.
  curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 60L);
  curl_easy_setopt(h, CURLOPT_USERAGENT, "LexiPeek");
#if defined(__APPLE__) || defined(_WIN32)
  curl_easy_setopt(h, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif
}

std::string message(CURLcode c, const char *err) { return *err ? err : curl_easy_strerror(c); }

size_t to_string(char *p, size_t size, size_t n, void *body) {
  static_cast<std::string *>(body)->append(p, size * n);
  return size * n;
}

struct Sink {
  CURL *h;
  std::FILE *f;
  std::filesystem::path path;
  std::int64_t offset, total;
  const Progress *progress;
  const Cancel *cancel;
  bool checked = false;
  bool failed = false;
};

size_t to_file(char *p, size_t size, size_t n, void *user) {
  auto &s = *static_cast<Sink *>(user);
  if (!s.checked) {
    s.checked = true;
    long code = 0;
    curl_easy_getinfo(s.h, CURLINFO_RESPONSE_CODE, &code);
    // Asked from an offset, answered with the whole file: start the file over.
    if (s.offset > 0 && code == 200) {
      std::fclose(s.f);
      s.f = text::open_file(s.path, "wb");
      s.offset = 0;
      if (!s.f) return 0;
    }
  }
  if (std::fwrite(p, size, n, s.f) != n) {
    s.failed = true;
    return 0;
  }
  return size * n;
}

int on_progress(void *user, curl_off_t, curl_off_t now, curl_off_t, curl_off_t) {
  auto &s = *static_cast<Sink *>(user);
  if (*s.cancel && (*s.cancel)()) return 1;
  if (*s.progress) (*s.progress)(s.offset + now, s.total);
  return 0;
}

}  // namespace

std::string get(const std::string &url, std::string &body, long *status) {
  Curl c;
  if (!c.h) return "curl_easy_init failed";
  char err[CURL_ERROR_SIZE] = "";
  common(c.h, url);
  curl_easy_setopt(c.h, CURLOPT_ERRORBUFFER, err);
  curl_easy_setopt(c.h, CURLOPT_WRITEFUNCTION, to_string);
  curl_easy_setopt(c.h, CURLOPT_WRITEDATA, &body);
  auto r = curl_easy_perform(c.h);
  if (status) curl_easy_getinfo(c.h, CURLINFO_RESPONSE_CODE, status);
  return r == CURLE_OK ? "" : message(r, err);
}

Reply request(const std::string &method, const std::string &url, const std::vector<std::pair<std::string, std::string>> &headers,
              const std::string &body, long timeout_s) {
  Reply out;
  Curl c;
  if (!c.h) return out.error = "curl_easy_init failed", out;
  char err[CURL_ERROR_SIZE] = "";
  common(c.h, url);
  curl_easy_setopt(c.h, CURLOPT_FAILONERROR, 0L);
  curl_easy_setopt(c.h, CURLOPT_TIMEOUT, timeout_s);
  curl_easy_setopt(c.h, CURLOPT_ERRORBUFFER, err);
  curl_easy_setopt(c.h, CURLOPT_WRITEFUNCTION, to_string);
  curl_easy_setopt(c.h, CURLOPT_WRITEDATA, &out.body);
  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> list(nullptr, curl_slist_free_all);
  for (auto &[k, v] : headers) list.reset(curl_slist_append(list.release(), (k + ": " + v).c_str()));
  curl_easy_setopt(c.h, CURLOPT_HTTPHEADER, list.get());
  if (method == "POST") {
    curl_easy_setopt(c.h, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c.h, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
  }
  auto r = curl_easy_perform(c.h);
  curl_easy_getinfo(c.h, CURLINFO_RESPONSE_CODE, &out.status);
  if (r != CURLE_OK) {
    out.error = message(r, err);
    out.resolve = r == CURLE_COULDNT_RESOLVE_HOST;
  }
  return out;
}

Status download(const std::string &url, const std::filesystem::path &file, std::int64_t total, const Progress &progress, const Cancel &cancel) {
  Curl c;
  if (!c.h) return {"curl_easy_init failed"};
  std::error_code ec;
  auto have = std::filesystem::exists(file, ec) ? static_cast<std::int64_t>(std::filesystem::file_size(file, ec)) : 0;
  if (ec) have = 0;
  Sink s{c.h, text::open_file(file, have ? "ab" : "wb"), file, have, total, &progress, &cancel};
  if (!s.f) return {"Cannot write " + file.filename().string()};
  char err[CURL_ERROR_SIZE] = "";
  common(c.h, url);
  curl_easy_setopt(c.h, CURLOPT_ERRORBUFFER, err);
  curl_easy_setopt(c.h, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(have));
  curl_easy_setopt(c.h, CURLOPT_WRITEFUNCTION, to_file);
  curl_easy_setopt(c.h, CURLOPT_WRITEDATA, &s);
  curl_easy_setopt(c.h, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(c.h, CURLOPT_XFERINFOFUNCTION, on_progress);
  curl_easy_setopt(c.h, CURLOPT_XFERINFODATA, &s);
  auto r = curl_easy_perform(c.h);
  bool closed = s.f && std::fclose(s.f) == 0;
  if (r == CURLE_ABORTED_BY_CALLBACK) return {"", true};
  if (s.failed || (r == CURLE_OK && !closed)) return {"Cannot write " + file.filename().string()};
  if (r != CURLE_OK) return {message(r, err)};
  return {};
}

}  // namespace lexipeek::http
