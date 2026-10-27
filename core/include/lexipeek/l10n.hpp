// The UI's texts in the packs' answer languages: key → text per language (core/src/l10n/<lang>.inc).
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace lexipeek {

// en, ru, de, fr, es, it, pt, pl.
const std::vector<std::string> &ui_languages();
// The app's language: the setting ("" = as the system), else the system's when it is one of them, else English.
std::string ui_language(const std::string &setting, const std::string &system);
// Every text from now on is in this language (one of ui_languages(), else English); for all threads.
void set_ui_language(const std::string &code);
std::string ui_language();

// The OS whose words the texts use: "mac" (the default), "windows". There a key's "<key>@win" text, when it has one,
// replaces it (the Credential Manager, Ctrl, "this computer"); for all threads.
void set_ui_platform(const std::string &platform);
std::string ui_platform();

// The text of key with {1}, {2}… filled in; English when the language lacks it, the key when English does.
std::string tr(std::string_view key, const std::vector<std::string> &args = {});
bool has_text(std::string_view key);
// A count's form (key.one, key.few, key.many, key.other by the language's rule), {n} its digits.
std::string trn(std::string_view key, long long n, const std::vector<std::string> &args = {});
// 12345 → "12,345" (en), "12 345" (ru), "12.345" (de)…
std::string digits(long long n);
// 1.5 → "1.5" (en), "1,5" (ru)…: one decimal.
std::string decimal(double x);

// For the tests: a language's keys and texts, and its plural forms.
std::vector<std::pair<std::string, std::string>> texts(const std::string &lang);
std::vector<std::string> plural_forms(const std::string &lang);
std::string plural_form(const std::string &lang, long long n);
// The keys asked for that English lacks, since the start.
std::vector<std::string> missing_texts();

}  // namespace lexipeek
