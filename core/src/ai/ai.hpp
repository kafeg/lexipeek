// The AI requests over a transport (HTTP and programs), so tests run them without a network.
#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "lexipeek/ai.hpp"
#include "lexipeek/store.hpp"
#include "packs/http.hpp"

namespace lexipeek::ai {

struct Call {
  std::string method = "POST", url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  long timeout = 45;  // s: the answer comes in one piece, so this covers the whole generation
};

using Ran = CliRan;

struct Transport {
  std::function<http::Reply(const Call &)> http;
  std::function<Ran(const CliCall &)> run;
};

// libcurl and the shell.
Transport default_transport();
// Answers from files, for snapshots and tests: <dir>/<section>.json is the model's JSON for every provider; <dir>/status
// (e.g. 401) fails every request with that status; <dir>/models.json the model list.
Transport file_transport(const std::filesystem::path &dir);

// One request about a text.
struct Query {
  Ask ask = Ask::quick;
  std::string key;     // the provider's API key ("" for a CLI or a server without one)
  std::string text;
  int words = 1;       // 1: a word; up to 8 in one sentence: a phrase; else a passage
  bool passage = false;
  std::string lang;    // the answer language's English name ("Russian")
  std::string source;  // the text's language as the packs tell it ("German"), "" = none
};

// Blocking. recall: the least-reasoning variants that worked (written back).
Answer ask(const Query &q, const AiSettings &s, std::map<std::string, int> &recall, const Transport &t);
Models models(const AiSettings &s, const std::string &key, const Transport &t);
// The saved answer, else asked and saved (InkPeek's Answers.get): lang is the answer language's English name.
// keep: the answer goes to the store (History off: it lives only in the card).
Answer answer(Store &store, const Query &q, const AiSettings &s, std::map<std::string, int> &recall, const Transport &t, bool keep = true);
// Removes <think>…</think> blocks; the JSON object of a reply (also inside a code fence or a sentence), "" when none.
std::string strip_thinking(const std::string &s);
std::string json_object(const std::string &s);
// A word for the shell, single-quoted.
std::string shell_quote(const std::string &s);
// The CLI's call for a prompt (the system prompt first).
CliCall cli_command(const AiSettings &s, const std::string &system, const std::string &prompt);
// The POSIX shell line for it: the usual install places on PATH, the temp folder, the prompt last.
std::string shell_line(const CliCall &c);

}  // namespace lexipeek::ai
