// InkPeek's packs.json (its docs/formats.md): the components and where they live in the data dir.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "lexipeek/mt.hpp"

namespace lexipeek {

struct Component {
  std::string id, kind, from, to;
  std::string file, sha256;
  std::int64_t size = 0;
  std::vector<std::string> covers;
  std::map<std::string, std::string> tags, names;
  int ram_mb = 0;
  std::vector<std::string> cpu;  // arm64's
  std::map<std::string, std::vector<std::string>> cpu_by_arch;  // "arm64", "x86_64" → features; missing: cpu on arm64
  std::string name, prompt, mirror, license, attribution;
  double coverage = 0;
};

struct Manifest {
  std::vector<Component> components;
  std::string error;
};

Manifest parse_manifest(const std::string &json);
// A packs.json on disk, parsed once until the file changes (its time and size).
Manifest read_manifest(const std::filesystem::path &file);

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
  std::filesystem::path dict(const std::string &id) const { return root / "dicts" / id; }
  std::filesystem::path installed() const { return root / "installed.json"; }
};

// The platform's data dir for LexiPeek; LEXIPEEK_DATA overrides it.
std::filesystem::path default_data_dir();

// InkPeek's release with the packs, the only source of downloads.
inline constexpr const char *PACKS_BASE = "https://github.com/kafeg/inkpeek/releases/download/packs/";

struct LangPair {
  std::string from, to;
  bool operator==(const LangPair &) const = default;
};

// A pair's label (InkPeek's Packs.level): none = no coverage known.
enum class Level { none, experimental, basic, full };
std::string_view level_name(Level l);
Level level(const Manifest &m, const std::string &from, const std::string &to);

// The pairs offered: one per dictionary.
std::vector<LangPair> pairs(const Manifest &m);
// What the pairs need: dictionaries, Mozilla models through English, langid, and the method's own model per pair.
std::vector<Component> needed(const Manifest &m, const std::vector<LangPair> &ps, Method method = Method::fast);
// What removing [pair] frees: its parts that no other installed pair needs.
std::vector<Component> freed(const Manifest &m, const std::vector<LangPair> &installed, const LangPair &pair, Method method = Method::fast);
// Installed components (id → sha256) whose file in the list differs.
std::vector<Component> updates(const Manifest &m, const std::map<std::string, std::string> &installed);

// What this machine can run; the shell fills it (Apple Silicon has int8 dot product: "asimddp").
// The architecture this build is for: "arm64", "x86_64".
inline std::string cpu_arch() {
#if defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#else
  return "x86_64";
#endif
}
// The CPU's features the packs name, as Linux's /proc/cpuinfo: on x86-64 "avx2", "fma" (cpuid, with the OS saving
// AVX state); elsewhere none (the shell knows: Apple Silicon's "asimddp").
std::vector<std::string> cpu_features();

struct Device {
  int ram_mb = 0;
  std::vector<std::string> features;  // Linux's /proc/cpuinfo names ("asimddp", "avx2", "fma")
  std::string arch = cpu_arch();
  std::string gpu = "";  // the GPU the AI model runs on (llm_gpu()); then its processor needs do not apply
};
// Why [c] cannot run on [d]; "" when it can.
std::string cannot(const Component &c, const Device &d);

struct Fetched {
  Manifest manifest;  // the release's list, or the last one kept when the release cannot be reached
  std::string error;  // why the release's list is not there ("" = fresh)
};

struct Status {
  std::string error;
  bool cancelled = false;
  bool ok() const { return error.empty() && !cancelled; }
};

using Progress = std::function<void(std::int64_t done, std::int64_t total)>;
using Cancel = std::function<bool()>;

// The packs in a data dir (InkPeek's layout): the list, installed.json (id → sha256), install and remove.
// Blocking calls; one install at a time per data dir.
class Packs {
 public:
  explicit Packs(std::filesystem::path data, std::string base = PACKS_BASE);
  Layout layout() const { return {data_}; }

  // The release's list; kept in the data dir for offline use.
  Fetched fetch() const;
  // The last list kept (error set when there is none).
  Manifest cached() const;
  std::map<std::string, std::string> installed() const;

  // Downloads (resumable .part, the mirror first), checks sha256 and free space, unpacks into place, marks installed.
  Status install(const Component &c, const Progress &progress = {}, const Cancel &cancel = {}) const;
  // By id from the kept list.
  Status install(const std::string &id, const Progress &progress = {}, const Cancel &cancel = {}) const;
  // Removes the files and the mark; an id no longer in the list is removed by its name.
  Status remove(const std::string &id) const;

 private:
  std::filesystem::path data_;
  std::string base_;
};

}  // namespace lexipeek
