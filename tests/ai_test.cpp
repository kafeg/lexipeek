// The AI layer through a fake transport: InkPeek's prompts and versions, requests per provider, errors, the cache, views.
#include <doctest/doctest.h>
#include "temp.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "ai/ai.hpp"
#include "lexipeek/app.hpp"
#include "lexipeek/core.hpp"
#include "lexipeek/l10n.hpp"
#include "lexipeek/session.hpp"
#include "lexipeek/views.hpp"

using namespace lexipeek;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// Records the calls; answers each from a queue (the last one repeats).
struct Fake {
  std::vector<ai::Call> calls;
  std::vector<std::string> commands;
  std::vector<ai::CliCall> calls_cli;
  std::vector<http::Reply> replies;
  ai::Ran ran{0, "", ""};
  ai::Transport t() {
    return {[this](const ai::Call &c) {
              calls.push_back(c);
              auto r = replies.at(std::min(calls.size(), replies.size()) - 1);
              return r;
            },
            [this](const ai::CliCall &c) {
              commands.push_back(ai::shell_line(c));
              calls_cli.push_back(c);
              return ran;
            }};
  }
};

http::Reply ok(const std::string &body) { return {200, body, "", false}; }
http::Reply status(long s, const std::string &msg = "") { return {s, msg.empty() ? "" : json{{"error", {{"message", msg}}}}.dump(), "", false}; }
std::string claude_reply(const std::string &text) { return json{{"content", {{{"type", "text"}, {"text", text}}}}, {"stop_reason", "end_turn"}}.dump(); }
std::string openai_reply(const std::string &text) { return json{{"choices", {{{"message", {{"content", text}}}}}}}.dump(); }

AiSettings with(const std::string &provider, const std::string &model = "") {
  AiSettings s;
  s.provider = provider;
  if (!model.empty()) s.models[provider] = model;
  return s;
}

ai::Query query(ai::Ask a = ai::Ask::quick, const std::string &text = "Häusern") { return {a, "sk-test-123", text, 1, false, "Russian", "German"}; }

const std::string quick_json = R"({"base":"Haus","article":"das","plural":"die Häuser","pos":"сущ., ср. р.","forms":"","irregular":false,"form_of":"дат. мн.","meanings":["дом","здание"],"translation":""})";

bool has_header(const ai::Call &c, const std::string &k, const std::string &v) {
  for (auto &h : c.headers)
    if (h.first == k && h.second == v) return true;
  return false;
}

}  // namespace

TEST_CASE("ai: InkPeek's prompts, word for word: the same versions, so synced answers match") {
  AiSettings s;
  CHECK(ai::version(ai::Ask::quick, s) == 1031579369);
  CHECK(ai::version(ai::Ask::analysis, s) == -358010166);
  CHECK(ai::version(ai::Ask::context, s) == -357382659);
  CHECK(ai::prompt(ai::Ask::context, s).starts_with("Selected text: «{text}». Answer with one JSON object"));
  // An edited about keeps the saved answers (the owner's choice); another set of keys asks again.
  s.prompts["analysis.grammar"] = "Only the case.";
  CHECK(ai::version(ai::Ask::analysis, s) == -358010166);
  CHECK(ai::prompt(ai::Ask::analysis, s).find("- grammar: Only the case.") != std::string::npos);
  s.picked["analysis.related"] = true;
  CHECK(ai::version(ai::Ask::analysis, s) != -358010166);
  s.picked = {{"context.explanation", false}, {"context.examples", false}};
  CHECK(!ai::module_on(ai::Ask::context, s));
}

TEST_CASE("ai: Claude — structured output, effort low, the least thinking per model, refusal fallback, a 400 asks plainly") {
  Fake f;
  f.replies = {ok(claude_reply(quick_json))};
  std::map<std::string, int> recall;
  auto a = ai::ask(query(), with("anthropic"), recall, f.t());
  REQUIRE(a.ok());
  CHECK(json::parse(a.json)["base"] == "Haus");
  REQUIRE(f.calls.size() == 1);
  auto &c = f.calls[0];
  CHECK(c.url == "https://api.anthropic.com/v1/messages");
  CHECK(has_header(c, "x-api-key", "sk-test-123"));
  CHECK(has_header(c, "anthropic-version", "2023-06-01"));
  CHECK(has_header(c, "anthropic-beta", "server-side-fallback-2026-07-01"));
  auto b = json::parse(c.body);
  CHECK(b["model"] == "claude-sonnet-5-5");  // InkPeek's default
  CHECK(b["output_config"]["effort"] == "low");
  CHECK(b["output_config"]["format"]["type"] == "json_schema");
  CHECK(b["output_config"]["format"]["schema"]["required"].size() == 9);
  CHECK(b["output_config"]["format"]["schema"]["properties"]["irregular"]["type"] == "boolean");
  CHECK(b["thinking"]["type"] == "between_tools");
  CHECK(b["fallbacks"] == "default");
  CHECK(b["system"].get<std::string>().find("Write every answer in Russian only.") != std::string::npos);
  auto user = b["messages"][0]["content"].get<std::string>();
  CHECK(user.starts_with("The selection is a single word.\nThe text is probably German.\nSelected text: «Häusern»."));
  CHECK(user.find("{lang}") == std::string::npos);

  auto body = [&](const std::string &model) {
    Fake g;
    g.replies = {ok(claude_reply(quick_json))};
    ai::ask(query(), with("anthropic", model), recall, g.t());
    return json::parse(g.calls.at(0).body);
  };
  CHECK(!body("claude-opus-5-5").contains("thinking"));  // cannot be off: effort low only
  CHECK(!body("claude-fable-5-1").contains("thinking"));
  CHECK(body("claude-haiku-5-5")["thinking"]["type"] == "disabled");
  CHECK(!body("claude-haiku-5-5").contains("fallbacks"));

  // An older model rejects these: once more without, remembered.
  Fake h;
  h.replies = {status(400, "unknown field"), ok(claude_reply(quick_json))};
  REQUIRE(ai::ask(query(), with("anthropic", "claude-3-old"), recall, h.t()).ok());
  REQUIRE(h.calls.size() == 2);
  CHECK(!json::parse(h.calls[1].body).contains("output_config"));
  CHECK(recall["claude-json|claude-3-old"] == 1);
  Fake i;
  i.replies = {ok(claude_reply(quick_json))};
  ai::ask(query(), with("anthropic", "claude-3-old"), recall, i.t());
  CHECK(!json::parse(i.calls.at(0).body).contains("output_config"));

  Fake r;
  r.replies = {ok(json{{"content", json::array()}, {"stop_reason", "refusal"}}.dump())};
  CHECK(ai::ask(query(), with("anthropic"), recall, r.t()).code == ai::Code::declined);
}

TEST_CASE("ai: OpenAI-compatible — JSON mode, the host's no-reasoning field, the next one on 400, remembered") {
  std::map<std::string, int> recall;
  Fake f;
  f.replies = {ok(openai_reply("```json\n" + quick_json + "\n```"))};
  auto a = ai::ask(query(), with("openrouter"), recall, f.t());
  REQUIRE(a.ok());  // a code fence around the JSON
  auto &c = f.calls.at(0);
  CHECK(c.url == "https://openrouter.ai/api/v1/chat/completions");
  CHECK(has_header(c, "Authorization", "Bearer sk-test-123"));
  CHECK(has_header(c, "X-Title", "LexiPeek"));
  auto b = json::parse(c.body);
  CHECK(b["model"] == "openrouter/free");
  CHECK(b["reasoning"]["effort"] == "none");
  CHECK(b["response_format"]["type"] == "json_object");
  CHECK(b["messages"][0]["role"] == "system");

  // OpenAI: none rejected → low; JSON mode kept.
  Fake g;
  g.replies = {status(400), ok(openai_reply(quick_json))};
  REQUIRE(ai::ask(query(), with("openai", "gpt-x"), recall, g.t()).ok());
  CHECK(json::parse(g.calls.at(0).body)["reasoning_effort"] == "none");
  CHECK(json::parse(g.calls.at(1).body)["reasoning_effort"] == "low");
  CHECK(recall["api.openai.com|gpt-x"] == 1);

  // A local server without JSON mode or reasoning fields: every variant, then without JSON mode.
  auto local = with("custom", "qwen");
  local.url = "http://localhost:1234/v1/";
  Fake h;
  h.replies = {status(400), status(422), status(400), ok(openai_reply("Sure! " + quick_json))};
  REQUIRE(ai::ask(query(), local, recall, h.t()).ok());
  REQUIRE(h.calls.size() == 4);
  CHECK(h.calls[0].url == "http://localhost:1234/v1/chat/completions");
  CHECK(h.calls[0].timeout == 120);
  CHECK(!json::parse(h.calls[3].body).contains("response_format"));
  CHECK(recall["json|localhost:1234|qwen"] == 1);
  // A server of one's own needs no key.
  CHECK(!has_header(h.calls[0], "Authorization", "Bearer "));
}

TEST_CASE("ai: errors as InkPeek's AiErrors; nothing is sent without a key, a model or a server") {
  std::map<std::string, int> recall;
  auto code = [&](http::Reply r, const std::string &provider = "openai") {
    Fake f;
    f.replies = {r};
    return ai::ask(query(), with(provider, "m"), recall, f.t());
  };
  CHECK(code(status(401)).code == ai::Code::key);
  CHECK(code(status(403)).code == ai::Code::region);
  CHECK(code(status(400, "User location is not supported for the API use.")).code == ai::Code::region);  // Gemini's 400
  CHECK(code(status(429)).code == ai::Code::rate);
  CHECK(code(status(402)).code == ai::Code::credits);
  CHECK(code(status(500, "Your credit balance is too low")).code == ai::Code::credits);
  CHECK(code({0, "", "Could not resolve host", true}).code == ai::Code::offline);
  CHECK(code({0, "", "Connection refused", false}).code == ai::Code::unreachable);
  CHECK(code(ok(openai_reply("no json here"))).code == ai::Code::not_json);
  auto local = with("custom", "m");
  local.url = "http://localhost:1/v1";
  Fake g;
  g.replies = {{0, "", "Connection refused", false}};
  auto a = ai::ask(query(), local, recall, g.t());
  CHECK(a.code == ai::Code::server);
  set_ui_language("en");
  CHECK(ai::error_text(a, local).starts_with("The server does not answer: http://localhost:1/v1"));
  // Region and failures say what the server said, never the key.
  auto r = code(status(403, "Forbidden"));
  CHECK(ai::error_text(r, with("openai")).find("HTTP 403: Forbidden") != std::string::npos);
  CHECK(ai::error_text(r, with("openai")).find("sk-test") == std::string::npos);

  Fake none;
  auto q = query();
  q.key = "";
  CHECK(ai::ask(q, with("anthropic"), recall, none.t()).code == ai::Code::no_key);
  CHECK(ai::ask(query(), with("openai"), recall, none.t()).code == ai::Code::no_model);
  CHECK(ai::ask(query(), with("custom", "m"), recall, none.t()).code == ai::Code::no_server);
  CHECK(ai::ask(query(), with(""), recall, none.t()).code == ai::Code::off);
  CHECK(none.calls.empty());
  CHECK(ai::error_text({ai::Code::no_key, "", "", false}, {}) == "No API key: add one in Settings → AI.");
}

TEST_CASE("ai: CLI providers — claude -p and codex exec as one shell line (the prompt quoted) or a call (the prompt on stdin)") {
  std::map<std::string, int> recall;
  Fake f;
  f.ran = {0, json{{"type", "result"}, {"is_error", false}, {"result", quick_json}}.dump(), ""};
  auto q = query();
  q.text = "it's";
  auto a = ai::ask(q, with("claude-cli"), recall, f.t());
  REQUIRE(a.ok());
  REQUIRE(f.commands.size() == 1);
  auto &c = f.commands[0];
  CHECK(c.find("'claude' -p --output-format json --setting-sources '' --strict-mcp-config --tools '' --effort low") != std::string::npos);
  CHECK(c.find("MAX_THINKING_TOKENS=0") != std::string::npos);
  CHECK(c.find("«it'\\''s»") != std::string::npos);  // a quote inside the prompt
  CHECK(c.find("--model sonnet") != std::string::npos);  // no model picked: Sonnet
  CHECK(c.find("sk-test") == std::string::npos);
  auto &call = f.calls_cli[0];
  CHECK(call.program == "claude");
  CHECK(call.args == std::vector<std::string>{"-p", "--output-format", "json", "--setting-sources", "", "--strict-mcp-config", "--tools", "",
                                              "--effort", "low", "--no-session-persistence", "--model", "sonnet"});
  CHECK(call.stdin_arg.empty());
  CHECK(call.prompt.find("«it's»") != std::string::npos);

  auto codex = with("codex-cli", "gpt-5");
  codex.commands["codex-cli"] = "/opt/bin/codex";
  Fake g;
  g.ran = {0, quick_json + "\n", "progress on stderr"};
  REQUIRE(ai::ask(query(), codex, recall, g.t()).ok());
  CHECK(g.commands.at(0).find("'/opt/bin/codex' exec --skip-git-repo-check --sandbox read-only -c model_reasoning_effort=minimal -m gpt-5 '") !=
        std::string::npos);
  CHECK(g.calls_cli.at(0).stdin_arg == "-");

  Fake missing;
  missing.ran = {127, "", "claude: command not found"};
  auto m = ai::ask(query(), with("claude-cli"), recall, missing.t());
  CHECK(m.code == ai::Code::no_cli);
  CHECK(ai::error_text(m, with("claude-cli")).find("claude") != std::string::npos);
  Fake bad;
  bad.ran = {1, json{{"type", "result"}, {"is_error", true}, {"result", "Not logged in"}}.dump(), ""};
  auto e = ai::ask(query(), with("claude-cli"), recall, bad.t());
  CHECK(e.code == ai::Code::failed);
  CHECK(e.detail == "Not logged in");
  CHECK(ai::method_title(with("claude-cli")) == "AI · claude");
  CHECK(ai::method_title(with("anthropic")) == "AI · claude-sonnet-5-5");
}

TEST_CASE("ai: answers kept through the store (ai.put), so they come back offline") {
  auto d = fs::temp_directory_path() / "lexipeek-ai-store";
  wipe(d);
  fs::create_directories(d);
  Store st({d, {}, {}});
  REQUIRE(st.error().empty());
  st.seen("Häusern", "word", "ru", "дом", "Haus");  // the card's look: the view keeps answers of looked-up texts
  std::map<std::string, int> recall;
  Fake f;
  f.replies = {ok(claude_reply(quick_json))};
  auto s = with("anthropic");
  auto a = ai::answer(st, query(), s, recall, f.t());
  REQUIRE(a.ok());
  CHECK(!a.saved);
  auto kept = st.answer("Häusern", "Russian", "quick", 1031579369);
  REQUIRE(kept);
  CHECK(json::parse(*kept)["base"] == "Haus");
  CHECK(st.records().ai.at(0).model == "claude-sonnet-5-5");
  // Offline now: the saved one, nothing sent.
  Fake offline;
  offline.replies = {{0, "", "Could not resolve host", true}};
  auto b = ai::answer(st, query(), s, recall, offline.t());
  CHECK(b.ok());
  CHECK(b.saved);
  CHECK(offline.calls.empty());
  // Not saved: offline says so.
  CHECK(ai::answer(st, query(ai::Ask::context), s, recall, offline.t()).code == ai::Code::offline);
  CHECK(ai::brief(*kept) == "das Haus — дом, здание");
  wipe(d);
}

TEST_CASE("ai: the tabs' Markdown under the app's headings; the reply's JSON inside thinking or a sentence") {
  set_ui_language("en");
  auto md = ai::render(ai::Ask::analysis, R"({"grammar":"**Häusern**: dative plural","words":["**Haus** — house","- **Heim** — home"]})", true);
  CHECK(md == "**Grammar**\n**Häusern**: dative plural\n\n**Words**\n- **Haus** — house\n- **Heim** — home");
  auto ctx = ai::render(ai::Ask::context, R"({"explanation":"","examples":[{"text":"Das **Haus** ist alt.","translation":"Дом старый."}]})", true);
  CHECK(ctx == "**Examples**\n- Das **Haus** ist alt.\n*Дом старый.*");
  CHECK(ai::render(ai::Ask::quick, quick_json, true) == "*das Haus · die Häuser · сущ., ср. р. · дат. мн.*\n- дом\n- здание");
  CHECK(ai::render(ai::Ask::quick, R"({"translation":"Я закрываю дверь."})", false) == "Я закрываю дверь.");
  CHECK(ai::json_object(ai::strip_thinking("<think>{\"no\":1}</think>Here: {\"a\": 1} done")) == R"({"a":1})");
  CHECK(ai::json_object("no braces").empty());
  // Fenced, with raw line breaks inside a string (and an escaped quote): read anyway.
  std::string fenced = "```json\n{\"grammar\": \"- a\n- \\\"b\\\"\tc\"}\n```";
  std::string read = R"({"grammar":"- a\n- \"b\"\tc"})";  // MSVC: no raw string inside a macro
  CHECK(ai::json_object(fenced) == read);
}

TEST_CASE("ai: the model list — chat models only, prices, free ones; a filter") {
  Fake f;
  f.replies = {ok(R"({"data":[
    {"id":"openai/gpt-x","name":"GPT X","pricing":{"prompt":"0.000002","completion":"0.000008"},"context_length":128000,
     "architecture":{"output_modalities":["text"]}},
    {"id":"meta/llama:free","pricing":{"prompt":"0","completion":"0"}},
    {"id":"openai/text-embedding-3"},
    {"id":"x/painter","architecture":{"output_modalities":["image"]}}]})")};
  auto m = ai::models(with("openrouter"), "", f.t());
  REQUIRE(m.error.ok());
  REQUIRE(m.list.size() == 2);
  CHECK(f.calls.at(0).url == "https://openrouter.ai/api/v1/models");
  CHECK(m.list[0].id == "meta/llama:free");
  CHECK(m.list[0].free);
  CHECK(m.list[1].publisher == "openai");
  set_ui_language("en");
  CHECK(ai::model_titles(m.list) == std::vector<std::string>{"meta/llama:free  · free", "openai/gpt-x  · $2.00/$8.00  · 128k"});
  CHECK(ai::filter_models(m.list, "GPT", false).size() == 1);
  CHECK(ai::filter_models(m.list, "", true).size() == 1);
  Fake g;
  g.replies = {ok(R"({"data":[{"id":"claude-sonnet-5-5","display_name":"Claude Sonnet 5.5"}]})")};
  auto c = ai::models(with("anthropic"), "sk", g.t());
  CHECK(g.calls.at(0).url == "https://api.anthropic.com/v1/models?limit=1000");
  CHECK(c.list.at(0).name == "Claude Sonnet 5.5");
  CHECK(ai::models(with("anthropic"), "", g.t()).error.code == ai::Code::no_key);
}

TEST_CASE("ai views: the card's AI tabs, asked only when opened; AI translation as a method; Card → Opening") {
  set_ui_language("en");
  Card c;
  c.kind = Card::Kind::phrase;
  c.text = "Ich mache die Tür zu";
  c.lang = "de", c.answer = "ru";
  c.gloss = {{"Tür", "Tür", "дверь"}};
  c.methods = {{Method::fast, "Fast · Mozilla", true, ""}};
  c.method = Method::fast;
  auto st = first_state(c);
  auto v = card_view(c, st);
  CHECK(v.tab_items.size() == 2);  // AI off: Dictionary · Translate
  CHECK(card_asks(c, st).empty());

  c.ai = {true, true, true, true, "AI · claude-sonnet-5-5"};
  v = card_view(c, st);
  REQUIRE(v.tab_items.size() == 4);
  CHECK(v.tab_items[2].title == "Analysis AI");
  CHECK(v.tab_items[3].tab == CardTab::context);
  CHECK(card_asks(c, st).empty());  // Translate offline: nothing asked
  REQUIRE(v.methods.size() == 2);
  CHECK(v.methods[1].ai);
  CHECK(v.methods[1].title == "AI · claude-sonnet-5-5");

  st.tab = CardTab::analysis;
  auto asks = card_asks(c, st);
  REQUIRE(asks.size() == 3);  // the open one first, the others with it
  CHECK(asks[0] == ai::Ask::analysis);
  v = card_view(c, st);
  REQUIRE(v.rows.size() == 1);
  CHECK(v.rows[0].style == CardRow::Style::progress);
  CHECK(v.rows[0].text == "Thinking…");
  CHECK(!v.menu);

  AiState got{false, true, true, false, R"({"grammar":"**mache … zu**: zumachen","words":["**Tür** — дверь"]})", ""};
  st = with_answer(st, ai::Ask::analysis, got);
  st = with_answer(st, ai::Ask::quick, {true, false, false, false, "", ""});
  st = with_answer(st, ai::Ask::context, {false, true, false, false, "", "No network."});
  CHECK(card_asks(c, st).empty());  // asked or answered
  v = card_view(c, st);
  REQUIRE(v.rows.size() == 4);
  CHECK(v.rows[0].style == CardRow::Style::heading);
  CHECK(v.rows[0].text == "Grammar");
  CHECK(v.rows[1].style == CardRow::Style::markdown);
  CHECK(v.rows[3].text == "• **Tür** — дверь");
  // A failed tab asks again when it is opened.
  st.tab = CardTab::context;
  CHECK(card_view(c, st).rows.at(0).style == CardRow::Style::error);
  CHECK(card_asks(c, st) == std::vector<ai::Ask>{ai::Ask::context});

  // AI translation picked in the menu.
  st.tab = CardTab::translate;
  st.by_ai = true;
  st = with_answer(st, ai::Ask::quick, {false, true, true, false, R"({"translation":"Я закрываю дверь."})", ""});
  v = card_view(c, st);
  CHECK(v.menu_title == "AI · claude-sonnet-5-5");
  CHECK(v.rows.at(0).text == "Я закрываю дверь.");
  CHECK(shown_translation(st) == "Я закрываю дверь.");

  // Card → Opening: an AI choice asks at once; one that is off falls back.
  c.prefs.phrase_open = "ai";
  auto s2 = first_state(c);
  CHECK(s2.by_ai);
  CHECK(card_asks(c, s2).front() == ai::Ask::quick);
  c.prefs.phrase_open = "context";
  CHECK(first_state(c).tab == CardTab::context);
  c.ai.context = false;
  CHECK(first_state(c).tab == CardTab::translate);
  // No prefetch: only the open one.
  c.ai.prefetch = false;
  c.ai.context = true;
  CardState s3;
  s3.tab = CardTab::context;
  CHECK(card_asks(c, s3).size() == 1);
}

TEST_CASE("ai settings: Connection by provider, Modules with ✎, prompts edited and reset, no key in settings.json") {
  set_ui_language("en");
  auto d = fs::temp_directory_path() / "lexipeek-ai-settings";
  wipe(d);
  SettingsState st;
  st.answers = {"en", "ru"};
  auto rows = [&](int tab) { return app::settings_page(d.string(), SettingsSection::ai, tab, st).rows; };
  auto find = [](const std::vector<SettingRow> &rs, const std::string &key) -> const SettingRow * {
    for (auto &r : rs)
      if (r.key == key) return &r;
    return nullptr;
  };
  REQUIRE(rows(0).size() == 1);  // off: only the provider
  CHECK(rows(0)[0].choices.at(0) == "Off");
  CHECK(app::choose(d.string(), "ai_provider", 1, st).empty());
  CHECK(app::ai_provider(d.string()) == "anthropic");
  auto r = rows(0);
  REQUIRE(find(r, "ai_key"));
  CHECK(find(r, "ai_key")->kind == SettingRow::Kind::secret);
  CHECK(!find(r, "ai_key")->on);
  REQUIRE(find(r, "ai_model"));
  CHECK(find(r, "ai_model")->placeholder == "claude-sonnet-5-5");
  CHECK(find(r, "ai_model")->on);
  CHECK(find(r, "edit")->edit == "system");
  CHECK(find(r, "ai_test"));
  st.ai_key = true;
  CHECK(find(rows(0), "ai_key")->about == "Kept in the macOS Keychain ✓");
  app::set_text(d.string(), "ai_model", " claude-haiku-5-5 ");
  CHECK(app::settings(d.string()).ai.models.at("anthropic") == "claude-haiku-5-5");

  // The server of one's own: a preset fills the address.
  app::choose(d.string(), "ai_provider", 4, st);
  app::choose(d.string(), "ai_preset", 2, st);
  CHECK(app::settings(d.string()).ai.url == "http://localhost:11434/v1");
  CHECK(find(rows(0), "ai_url")->value == "http://localhost:11434/v1");
  // A CLI: its program, no key, no list.
  app::choose(d.string(), "ai_provider", 5, st);
  r = rows(0);
  CHECK(!find(r, "ai_key"));
  CHECK(find(r, "ai_command")->placeholder == "claude");
  CHECK(!find(r, "ai_model")->on);

  // Modules: the extra parts are off until picked; the module's parts with ✎.
  r = rows(1);
  CHECK(find(r, "ai_translate")->edit == "quick");
  CHECK(find(r, "ai_key.analysis.grammar")->on);
  CHECK(!find(r, "ai_key.analysis.related")->on);
  app::toggle(d.string(), "ai_key.analysis.related", true);
  app::toggle(d.string(), "ai_key.analysis.grammar", true);  // its default: not kept
  CHECK(app::settings(d.string()).ai.picked == std::map<std::string, bool>{{"analysis.related", true}});
  // Card → Opening offers the AI choices now.
  auto open = app::settings_page(d.string(), SettingsSection::card, 0, st).rows.at(0);
  CHECK(open.choices == std::vector<std::string>{"Translation, offline", "Translation, AI", "Dictionary, word by word", "Analysis AI", "Context AI"});
  app::choose(d.string(), "phrase_open", 3, st);
  CHECK(app::settings(d.string()).card.phrase_open == "analysis");

  auto e = app::prompt_editor(d.string(), "quick");
  REQUIRE(e.fields.size() == 9);
  CHECK(e.fields[0].id == "quick.base");
  CHECK(e.fields[0].label == "base");
  app::set_prompts(d.string(), {"quick.base", "quick.pos"}, {"the lemma", e.fields[3].original});
  CHECK(app::settings(d.string()).ai.prompts == std::map<std::string, std::string>{{"quick.base", "the lemma"}});
  CHECK(app::prompt_editor(d.string(), "quick").fields[0].text == "the lemma");
  CHECK(find(rows(1), "ai_translate")->about.starts_with("edited · "));
  CHECK(app::prompt_editor(d.string(), "system").fields.at(0).original == ai::system_prompt());
  app::reset_prompts(d.string());
  CHECK(app::settings(d.string()).ai.prompts.empty());

  std::ifstream f(d / "settings.json");
  auto ai = json::parse(f)["ai"];
  for (auto &[k, v] : ai.items()) CHECK_MESSAGE(k.find("key") == std::string::npos, k);
  wipe(d);
}

TEST_CASE("ai core: the card offers what is on; Core::ask answers from the provider once, then from the store") {
  auto d = fs::temp_directory_path() / "lexipeek-ai-core";
  wipe(d);
  fs::create_directories(d / "replies");
  std::ofstream(d / "replies" / "quick.json") << quick_json;
  CoreOptions o;
  o.data = d;
  o.ai = with("anthropic");
  o.ai_replies = d / "replies";
  Core core(o);
  auto card = core.lookup({"Häusern", Source::input, ""});
  CHECK(card.ai.translate);
  CHECK(card.ai.analysis);
  CHECK(card.ai.method == "AI · claude-sonnet-5-5");
  CHECK(core.ask(card, ai::Ask::quick, "").code == ai::Code::no_key);
  auto a = core.ask(card, ai::Ask::quick, "sk-test");
  REQUIRE(a.ok());
  CHECK(!a.saved);
  std::ofstream(d / "replies" / "status") << "401";
  auto b = core.ask(card, ai::Ask::quick, "sk-test");
  CHECK(b.ok());
  CHECK(b.saved);
  CHECK(core.ask(card, ai::Ask::analysis, "sk-test").code == ai::Code::key);
  // AI off: the card offers nothing of it.
  Settings s;
  core.configure(s);
  auto off = core.lookup({"Häusern", Source::input, ""});
  CHECK(!off.ai.translate);
  CHECK(!off.ai.analysis);
  wipe(d);
}

TEST_CASE("ai: a module with nothing ticked has no tab (the look-up's card, AI → Modules)") {
  set_ui_language("en");
  auto d = std::filesystem::temp_directory_path() / "lexipeek-ai-modules";
  wipe(d);
  std::filesystem::create_directories(d);
  auto tabs = [&](const std::map<std::string, bool> &picked) {
    CoreOptions o;
    o.data = d;
    o.ai.provider = "anthropic";
    o.ai.picked = picked;
    Core core(o);
    std::vector<std::string> out;
    for (auto &t : card_view(core.lookup({"Haus", Source::cli}), {}).tab_items) out.push_back(t.title);
    return out;
  };
  auto all = tabs({});
  CHECK(std::find(all.begin(), all.end(), "Analysis AI") != all.end());
  CHECK(std::find(all.begin(), all.end(), "Context AI") != all.end());
  std::map<std::string, bool> off;
  for (auto &k : ai::keys(ai::Ask::analysis)) off["analysis." + k.name] = false;
  auto no_analysis = tabs(off);
  CHECK(std::find(no_analysis.begin(), no_analysis.end(), "Analysis AI") == no_analysis.end());
  CHECK(std::find(no_analysis.begin(), no_analysis.end(), "Context AI") != no_analysis.end());
  for (auto &k : ai::keys(ai::Ask::context)) off["context." + k.name] = false;
  auto none = tabs(off);
  CHECK(std::find(none.begin(), none.end(), "Context AI") == none.end());
  wipe(d);
}

TEST_CASE("ai: a slow Analysis and a quick Context, Context opened while Analysis is on its way — each in its own tab") {
  set_ui_language("en");
  auto d = std::filesystem::temp_directory_path() / "lexipeek-ai-race";
  wipe(d);
  std::filesystem::create_directories(d);
  {
    std::ofstream(d / "settings.json") << R"({"ai": {"provider": "claude-cli", "prefetch": true, "translate": true}})";
  }
  ai::set_cli_runner([](const ai::CliCall &c) {
    bool analysis = c.prompt.find("grammar") != std::string::npos || c.stdin_arg.find("grammar") != std::string::npos;
    std::this_thread::sleep_for(std::chrono::milliseconds(analysis ? 600 : 50));
    nlohmann::json body = analysis ? nlohmann::json{{"grammar", "ANALYSIS"}, {"words", nlohmann::json::array()}}
                                   : nlohmann::json{{"explanation", "CONTEXT"}, {"examples", nlohmann::json::array()}};
    return ai::CliRan{0, nlohmann::json{{"type", "result"}, {"is_error", false}, {"result", body.dump()}}.dump(), ""};
  });
  auto e = app::Engine::make(d.string(), app::device(16384, {}, "arm64"));
  auto s = app::CardSession::make(e);
  s.look("Haus", Source::input);
  auto settle = [&](int ms) {
    for (int i = 0; i < ms / 10; i++) s.poll(), std::this_thread::sleep_for(std::chrono::milliseconds(10));
    s.poll();
  };
  settle(300);
  REQUIRE(s.has_card());
  s.select(CardTab::analysis);
  settle(100);
  s.select(CardTab::context);
  settle(1500);
  auto st = s.nav().state;
  CHECK(!st.analysis.asking);
  CHECK(st.analysis.json.find("ANALYSIS") != std::string::npos);
  CHECK(st.context.json.find("CONTEXT") != std::string::npos);
  CHECK(!s.busy());
  ai::set_cli_runner(nullptr);
  wipe(d);
}
