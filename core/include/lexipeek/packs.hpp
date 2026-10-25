// InkPeek's packs.json (its docs/formats.md): the components and where they live in the data dir.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lexipeek {

struct Component {
  std::string id, kind, from, to;
  std::string file, sha256;
  std::int64_t size = 0;
  std::vector<std::string> covers;
  std::map<std::string, std::string> tags, names;
  int ram_mb = 0;
  std::vector<std::string> cpu;
  std::string name, prompt, mirror, license, attribution;
  double coverage = 0;
};

struct Manifest {
  std::vector<Component> components;
  std::string error;
};

Manifest parse_manifest(const std::string &json);

// The first component of [kind] whose covers has from-to.
std::optional<Component> covering(const Manifest &m, const std::string &kind, const std::string &from, const std::string &to);

// Install layout under the data dir (as InkPeek's): models/<from><to>/, opus/<id>/, llm/<id>.gguf, langid.ftz, packs.json.
struct Layout {
  std::filesystem::path root;
  std::filesystem::path manifest() const { return root / "packs.json"; }
  std::filesystem::path langid() const { return root / "langid.ftz"; }
  std::filesystem::path model(const std::string &from, const std::string &to) const { return root / "models" / (from + to); }
  std::filesystem::path opus(const std::string &id) const { return root / "opus" / id; }
  std::filesystem::path llm(const std::string &id) const { return root / "llm" / (id + ".gguf"); }
};

// The platform's data dir for LexiPeek; LEXIPEEK_DATA overrides it.
std::filesystem::path default_data_dir();

}  // namespace lexipeek
