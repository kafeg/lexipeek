// One JSON for the app and the CLI: <data>/settings.json. Unknown fields are kept as they are.
#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "lexipeek/mt.hpp"

namespace lexipeek {

// The default is ⌥D / ⌥A; on Windows (ui_platform) load_settings makes it Win+Shift+D / Win+Shift+A.
struct Hotkey {
  std::string key = "D";  // a letter or a digit
  bool option = true, command = false, control = false, shift = false;
  bool operator==(const Hotkey &) const = default;
};

// Other (own) dictionaries, as InkPeek's Prefs: their order with the packs', which are off, folders read in place.
struct DictSettings {
  std::vector<std::string> order;        // dictionary ids, packs and others together (Card → Article)
  std::vector<std::string> off;          // own dictionaries switched off
  std::vector<std::string> folders;      // folders added by the person (KOReader's, GoldenDict's), read in place
  std::vector<std::string> sources_off;  // catalog sources not listed
  std::vector<std::string> got;          // "<url>\t<name>\t…": what a catalog download brought (its ✓)
};

// The card's settings (InkPeek's Card section): Opening, Article, Appearance.
struct CardPrefs {
  std::string phrase_open = "offline";  // a phrase opens on: "offline" (Translate) or "dict" (word by word)
  bool full = false;                    // a word's dictionaries as full entries, not short lines
  bool grammar = true;                  // the header's grammar from the packs: "das Haus (Hauses, pl. Häuser)"
  std::vector<std::string> sections{"rel", "syn", "expr", "forms"};  // .rel sections shown (InkPeek: origin off)
  int width = 400;      // points (a passage: 60 more)
  int text = 13;        // points
  std::string font;     // "" the system's, "serif", "rounded", "mono"
  int height = 45;      // % of the screen at most
  bool history = true;  // what is opened goes to History (InkPeek's Card → Opening → History); off: nothing is written
};

// The optional AI layer (InkPeek's AI → Connection · Modules). The API key is never here: the shell keeps it (Keychain).
struct AiSettings {
  std::string provider;                         // "" off, "anthropic", "openai", "openrouter", "custom", "claude-cli", "codex-cli"
  std::map<std::string, std::string> models;    // per provider; none: its default
  std::string url;                              // the OpenAI-compatible server's address
  std::map<std::string, std::string> commands;  // a CLI provider's program; none: "claude", "codex"
  bool translate = true;                        // AI translation as a Translate method
  bool prefetch = true;                         // opening one AI tab asks the others too
  std::map<std::string, std::string> prompts;   // edited prompt parts: "system", "analysis.grammar"
  std::map<std::string, bool> picked;           // answer keys switched from their default: "analysis.related"
  std::map<std::string, int> recall;            // which least-reasoning variant a (host, model) accepts
};

struct Settings {
  std::string answer = "ru";     // the answer language
  std::optional<Method> method = Method::direct;  // OPUS-MT (desktop: memory allows it); none: the first available
  bool mt_off = false;           // no offline translation: dictionaries only, Translate is AI only (InkPeek's "off")
  Hotkey lookup{"D"}, input{"A"};
  bool open_at_login = false;
  bool selection_icon = false;  // a small icon next to a new selection opens the card
  bool pin_moved = false;       // a dragged card stays open, as pinned
  bool mt_english = false;      // Mozilla's English in between, under a translation through English
  bool first_run = false;       // the first-run window was finished once
  int keep_model = 5;           // minutes a model stays in memory after a use, 0: until quit
  std::vector<std::string> ignore_apps;  // bundle ids where the hotkey does nothing
  DictSettings dicts;
  CardPrefs card;
  std::string ui_lang;                  // the app's language, "" = the system's
  std::string theme;                    // "" as the system, "light", "dark": the card and every window
  AiSettings ai;
  std::map<std::string, std::vector<std::string>> study;  // languages you learn per answer language: "ru" → de, en (two at most)
};

// Missing or broken file: the defaults.
Settings load_settings(const std::filesystem::path &data);
// "" or the error. Written whole, then renamed.
std::string save_settings(const std::filesystem::path &data, const Settings &s);
// Load, change, save as one step (one lock for every thread: an AI answer's recall and a click in Settings).
std::string update_settings(const std::filesystem::path &data, const std::function<void(Settings &)> &change);

}  // namespace lexipeek
