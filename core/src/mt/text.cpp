#include "mt/text.hpp"
#include <algorithm>
#include <utf8proc.h>
#include "ssplit.h"

namespace lexipeek::mt {

namespace fs = std::filesystem;

std::vector<std::pair<std::string, std::string>> steps(const std::string &from, const std::string &to) {
  if (from == to) return {};
  if (from == "en" || to == "en") return {{from, to}};
  return {{from, "en"}, {"en", to}};
}

std::string bergamot_config(const fs::path &dir) {
  std::vector<std::string> names;
  std::error_code ec;
  for (auto &e : fs::directory_iterator(dir, ec))
    if (e.is_regular_file()) names.push_back(e.path().filename().string());
  std::sort(names.begin(), names.end());
  auto find = [&](const std::string &prefix) -> std::string {
    for (auto &n : names)
      if (n.starts_with(prefix)) return (dir / n).string();
    return "";
  };
  auto model = find("model.");
  // Mozilla's models have a lexical shortlist; OPUS-MT's have none.
  auto lex = find("lex.");
  auto src = find("srcvocab.");
  if (src.empty()) src = find("vocab.");
  auto trg = find("trgvocab.");
  if (trg.empty()) trg = src;
  if (model.empty() || src.empty()) return "";
  std::string c = "models: [" + model + "]\nvocabs: [" + src + ", " + trg + "]\n";
  if (!lex.empty()) c += "shortlist: [" + lex + ", false]\n";
  c += "beam-size: 1\nnormalize: 1.0\nword-penalty: 0\nmax-length-break: 128\nmini-batch-words: 1024\n"
       "max-length-factor: 2.0\nskip-cost: true\ncpu-threads: 1\nquiet: true\nquiet-translation: true\n";
  c += std::string("gemm-precision: ") + (model.find("alphas") != std::string::npos ? "int8shiftAlphaAll" : "int8shiftAll") + "\n";
  c += "alignment: soft\n";
  return c;
}

std::string trim(const std::string &s) {
  auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::vector<std::string> sentences(const std::string &text) {
  static const ug::ssplit::SentenceSplitter splitter;
  ug::ssplit::SentenceStream in(text, splitter, ug::ssplit::SentenceStream::splitmode::wrapped_text, false);
  std::vector<std::string> out;
  std::string s;
  while (in >> s)
    if (auto t = trim(s); !t.empty()) out.push_back(t);
  return out;
}

size_t utf16_length(const std::string &s) {
  size_t n = 0;
  auto p = reinterpret_cast<const utf8proc_uint8_t *>(s.data());
  utf8proc_ssize_t left = static_cast<utf8proc_ssize_t>(s.size());
  while (left > 0) {
    utf8proc_int32_t cp;
    auto k = utf8proc_iterate(p, left, &cp);
    if (k <= 0) { k = 1; cp = 0xFFFD; }
    n += cp > 0xFFFF ? 2 : 1;
    p += k;
    left -= k;
  }
  return n;
}

std::vector<std::string> chunks(const std::string &text, size_t max) {
  if (utf16_length(text) <= max) return {text};
  std::vector<std::string> out;
  for (auto &s : sentences(text)) {
    if (!out.empty() && utf16_length(out.back()) + 1 + utf16_length(s) <= max) out.back() += " " + s;
    else out.push_back(s);
  }
  return out;
}

std::string fill(std::string prompt, const std::string &from, const std::string &to, const std::string &text) {
  auto replace = [&](const std::string &key, const std::string &value) {
    for (size_t at = 0; (at = prompt.find(key, at)) != std::string::npos; at += value.size()) prompt.replace(at, key.size(), value);
  };
  replace("{from}", from);
  replace("{to}", to);
  replace("{text}", text);
  return prompt;
}

}  // namespace lexipeek::mt
