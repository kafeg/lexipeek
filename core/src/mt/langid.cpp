// fastText language identification (lid.176), as InkPeek's langid_jni.cpp.
#include <sstream>
#include "fasttext.h"
#include "mt/engines.hpp"

namespace lexipeek::mt {

struct LangIdModel::Impl {
  fasttext::FastText ft;
};

LangIdModel::LangIdModel(const std::string &path) : impl_(std::make_unique<Impl>()) { impl_->ft.loadModel(path); }

LangIdModel::~LangIdModel() = default;

std::vector<Language> LangIdModel::detect(const std::string &text, int k, float threshold) {
  std::string line = text;
  for (auto &c : line)
    if (c == '\n' || c == '\r') c = ' ';
  std::istringstream in(line + "\n");
  std::vector<std::pair<fasttext::real, std::string>> preds;
  impl_->ft.predictLine(in, preds, k, threshold);
  std::vector<Language> out;
  for (auto &[p, label] : preds) out.push_back({label.starts_with("__label__") ? label.substr(9) : label, p});
  return out;
}

}  // namespace lexipeek::mt
