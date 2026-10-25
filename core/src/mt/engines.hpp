// The engines without JNI (ported from InkPeek's translator_jni.cpp, llm_jni.cpp, langid_jni.cpp). Errors throw.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "lexipeek/mt.hpp"

namespace lexipeek::mt {

// One Bergamot model (a Mozilla direction or an OPUS-MT model) from its Marian options.
class BergamotModel {
 public:
  explicit BergamotModel(const std::string &config);
  ~BergamotModel();
  std::string translate(const std::string &text);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// A GGUF model with its context: greedy decoding of a prompt made by the model's template.
class LlmModel {
 public:
  LlmModel(const std::string &path, int threads);
  ~LlmModel();
  std::string generate(const std::string &prompt, int max_tokens);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// fastText's lid.176: the top languages of a text above a threshold.
class LangIdModel {
 public:
  explicit LangIdModel(const std::string &path);
  ~LangIdModel();
  std::vector<Language> detect(const std::string &text, int k, float threshold);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lexipeek::mt
