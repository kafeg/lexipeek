// How the AI requests leave: libcurl, a shell for the CLI providers, or files (snapshots, tests).
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <nlohmann/json.hpp>
#include "ai/ai.hpp"

namespace lexipeek::ai {

namespace fs = std::filesystem;

namespace {

std::string read(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

#ifdef _WIN32
FILE *open_pipe(const std::string &c) { return _popen(c.c_str(), "r"); }
int close_pipe(FILE *f) { return _pclose(f); }
#else
FILE *open_pipe(const std::string &c) { return popen(c.c_str(), "r"); }
// pclose gives a wait status: the exit code is its second byte.
int close_pipe(FILE *f) { return (pclose(f) >> 8) & 0xff; }
#endif

std::function<CliRan(const CliCall &)> &runner() {
  static std::function<CliRan(const CliCall &)> r;
  return r;
}

// The command's stdout; its stderr into a temp file read after it ends. A program that hangs is stopped after
// cli_timeout seconds (exit 143: SIGTERM). A shell's own runner instead when it set one (Windows).
Ran run(const CliCall &call) {
  if (runner()) return runner()(call);
  Ran r;
  std::random_device rd;
  auto err = fs::temp_directory_path() / ("lexipeek-ai-" + std::to_string(rd()) + ".err");
  auto command = shell_line(call);
#ifdef _WIN32
  auto line = "{ " + command + "; } 2>" + shell_quote(err.string());
#else
  // set -m: the command is a process group of its own, stopped whole (its programs hold the pipe); the watcher writes
  // nowhere, so the pipe ends when the command does.
  auto line = "exec 2>/dev/null; set -m; { " + command + "; } 2>" + shell_quote(err.string()) + " & p=$!; ( sleep " +
              std::to_string(cli_timeout) + "; kill -TERM -$p ) >/dev/null 2>&1 & w=$!; wait $p; s=$?; kill $w; exit $s";
#endif
  auto *f = open_pipe(line);
  if (!f) return r.err = "cannot start a shell", r;
  char buf[4096];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) r.out.append(buf, n);
  r.code = close_pipe(f);
  r.err = read(err);
  std::error_code ec;
  fs::remove(err, ec);
  return r;
}

}  // namespace

void set_cli_runner(std::function<CliRan(const CliCall &)> run) { runner() = std::move(run); }

Transport default_transport() {
  return {[](const Call &c) { return http::request(c.method, c.url, c.headers, c.body, c.timeout); }, run};
}

Transport file_transport(const fs::path &dir) {
  // The section is named in the prompt's schema keys: the first key of each request tells which one it is.
  auto which = [](const std::string &body) {
    if (body.find("grammar") != std::string::npos) return std::string("analysis");
    if (body.find("explanation") != std::string::npos) return std::string("context");
    return std::string("quick");
  };
  auto status = [dir] {
    auto s = read(dir / "status");
    return s.empty() ? 0L : std::stol(s);
  };
  return {[=](const Call &c) {
            http::Reply r;
            if (auto s = status()) return r.status = s, r.body = R"({"error":{"message":"from the test's status file"}})", r;
            r.status = 200;
            if (c.method == "GET") return r.body = read(dir / "models.json"), r;
            auto answer = read(dir / (which(c.body) + ".json"));
            if (c.url.ends_with("/v1/messages")) {
              nlohmann::json j{{"content", {{{"type", "text"}, {"text", answer}}}}, {"stop_reason", "end_turn"}};
              r.body = j.dump();
            } else {
              nlohmann::json j{{"choices", {{{"message", {{"content", answer}}}}}}};
              r.body = j.dump();
            }
            return r;
          },
          [=](const CliCall &call) {
            Ran r;
            if (auto s = status()) return r.code = 1, r.err = "exit " + std::to_string(s), r;
            r.code = 0;
            r.out = read(dir / (which(call.prompt) + ".json"));
            return r;
          }};
}

}  // namespace lexipeek::ai
