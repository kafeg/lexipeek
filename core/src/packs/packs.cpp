#include <cstdlib>
#include <nlohmann/json.hpp>
#include "lexipeek/packs.hpp"

namespace lexipeek {

using nlohmann::json;

namespace {

std::string str(const json &j, const char *k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }

template <class T>
T get(const json &j, const char *k, T def) {
  return j.contains(k) && !j[k].is_null() ? j[k].get<T>() : def;
}

}  // namespace

Manifest parse_manifest(const std::string &text) {
  Manifest m;
  try {
    auto j = json::parse(text);
    if (get<int>(j, "format", 0) != 1) return {{}, "Unknown packs.json format"};
    for (auto &c : j.at("components")) {
      Component x;
      x.id = str(c, "id");
      x.kind = str(c, "kind");
      x.from = str(c, "from");
      x.to = str(c, "to");
      auto &f = c.at("file");
      x.file = str(f, "name");
      x.sha256 = str(f, "sha256");
      x.size = get<std::int64_t>(f, "size", 0);
      x.covers = get(c, "covers", std::vector<std::string>{});
      x.tags = get(c, "tags", std::map<std::string, std::string>{});
      x.names = get(c, "names", std::map<std::string, std::string>{});
      x.ram_mb = get(c, "ram_mb", 0);
      x.cpu = get(c, "cpu", std::vector<std::string>{});
      x.name = str(c, "name");
      x.prompt = str(c, "template");
      x.mirror = str(c, "mirror");
      x.license = str(c, "license");
      x.attribution = str(c, "attribution");
      if (c.contains("stats") && c["stats"].is_object()) x.coverage = get(c["stats"], "coverage", 0.0);
      m.components.push_back(std::move(x));
    }
  } catch (const std::exception &e) {
    return {{}, std::string("Broken packs.json: ") + e.what()};
  }
  return m;
}

std::optional<Component> covering(const Manifest &m, const std::string &kind, const std::string &from, const std::string &to) {
  const auto pair = from + "-" + to;
  for (auto &c : m.components)
    if (c.kind == kind)
      for (auto &p : c.covers)
        if (p == pair) return c;
  return std::nullopt;
}

std::filesystem::path default_data_dir() {
  auto env = [](const char *k) -> std::filesystem::path {
    const char *v = std::getenv(k);  // NOLINT(concurrency-mt-unsafe)
    return v && *v ? std::filesystem::path(reinterpret_cast<const char8_t *>(v)) : std::filesystem::path();
  };
  if (auto d = env("LEXIPEEK_DATA"); !d.empty()) return d;
#if defined(_WIN32)
  return env("APPDATA") / "LexiPeek";
#elif defined(__APPLE__)
  return env("HOME") / "Library" / "Application Support" / "LexiPeek";
#else
  if (auto x = env("XDG_DATA_HOME"); !x.empty()) return x / "LexiPeek";
  return env("HOME") / ".local" / "share" / "LexiPeek";
#endif
}

}  // namespace lexipeek
