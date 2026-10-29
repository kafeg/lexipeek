// The optional AI layer (InkPeek's ai/): providers, its three requests and their prompts, answers as JSON, errors.
// Nothing in the core waits for it; a request goes out only on the person's action.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "lexipeek/settings.hpp"

namespace lexipeek::ai {

// Quick: AI translation (head facts and the translation); Analysis: grammar, words; Context: explanation, examples.
enum class Ask { quick, analysis, context };

// Why there is no answer (InkPeek's AiErrors), ok when there is one.
enum class Code { ok, off, no_key, no_model, no_server, offline, unreachable, server, region, key, rate, credits, declined, not_json, no_cli, failed };

struct Provider {
  std::string id, title;  // "anthropic", "Claude (Anthropic)"
  std::string url;        // the API's base ("" for a CLI or a server of one's own)
  std::string model;      // its default model ("" = none)
  bool key = true;        // needs an API key
  bool cli = false;       // runs a program (claude -p, codex exec)
  bool list = true;       // offers a model list
};
const std::vector<Provider> &providers();
// The provider by id; nullptr for "" (off) and unknown ids.
const Provider *provider(const std::string &id);

// Ready addresses for the OpenAI-compatible server; for a local one only the IP needs changing.
struct Preset {
  std::string name, url;
};
const std::vector<Preset> &presets();

// One key of an answer: its name and JSON type are fixed (s string, b boolean, a strings, e examples), its about can be
// edited; an extra one is off until picked.
struct Key {
  std::string name, type, about;
  bool extra = false;
};
const std::vector<Key> &keys(Ask a);
std::string section(Ask a);  // "quick", "analysis", "context": the answers' section and the prompts' prefix
const std::string &system_prompt();  // the general style ({lang}), editable as "system"
// The keys asked now (AI → Modules); none: the module is off.
std::vector<Key> active(Ask a, const AiSettings &s);
bool module_on(Ask a, const AiSettings &s);
// The prompt with {text}, {lang} left in: the edited abouts, or the originals (edited false).
std::string prompt(Ask a, const AiSettings &s, bool edited = true);
// InkPeek's Ai.STUDY paragraph for languages you learn (English names, in order), after "\n"; "" for none.
std::string study_prompt(const std::vector<std::string> &names);
// What an answer is kept under: Java's hashCode of the original prompt (InkPeek's Ask.version), with the study paragraph
// when there is one (InkPeek's Ask.version(study)).
std::int64_t version(Ask a, const AiSettings &s, const std::vector<std::string> &study = {});
// The provider's model: the setting, else its default.
std::string model(const AiSettings &s);
// "AI · claude-sonnet-5-5" (the Translate method's name).
std::string method_title(const AiSettings &s);

struct Answer {
  Code code = Code::ok;
  std::string json;    // the answer object
  std::string detail;  // what the server or program said (never the key)
  bool saved = false;  // from the store, not asked now
  bool ok() const { return code == Code::ok; }
};

// A short, actionable text for a failed answer (the server's address for one's own server).
std::string error_text(const Answer &a, const AiSettings &s);
// A tab's light Markdown from its answer, under the app's headings (InkPeek's AiPage.render).
std::string render(Ask a, const std::string &json, bool word);
// One line of a quick answer for Notes: the translation, else "das Haus — house, home".
std::string brief(const std::string &json);

// A model from the provider's list; prices in USD per million tokens (-1 = unknown).
struct Model {
  std::string id, name, publisher;
  bool free = false;
  double in = -1, out = -1;
  int context = 0;
};
struct Models {
  std::vector<Model> list;
  Answer error;  // code ok when the list came
};
// The list's rows by a filter and "free only": "id · free · 128k".
std::vector<std::string> model_titles(const std::vector<Model> &list);
std::vector<Model> filter_models(const std::vector<Model> &list, const std::string &filter, bool free_only);

// A CLI provider's program call. The POSIX run puts the prompt last on its shell line; a runner may give it on stdin
// instead, with stdin_arg ("-" for codex, "" for claude: no prompt argument means stdin).
struct CliCall {
  std::string program;
  std::vector<std::string> args;
  std::vector<std::pair<std::string, std::string>> env;
  std::string prompt, stdin_arg;
};
struct CliRan {
  int code = -1;  // the program's exit code (127: not found, 143: stopped after cli_timeout)
  std::string out, err;
};
// How long a CLI provider may take (seconds): then it is stopped and the card says so.
inline constexpr int cli_timeout = 120;
// A shell's own way to run CLI providers (Windows: a process and a job object); none: a POSIX shell line.
void set_cli_runner(std::function<CliRan(const CliCall &)> run);

}  // namespace lexipeek::ai
