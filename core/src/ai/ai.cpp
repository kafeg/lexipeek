// InkPeek's ai/Ai.kt and Answers.kt: providers, the three requests, least reasoning per host, answers kept in the store.
#include "ai/ai.hpp"
#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>
#include "lexipeek/l10n.hpp"
#include "text/text.hpp"

namespace lexipeek::ai {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct AskDef {
  std::string intro;
  std::vector<Key> keys;
  std::string tail;
  int max_tokens;
};

const std::vector<AskDef> &defs() {
  static const std::vector<AskDef> d{
#include "ai/prompts.inc"
  };
  return d;
}

const AskDef &def(Ask a) { return defs()[static_cast<size_t>(a)]; }

std::string replace_all(std::string s, const std::string &what, const std::string &with) {
  for (auto at = s.find(what); at != std::string::npos; at = s.find(what, at + with.size())) s.replace(at, what.size(), with);
  return s;
}

std::string trim(const std::string &s) {
  auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// At most n bytes, cut at a character's start.
std::string cut(const std::string &s, size_t n) {
  if (s.size() <= n) return s;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
  return s.substr(0, n);
}

std::string host_of(const std::string &url) {
  auto a = url.find("://");
  auto h = a == std::string::npos ? url : url.substr(a + 3);
  return h.substr(0, h.find('/'));
}

std::string base_url(const AiSettings &s) {
  auto *p = provider(s.provider);
  auto u = p && p->id == "custom" ? s.url : p ? p->url : "";
  while (!u.empty() && u.back() == '/') u.pop_back();
  return u;
}

// The server's error message, else the start of its body.
std::string error_message(const std::string &body) {
  try {
    auto j = json::parse(body);
    if (j.contains("error")) {
      auto &e = j["error"];
      if (e.is_object() && e.contains("message") && e["message"].is_string()) return e["message"];
      if (e.is_string()) return e;
    }
  } catch (const std::exception &) {
  }
  return cut(trim(body), 200);
}

bool hit(const text::Regex &r, const std::string &s) {
  text::Regex::Match m;
  return r.find(s, m);
}

Answer fail(Code c, std::string detail = "") { return {c, "", std::move(detail), false}; }

// An HTTP failure as InkPeek's AiErrors sorts it.
Answer http_fail(const http::Reply &r, bool own_server) {
  if (!r.error.empty()) {
    if (own_server) return fail(Code::server, r.error);
    return fail(r.resolve ? Code::offline : Code::unreachable, r.error);
  }
  auto msg = error_message(r.body);
  auto detail = "HTTP " + std::to_string(r.status) + (msg.empty() ? "" : ": " + msg);
  static const text::Regex region("(location|country|region|territory)[^.]{0,40}not supported|unsupported_country", true);
  static const text::Regex credit("credit", true);
  if (r.status == 401) return fail(Code::key, detail);
  if (r.status == 403 || hit(region, msg)) return fail(Code::region, detail);
  if (r.status == 429) return fail(Code::rate, detail);
  if (r.status == 402 || hit(credit, msg)) return fail(Code::credits, detail);
  return fail(Code::failed, detail);
}

json schema(Ask a, const AiSettings &s) {
  json props = json::object(), required = json::array();
  for (auto &k : active(a, s)) {
    required.push_back(k.name);
    if (k.type == "b") props[k.name] = {{"type", "boolean"}};
    else if (k.type == "a") props[k.name] = {{"type", "array"}, {"items", {{"type", "string"}}}};
    else if (k.type == "e")
      props[k.name] = {{"type", "array"},
                       {"items",
                        {{"type", "object"},
                         {"additionalProperties", false},
                         {"required", {"text", "translation"}},
                         {"properties", {{"text", {{"type", "string"}}}, {"translation", {{"type", "string"}}}}}}}};
    else props[k.name] = {{"type", "string"}};
  }
  return {{"type", "object"}, {"additionalProperties", false}, {"required", required}, {"properties", props}};
}

std::string system_text(const AiSettings &s, const std::string &lang) {
  auto i = s.prompts.find("system");
  return replace_all(i == s.prompts.end() ? system_prompt() : i->second, "{lang}", lang);
}

// Claude (Messages API): structured output by the schema, the least thinking the model allows; a 400 asks once more
// without these and is remembered.
Answer claude(const Query &q, const AiSettings &s, const std::string &model, const std::string &prompt, const std::string &sys,
              std::map<std::string, int> &recall, const Transport &t) {
  auto body = [&](bool extras) {
    json b{{"model", model}, {"max_tokens", def(q.ask).max_tokens}, {"system", sys}, {"messages", {{{"role", "user"}, {"content", prompt}}}}};
    if (!extras) return b;
    b["output_config"] = {{"effort", "low"}, {"format", {{"type", "json_schema"}, {"schema", schema(q.ask, s)}}}};
    // Least thinking: off, or "between_tools" (Sonnet 5.5), or only effort low (Opus 5.5, Fable, Mythos).
    static const text::Regex always("^claude-(opus-5-5|fable|mythos)");
    if (model.starts_with("claude-sonnet-5-5")) b["thinking"] = {{"type", "between_tools"}};
    else if (!hit(always, model)) b["thinking"] = {{"type", "disabled"}};
    return b;
  };
  static const std::vector<std::string> fallback{"claude-fable-5-1", "claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5"};
  auto send = [&](bool extras) {
    Call c{"POST", base_url(s) + "/v1/messages", {{"x-api-key", q.key}, {"anthropic-version", "2023-06-01"}, {"content-type", "application/json"}}, "", 45};
    auto b = body(extras);
    if (extras && std::find(fallback.begin(), fallback.end(), model) != fallback.end()) {
      c.headers.emplace_back("anthropic-beta", "server-side-fallback-2026-07-01");
      b["fallbacks"] = "default";
    }
    c.body = b.dump();
    auto r = t.http(c);
    // One retry, as the SDK's: a lost connection or the server's own trouble.
    if ((!r.error.empty() && !r.resolve) || r.status >= 500) r = t.http(c);
    return r;
  };
  auto memo = "claude-json|" + model;
  bool plain = recall.contains(memo) && recall[memo] == 1;
  auto r = send(!plain);
  if (!plain && r.error.empty() && r.status == 400) {
    r = send(false);
    if (r.status >= 200 && r.status < 300) recall[memo] = 1;
  }
  if (!r.error.empty() || r.status < 200 || r.status >= 300) return http_fail(r, false);
  try {
    auto j = json::parse(r.body);
    if (j.value("stop_reason", "") == "refusal") return fail(Code::declined);
    std::string out;
    for (auto &c : j.value("content", json::array()))
      if (c.value("type", "") == "text") out += (out.empty() ? "" : "\n") + c.value("text", "");
    return {Code::ok, out, "", false};
  } catch (const std::exception &) {
    return fail(Code::not_json, cut(r.body, 120));
  }
}

// Fields that switch reasoning off on this server, strongest first (each provider names it differently).
std::vector<json> no_reasoning(const std::string &host) {
  auto has = [&](const char *h) { return host.find(h) != std::string::npos; };
  if (has("openrouter.ai")) return {{{"reasoning", {{"effort", "none"}}}}};
  if (has("api.openai.com")) return {{{"reasoning_effort", "none"}}, {{"reasoning_effort", "low"}}};
  if (has("generativelanguage.googleapis.com")) return {{{"reasoning_effort", "none"}}, {{"reasoning_effort", "minimal"}}};
  if (has("deepseek.com")) return {{{"thinking", {{"type", "disabled"}}}}};
  if (has("dashscope")) return {{{"enable_thinking", false}}};
  if (has("groq.com")) return {{{"reasoning_effort", "none"}}, {{"reasoning_effort", "low"}, {"include_reasoning", false}}};
  return {{{"reasoning_effort", "none"}}};  // Mistral, Ollama, llama.cpp, vLLM, LM Studio
}

// OpenAI-compatible /chat/completions: JSON mode, then without it on 400/422; the host's "no reasoning" field, then a
// weaker one, then none; what worked is remembered per (host, model).
Answer openai(const Query &q, const AiSettings &s, const std::string &model, const std::string &prompt, const std::string &sys,
              std::map<std::string, int> &recall, const Transport &t) {
  bool own = s.provider == "custom";
  auto url = base_url(s);
  json body{{"model", model}, {"messages", {{{"role", "system"}, {"content", sys}}, {{"role", "user"}, {"content", prompt}}}}};
  auto call = [&](const json &b) {
    Call c{"POST", url + "/chat/completions", {{"Content-Type", "application/json"}, {"X-Title", "LexiPeek"}}, b.dump(), own ? 120L : 45L};
    if (!q.key.empty()) c.headers.emplace_back("Authorization", "Bearer " + q.key);
    return t.http(c);
  };
  auto variants = no_reasoning(host_of(url));
  variants.push_back(json::object());
  auto memo = host_of(url) + "|" + model, fmt_memo = "json|" + memo;
  bool mode = !(recall.contains(fmt_memo) && recall[fmt_memo] == 1);
  auto bad = [](const http::Reply &r) { return r.error.empty() && (r.status == 400 || r.status == 422); };
  auto ok = [](const http::Reply &r) { return r.error.empty() && r.status >= 200 && r.status < 300; };
  auto attempt = [&] {
    auto b = body;
    if (mode) b["response_format"] = {{"type", "json_object"}};
    int i = recall.contains(memo) ? std::clamp(recall[memo], 0, static_cast<int>(variants.size()) - 1) : 0;
    auto with = [&](int v) {
      auto x = b;
      x.update(variants[static_cast<size_t>(v)]);
      return call(x);
    };
    auto r = with(i);
    while (bad(r) && i < static_cast<int>(variants.size()) - 1) r = with(++i);
    if (ok(r)) recall[memo] = i;
    return r;
  };
  auto r = attempt();
  if (bad(r) && mode) {
    mode = false;
    r = attempt();
    if (ok(r)) recall[fmt_memo] = 1;
  }
  if (!ok(r)) return http_fail(r, own);
  try {
    auto j = json::parse(r.body);
    if (!j.contains("choices") || j["choices"].empty()) return fail(Code::failed, error_message(r.body));
    auto &m = j["choices"][0]["message"];
    return {Code::ok, m.contains("content") && m["content"].is_string() ? m["content"].get<std::string>() : "", "", false};
  } catch (const std::exception &) {
    return fail(Code::not_json, cut(r.body, 120));
  }
}

std::string command(const AiSettings &s) {
  auto i = s.commands.find(s.provider);
  if (i != s.commands.end() && !trim(i->second).empty()) return trim(i->second);
  return s.provider == "codex-cli" ? "codex" : "claude";
}

// claude -p / codex exec as a child process; the prompt as one argument.
Answer cli(const AiSettings &s, const std::string &prompt, const std::string &sys, const Transport &t) {
  auto r = t.run(cli_command(s, sys, prompt));
  if (r.code == 127) return fail(Code::no_cli, command(s));
  auto out = trim(r.out);
  if (s.provider == "claude-cli") {
    // --output-format json: {"type":"result","is_error":false,"result":"…"}.
    try {
      auto j = json::parse(out);
      if (j.is_object() && j.contains("result")) {
        auto res = j["result"].is_string() ? j["result"].get<std::string>() : j["result"].dump();
        if (j.value("is_error", false) || r.code != 0) return fail(Code::failed, cut(res, 300));
        return {Code::ok, res, "", false};
      }
    } catch (const std::exception &) {
    }
  }
  if (r.code != 0) return fail(Code::failed, cut(trim(r.err.empty() ? r.out : r.err), 300));
  return {Code::ok, out, "", false};
}

const text::Regex &not_chat() {
  static const text::Regex r("embed|whisper|tts|transcri|moderation|guard|safety|dall-e|image|audio|realtime|search|:batch$", true);
  return r;
}

}  // namespace

const std::vector<Provider> &providers() {
  static const std::vector<Provider> p{
      {"anthropic", "Claude (Anthropic)", "https://api.anthropic.com", "claude-sonnet-5-5", true, false, true},
      {"openai", "OpenAI", "https://api.openai.com/v1", "", true, false, true},
      {"openrouter", "OpenRouter", "https://openrouter.ai/api/v1", "openrouter/free", true, false, true},
      {"custom", "", "", "", false, false, true},
      {"claude-cli", "Claude Code (claude -p)", "", "", false, true, false},
      {"codex-cli", "Codex (codex exec)", "", "", false, true, false}};
  return p;
}

const Provider *provider(const std::string &id) {
  for (auto &p : providers())
    if (p.id == id) return &p;
  return nullptr;
}

const std::vector<Preset> &presets() {
  static const std::vector<Preset> p{{"LM Studio", "http://localhost:1234/v1"},
                                     {"Ollama", "http://localhost:11434/v1"},
                                     {"llama.cpp server", "http://localhost:8080/v1"},
                                     {"vLLM", "http://localhost:8000/v1"},
                                     {"Groq", "https://api.groq.com/openai/v1"},
                                     {"DeepSeek", "https://api.deepseek.com/v1"},
                                     {"Qwen (Alibaba Cloud)", "https://dashscope-intl.aliyuncs.com/compatible-mode/v1"},
                                     {"Mistral", "https://api.mistral.ai/v1"},
                                     {"Google Gemini", "https://generativelanguage.googleapis.com/v1beta/openai"}};
  return p;
}

const std::vector<Key> &keys(Ask a) { return def(a).keys; }

std::string section(Ask a) { return a == Ask::quick ? "quick" : a == Ask::analysis ? "analysis" : "context"; }

const std::string &system_prompt() {
  static const std::string s =
      "You are a concise dictionary and language tutor inside a pop-up dictionary on a computer. Write every answer in {lang} "
      "only. If the text is already in {lang}, answer like an explanatory dictionary of {lang} instead of translating. "
      "Formatting: only **bold** and *italic*, short lines, simple \"- \" lists inside JSON string values. No headings, "
      "tables or code. Keep each part under 120 words unless it needs more. You see only the selected text, not the "
      "sentence around it: do not guess a context (a preposition, a speaker) that is not in the text.";
  return s;
}

std::vector<Key> active(Ask a, const AiSettings &s) {
  std::vector<Key> out;
  for (auto &k : keys(a)) {
    auto i = s.picked.find(section(a) + "." + k.name);
    if (i != s.picked.end() ? i->second : !k.extra) out.push_back(k);
  }
  return out;
}

bool module_on(Ask a, const AiSettings &s) { return !active(a, s).empty(); }

std::string prompt(Ask a, const AiSettings &s, bool edited) {
  auto &d = def(a);
  auto out = "Selected text: «{text}». " + d.intro + "\n";
  bool first = true;
  for (auto &k : active(a, s)) {
    auto i = edited ? s.prompts.find(section(a) + "." + k.name) : s.prompts.end();
    out += (first ? "" : "\n") + std::string("- ") + k.name + ": " + (i != s.prompts.end() ? i->second : k.about);
    first = false;
  }
  return out + (d.tail.empty() ? "" : "\n" + d.tail);
}

std::int64_t version(Ask a, const AiSettings &s) { return text::java_hash(prompt(a, s, false)); }

std::string model(const AiSettings &s) {
  auto i = s.models.find(s.provider);
  if (i != s.models.end() && !trim(i->second).empty()) return trim(i->second);
  auto *p = provider(s.provider);
  return p ? p->model : "";
}

std::string method_title(const AiSettings &s) {
  auto m = model(s);
  auto *p = provider(s.provider);
  return tr("ai_method", {!m.empty() ? m : p && p->cli ? command(s) : tr("tab_ai")});
}

std::string strip_thinking(const std::string &s) {
  static const text::Regex think("<think>.*?</think>", false, true);
  return think.replace(s, "");
}

std::string json_object(const std::string &s) {
  auto a = s.find('{');
  auto b = s.rfind('}');
  if (a == std::string::npos || b == std::string::npos || b <= a) return "";
  auto j = json::parse(s.substr(a, b - a + 1), nullptr, false);
  return j.is_object() ? j.dump() : "";
}

std::string shell_quote(const std::string &s) { return "'" + replace_all(s, "'", "'\\''") + "'"; }

std::string cli_command(const AiSettings &s, const std::string &system, const std::string &prompt) {
  auto m = model(s);
  auto text = system + "\n\n" + prompt;
  // An app started from the Finder has a short PATH: the usual install places first. Run in the temp folder, no project.
  std::string cmd = "PATH=\"$HOME/.local/bin:$HOME/.npm-global/bin:/opt/homebrew/bin:/usr/local/bin:$PATH\"; cd " +
                    shell_quote(fs::temp_directory_path().string()) + " && ";
  if (s.provider == "codex-cli")
    return cmd + shell_quote(command(s)) + " exec --skip-git-repo-check --sandbox read-only -c model_reasoning_effort=minimal" +
           (m.empty() ? "" : " -m " + shell_quote(m)) + " " + shell_quote(text);
  return cmd + "MAX_THINKING_TOKENS=0 " + shell_quote(command(s)) + " -p --output-format json" + (m.empty() ? "" : " --model " + shell_quote(m)) +
         " " + shell_quote(text);
}

Answer ask(const Query &q, const AiSettings &s, std::map<std::string, int> &recall, const Transport &t) {
  auto *p = provider(s.provider);
  if (!p) return fail(Code::off);
  if (p->id == "custom" && trim(s.url).empty()) return fail(Code::no_server);
  if (p->key && trim(q.key).empty()) return fail(Code::no_key);
  auto m = model(s);
  if (m.empty() && !p->cli) return fail(Code::no_model);
  auto what = q.words <= 1 && !q.passage ? std::string("a single word")
              : !q.passage                ? std::string("a short phrase")
                                          : "a passage of about " + std::to_string(q.words) + " words";
  auto text = "The selection is " + what + ".\n" + (q.source.empty() ? "" : "The text is probably " + q.source + ".\n") +
              replace_all(replace_all(prompt(q.ask, s), "{text}", q.text), "{lang}", q.lang);
  auto sys = system_text(s, q.lang);
  auto a = p->cli ? cli(s, text, sys, t) : p->id == "anthropic" ? claude(q, s, m, text, sys, recall, t) : openai(q, s, m, text, sys, recall, t);
  if (!a.ok()) return a;
  auto raw = trim(strip_thinking(a.json));
  auto obj = json_object(raw);
  if (obj.empty()) return fail(Code::not_json, cut(raw, 120));
  return {Code::ok, obj, "", false};
}

Answer answer(Store &store, const Query &q, const AiSettings &s, std::map<std::string, int> &recall, const Transport &t) {
  auto ver = version(q.ask, s);
  if (auto saved = store.answer(q.text, q.lang, section(q.ask), ver)) return {Code::ok, *saved, "", true};
  auto a = ask(q, s, recall, t);
  if (a.ok()) store.put_answer(q.text, q.lang, section(q.ask), ver, model(s), a.json);
  return a;
}

Models models(const AiSettings &s, const std::string &key, const Transport &t) {
  Models out;
  auto *p = provider(s.provider);
  if (!p) return out.error = fail(Code::off), out;
  if (p->id == "custom" && trim(s.url).empty()) return out.error = fail(Code::no_server), out;
  if (p->key && p->id != "openrouter" && trim(key).empty()) return out.error = fail(Code::no_key), out;
  if (!p->list) return out;
  Call c{"GET", "", {}, "", 45};
  if (p->id == "anthropic") {
    c.url = base_url(s) + "/v1/models?limit=1000";
    c.headers = {{"x-api-key", key}, {"anthropic-version", "2023-06-01"}};
  } else {
    c.url = base_url(s) + "/models";
    if (!trim(key).empty()) c.headers = {{"Authorization", "Bearer " + key}};
  }
  auto r = t.http(c);
  if (!r.error.empty() || r.status < 200 || r.status >= 300) return out.error = http_fail(r, p->id == "custom"), out;
  try {
    auto j = json::parse(r.body);
    for (auto &m : j.value("data", json::array())) {
      if (!m.contains("id") || !m["id"].is_string()) continue;
      Model x;
      x.id = m["id"];
      x.name = m.contains("display_name") && m["display_name"].is_string() ? m["display_name"].get<std::string>() : m.value("name", x.id);
      // Text-only chat models: no embeddings, speech or image generators.
      if (hit(not_chat(), x.id)) continue;
      if (m.contains("architecture") && m["architecture"].contains("output_modalities")) {
        auto &o = m["architecture"]["output_modalities"];
        if (o.size() != 1 || o[0] != "text") continue;
      }
      auto slash = x.id.find('/');
      x.publisher = slash != std::string::npos ? x.id.substr(0, slash) : m.value("owned_by", "");
      if (m.contains("pricing") && m["pricing"].is_object()) {
        auto price = [&](const char *k) {
          auto &v = m["pricing"][k];
          try {
            return v.is_string() ? std::stod(v.get<std::string>()) * 1e6 : v.is_number() ? v.get<double>() * 1e6 : -1.0;
          } catch (const std::exception &) {
            return -1.0;
          }
        };
        x.in = price("prompt"), x.out = price("completion");
      }
      x.free = x.id.ends_with(":free") || (x.in == 0 && x.out == 0);
      if (m.contains("context_length") && m["context_length"].is_number()) x.context = m["context_length"];
      out.list.push_back(x);
    }
  } catch (const std::exception &) {
    out.error = fail(Code::not_json, cut(r.body, 120));
  }
  std::sort(out.list.begin(), out.list.end(), [](auto &a, auto &b) { return a.id < b.id; });
  return out;
}

std::vector<std::string> model_titles(const std::vector<Model> &list) {
  std::vector<std::string> out;
  for (auto &m : list) {
    auto t = m.id;
    if (m.free) t += "  · " + tr("free");
    else if (m.in >= 0 && m.out >= 0) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "  · $%.2f/$%.2f", m.in, m.out);
      t += buf;
    }
    if (m.context > 0) t += "  · " + std::to_string(m.context / 1000) + "k";
    out.push_back(t);
  }
  return out;
}

std::vector<Model> filter_models(const std::vector<Model> &list, const std::string &filter, bool free_only) {
  auto f = text::lower(trim(filter));
  std::vector<Model> out;
  for (auto &m : list)
    if ((!free_only || m.free) && (f.empty() || text::lower(m.id).find(f) != std::string::npos || text::lower(m.name).find(f) != std::string::npos))
      out.push_back(m);
  return out;
}

std::string error_text(const Answer &a, const AiSettings &s) {
  auto with = [&](const std::string &t) { return a.detail.empty() ? t : t + "\n\n(" + a.detail + ")"; };
  switch (a.code) {
    case Code::ok: return "";
    case Code::off: return tr("ai_off");
    case Code::no_key: return tr("err_no_key");
    case Code::no_model: return tr("err_no_model");
    case Code::no_server: return tr("err_server", {"—"});
    case Code::offline: return tr("err_offline");
    case Code::unreachable: return tr("err_unreachable");
    case Code::server: return with(tr("err_server", {s.url.empty() ? "—" : s.url}));
    case Code::region: return with(tr("err_region"));
    case Code::key: return tr("err_key");
    case Code::rate: return tr("err_rate");
    case Code::credits: return tr("err_credits");
    case Code::declined: return tr("err_declined");
    case Code::not_json: return with(tr("err_not_json"));
    case Code::no_cli: return tr("err_no_cli", {a.detail});
    case Code::failed: return with(tr("err_other"));
  }
  return tr("err_other");
}

std::string render(Ask a, const std::string &answer, bool word) {
  auto j = json::parse(answer, nullptr, false);
  if (!j.is_object()) return "";
  auto str = [&](const json &o, const char *k) { return o.contains(k) && o[k].is_string() ? trim(o[k].get<std::string>()) : std::string(); };
  auto list = [&](const char *k) {
    std::string out;
    if (j.contains(k) && j[k].is_array())
      for (auto &x : j[k]) {
        auto t = trim(x.is_string() ? x.get<std::string>() : x.dump());
        if (t.starts_with("- ")) t = t.substr(2);
        out += (out.empty() ? "- " : "\n- ") + t;
      }
    return out;
  };
  auto pairs = [&](const char *k) {
    std::string out;
    if (j.contains(k) && j[k].is_array())
      for (auto &x : j[k])
        if (x.is_object()) out += (out.empty() ? "- " : "\n- ") + str(x, "text") + "\n*" + str(x, "translation") + "*";
    return out;
  };
  auto sections = [](const std::vector<std::pair<std::string, std::string>> &s) {
    std::string out;
    for (auto &[h, t] : s)
      if (!t.empty()) out += (out.empty() ? "" : "\n\n") + ("**" + h + "**\n") + t;
    return out;
  };
  if (a == Ask::analysis)
    return sections({{tr("sec_grammar"), str(j, "grammar")}, {tr("sec_words"), list("words")}, {tr("ak_related"), list("related")},
                     {tr("ak_synonyms"), list("synonyms")}});
  if (a == Ask::context)
    return sections({{tr("sec_explanation"), str(j, "explanation")}, {tr("sec_examples"), pairs("examples")}, {tr("ak_idioms"), pairs("idioms")},
                     {tr("ak_origin"), str(j, "origin")}});
  // AI translation: its view of the word ("das Haus · die Häuser", the verb's forms, irregular), the translation, meanings.
  std::string facts;
  if (word) {
    auto f = str(j, "forms");
    if (f.empty()) {
      auto art = str(j, "article"), base = str(j, "base"), pl = str(j, "plural");
      f = art + (!art.empty() && !base.empty() ? " " : "") + base + (pl.empty() ? "" : " · " + pl);
    }
    for (auto &x : {f, str(j, "pos"), str(j, "form_of"), j.value("irregular", false) ? tr("irregular") : std::string()})
      if (!x.empty()) facts += (facts.empty() ? "" : " · ") + x;
  }
  std::string out = facts.empty() ? "" : "*" + facts + "*";
  auto add = [&](const std::string &x) {
    if (!x.empty()) out += (out.empty() ? "" : "\n") + x;
  };
  add(str(j, "translation"));
  if (j.contains("meanings") && j["meanings"].is_array())
    for (auto &m : j["meanings"])
      if (m.is_string()) add("- " + trim(m.get<std::string>()));
  return out;
}

std::string brief(const std::string &answer) {
  auto j = json::parse(answer, nullptr, false);
  if (!j.is_object()) return "";
  auto str = [&](const char *k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); };
  auto t = str("translation");
  if (t.empty()) {
    std::string m;
    if (j.contains("meanings") && j["meanings"].is_array())
      for (auto &x : j["meanings"])
        if (x.is_string()) m += (m.empty() ? "" : ", ") + x.get<std::string>();
    auto head = str("article") + (str("article").empty() || str("base").empty() ? "" : " ") + str("base");
    t = head + (!head.empty() && !m.empty() ? " — " : "") + m;
  }
  return cut(t, 280);
}

}  // namespace lexipeek::ai
