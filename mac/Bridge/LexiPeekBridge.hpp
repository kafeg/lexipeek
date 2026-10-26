// The facade for Swift: a copyable handle on one Core, built from plain strings.
#pragma once
#include <memory>
#include <string>
#include "lexipeek/core.hpp"
#include "lexipeek/packs.hpp"

namespace lexipeek::mac {

struct Engine {
  std::shared_ptr<Core> core;

  // method: "fast", "direct", "llm" or "" for the first available.
  static Engine make(const std::string &data, const std::string &answer, const std::string &method) {
    CoreOptions o;
    o.data = data.empty() ? default_data_dir() : std::filesystem::path(data);
    if (!answer.empty()) o.answer = answer;
    o.method = method_from(method);
    return {std::make_shared<Core>(std::move(o))};
  }
  Card lookup(const std::string &text, Source source) const { return core->lookup({text, source, ""}); }
  Translation translate(const Card &card, Method m) const { return core->translate(card, m); }
  void release() const { core->release(); }
};

inline std::string data_dir() { return default_data_dir().string(); }
inline bool has_method(const Card &c) { return c.method.has_value(); }
inline Method method_of(const Card &c) { return c.method.value_or(Method::fast); }

}  // namespace lexipeek::mac
