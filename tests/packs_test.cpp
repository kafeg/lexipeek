// The pack manager without network: fixture archives built here, a release served by file://.
#include <archive.h>
#include <archive_entry.h>
#include <doctest/doctest.h>
#include "temp.hpp"
#include <sha256.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include "lexipeek/packs.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

using Files = std::vector<std::pair<std::string, std::string>>;

std::string slurp(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

void spit(const fs::path &p, const std::string &text) { std::ofstream(p, std::ios::binary) << text; }

std::string sha(const std::string &bytes) { return SHA256()(bytes); }

std::string url(const fs::path &p) {
  auto g = p.generic_string();
  return "file://" + std::string(g.starts_with("/") ? "" : "/") + g;
}

void archive_to(const fs::path &out, bool zip, const Files &files) {
  auto *a = archive_write_new();
  if (zip) archive_write_set_format_zip(a);
  else {
    archive_write_add_filter_zstd(a);
    archive_write_set_format_pax_restricted(a);
  }
  REQUIRE(archive_write_open_filename(a, out.string().c_str()) == ARCHIVE_OK);
  for (auto &[name, data] : files) {
    auto *e = archive_entry_new();
    archive_entry_set_pathname(e, name.c_str());
    archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_write_header(a, e);
    archive_write_data(a, data.data(), data.size());
    archive_entry_free(e);
  }
  archive_write_close(a);
  archive_write_free(a);
}

// A release folder with packs.json and its files; data: an empty data dir.
struct Release {
  fs::path dir, data;
  json list = {{"format", 1}, {"generated", "2026-10-26"}, {"components", json::array()}};

  explicit Release(const std::string &name) {
    auto root = fs::temp_directory_path() / ("lexipeek-packs-" + name);
    wipe(root);
    dir = root / "release";
    data = root / "data";
    fs::create_directories(dir);
  }
  json &add(json c, const std::string &file) {
    auto bytes = slurp(dir / file);
    c["file"] = {{"name", file}, {"size", bytes.size()}, {"sha256", sha(bytes)}};
    list["components"].push_back(c);
    return list["components"].back();
  }
  void publish() { spit(dir / "packs.json", list.dump()); }
  Packs packs() const { return Packs(data, url(dir)); }
};

Release standard(const std::string &name) {
  Release r(name);
  archive_to(r.dir / "model-de-en.tar.zst", false, {{"deen/set", "1 tiny"}, {"deen/model.deen.bin", "weights"}});
  archive_to(r.dir / "dict-de-ru.zip", true, {{"dict-de-ru/dict-de-ru.ifo", "ifo"}, {"dict-de-ru/dict-de-ru.idx", "idx"}});
  archive_to(r.dir / "opus-x.tar.zst", false, {{"opus-x/model.bin", "opus"}, {"opus-x/LICENSE", "CC BY"}});
  spit(r.dir / "langid.ftz", "fasttext");
  spit(r.dir / "llm-x.gguf", std::string(5000, 'g'));
  r.add({{"id", "model-de-en"}, {"kind", "model"}, {"from", "de"}, {"to", "en"}}, "model-de-en.tar.zst");
  r.add({{"id", "dict-de-ru"}, {"kind", "dict"}, {"from", "de"}, {"to", "ru"}, {"stats", {{"coverage", 90}}}}, "dict-de-ru.zip");
  r.add({{"id", "opus-x"}, {"kind", "opus"}, {"from", ""}, {"to", ""}, {"covers", {"de-ru"}}, {"ram_mb", 4000}}, "opus-x.tar.zst");
  r.add({{"id", "langid"}, {"kind", "langid"}, {"from", ""}, {"to", ""}}, "langid.ftz");
  r.add({{"id", "llm-x"}, {"kind", "llm"}, {"covers", {"de-ru"}}, {"ram_mb", 6000}, {"cpu", {"asimddp"}}, {"mirror", url(r.dir / "gone.gguf")}}, "llm-x.gguf");
  r.publish();
  return r;
}

}  // namespace

TEST_CASE("packs: the list is fetched, kept and read offline") {
  auto r = standard("list");
  auto p = r.packs();
  CHECK(!p.cached().error.empty());
  auto f = p.fetch();
  CHECK(f.error.empty());
  REQUIRE(f.manifest.error.empty());
  CHECK(f.manifest.components.size() == 5);
  CHECK(fs::exists(r.data / "packs.json"));
  auto off = Packs(r.data, url(r.dir / "nowhere")).fetch();
  CHECK(!off.error.empty());
  CHECK(off.manifest.components.size() == 5);
}

TEST_CASE("packs: install puts every kind in InkPeek's layout, flat") {
  auto r = standard("layout");
  auto p = r.packs();
  p.fetch();
  for (auto id : {"model-de-en", "dict-de-ru", "opus-x", "langid", "llm-x"}) CHECK_MESSAGE(p.install(id).ok(), id);
  CHECK(slurp(r.data / "models" / "deen" / "set") == "1 tiny");
  CHECK(fs::exists(r.data / "models" / "deen" / "model.deen.bin"));
  CHECK(!fs::exists(r.data / "models" / "deen" / "deen"));
  CHECK(fs::exists(r.data / "dicts" / "dict-de-ru" / "dict-de-ru.ifo"));
  CHECK(fs::exists(r.data / "opus" / "opus-x" / "LICENSE"));
  CHECK(slurp(r.data / "langid.ftz") == "fasttext");
  CHECK(fs::file_size(r.data / "llm" / "llm-x.gguf") == 5000);  // the mirror is gone: from the release
  CHECK(!fs::exists(r.data / "tmp"));
  for (auto &e : fs::directory_iterator(r.data)) CHECK(e.path().extension() != ".part");
  auto have = p.installed();
  CHECK(have.size() == 5);
  CHECK(have["langid"] == sha("fasttext"));

  // Reinstalling replaces the folder: stale files go.
  spit(r.data / "models" / "deen" / "old.bin", "x");
  CHECK(p.install("model-de-en").ok());
  CHECK(!fs::exists(r.data / "models" / "deen" / "old.bin"));

  CHECK(p.remove("model-de-en").ok());
  CHECK(!fs::exists(r.data / "models" / "deen"));
  CHECK(!p.installed().contains("model-de-en"));
  CHECK(p.remove("llm-x").ok());
  CHECK(!fs::exists(r.data / "llm" / "llm-x.gguf"));
}

TEST_CASE("packs: the mirror is tried first") {
  auto r = standard("mirror");
  spit(r.dir / "gone.gguf", std::string(5000, 'g'));
  fs::remove(r.dir / "llm-x.gguf");
  auto p = r.packs();
  p.fetch();
  CHECK(p.install("llm-x").ok());
}

TEST_CASE("packs: a download resumes from its .part") {
  auto r = standard("resume");
  auto p = r.packs();
  p.fetch();
  fs::create_directories(r.data);
  spit(r.data / "llm-x.gguf.part", std::string(3000, 'g'));
  std::int64_t first = -1;
  auto s = p.install("llm-x", [&](std::int64_t done, std::int64_t total) {
    if (first < 0 && done > 0) first = done;
    CHECK(total == 5000);
  });
  CHECK(s.ok());
  CHECK(first >= 3000);
  CHECK(fs::file_size(r.data / "llm" / "llm-x.gguf") == 5000);

  // A .part of other bytes fails the check and is dropped; the next try starts over.
  p.remove("llm-x");
  spit(r.data / "llm-x.gguf.part", std::string(3000, 'x'));
  CHECK(p.install("llm-x").error.find("Checksum") != std::string::npos);
  CHECK(!fs::exists(r.data / "llm-x.gguf.part"));
  CHECK(p.install("llm-x").ok());
}

TEST_CASE("packs: a bad sha256, a cancel, no space, a bad id install nothing") {
  auto r = standard("bad");
  r.list["components"][3]["file"]["sha256"] = std::string(64, '0');
  r.publish();
  auto p = r.packs();
  p.fetch();
  auto s = p.install("langid");
  CHECK(s.error.find("Checksum mismatch") != std::string::npos);
  CHECK(!fs::exists(r.data / "langid.ftz"));
  CHECK(!fs::exists(r.data / "langid.ftz.part"));
  CHECK(p.installed().empty());

  auto c = p.install("llm-x", {}, [] { return true; });
  CHECK(c.cancelled);
  CHECK(!c.ok());
  CHECK(!fs::exists(r.data / "llm"));

  auto m = p.cached();
  auto big = m.components[0];
  big.size = std::int64_t(1) << 60;
  CHECK(p.install(big).error.find("Not enough free space") != std::string::npos);

  auto evil = m.components[0];
  evil.id = "../evil";
  CHECK(!p.install(evil).ok());
  evil = m.components[0];
  evil.from = "..";
  CHECK(!p.install(evil).ok());
  evil.kind = "grammar";
  evil.from = "de";
  CHECK(p.install(evil).error.find("Unknown pack kind") != std::string::npos);
  CHECK(p.install("nothing").error.find("No pack") != std::string::npos);
}

TEST_CASE("packs: updates, removal of a stale id") {
  auto r = standard("updates");
  auto p = r.packs();
  p.fetch();
  CHECK(p.install("langid").ok());
  CHECK(updates(p.cached(), p.installed()).empty());
  spit(r.data / "installed.json", json{{"langid", "old"}, {"model-fr-en", "x"}}.dump());
  auto u = updates(p.cached(), p.installed());
  REQUIRE(u.size() == 1);
  CHECK(u[0].id == "langid");
  fs::create_directories(r.data / "models" / "fren");
  CHECK(p.remove("model-fr-en").ok());
  CHECK(!fs::exists(r.data / "models" / "fren"));
  CHECK(p.installed().size() == 1);
}

TEST_CASE("packs: levels and what a pair needs") {
  auto r = standard("levels");
  auto m = parse_manifest(r.list.dump());
  REQUIRE(m.error.empty());
  CHECK(level(m, "de", "ru") == Level::basic);  // no en-ru model
  CHECK(level(m, "fr", "ru") == Level::none);
  auto add = [&](json c) {
    c["file"] = {{"name", "x"}, {"size", 1}, {"sha256", "0"}};
    r.list["components"].push_back(c);
  };
  add({{"id", "model-en-ru"}, {"kind", "model"}, {"from", "en"}, {"to", "ru"}});
  add({{"id", "dict-de-en"}, {"kind", "dict"}, {"from", "de"}, {"to", "en"}, {"stats", {{"coverage", 70}}}});
  add({{"id", "dict-fr-ru"}, {"kind", "dict"}, {"from", "fr"}, {"to", "ru"}, {"stats", {{"coverage", 40}}}});
  add({{"id", "dict-it-ru"}, {"kind", "dict"}, {"from", "it"}, {"to", "ru"}});
  m = parse_manifest(r.list.dump());
  CHECK(level(m, "de", "ru") == Level::full);
  CHECK(level(m, "de", "en") == Level::basic);
  CHECK(level(m, "fr", "ru") == Level::experimental);
  CHECK(level(m, "it", "ru") == Level::none);
  CHECK(level_name(Level::full) == "full");
  CHECK(pairs(m).size() == 4);

  auto ids = [](const std::vector<Component> &cs) {
    std::vector<std::string> v;
    for (auto &c : cs) v.push_back(c.id);
    std::sort(v.begin(), v.end());
    return v;
  };
  using V = std::vector<std::string>;
  CHECK(ids(needed(m, {{"de", "ru"}})) == V{"dict-de-ru", "langid", "model-de-en", "model-en-ru"});
  CHECK(ids(needed(m, {{"de", "ru"}}, Method::direct)) == V{"dict-de-ru", "langid", "model-de-en", "model-en-ru", "opus-x"});
  CHECK(ids(needed(m, {{"de", "ru"}}, Method::llm)) == V{"dict-de-ru", "langid", "llm-x", "model-de-en", "model-en-ru"});
  CHECK(ids(needed(m, {})).empty());
  CHECK(ids(freed(m, {{"de", "ru"}, {"de", "en"}}, {"de", "ru"})) == V{"dict-de-ru", "model-en-ru"});
  CHECK(ids(freed(m, {{"de", "ru"}}, {"de", "ru"})) == V{"dict-de-ru", "langid", "model-de-en", "model-en-ru"});
}

TEST_CASE("packs: why a component cannot run on a machine") {
  auto r = standard("cannot");
  auto m = parse_manifest(r.list.dump());
  auto opus = *covering(m, "opus", "de", "ru");
  auto llm = *covering(m, "llm", "de", "ru");
  CHECK(cannot(opus, {16384, {}}).empty());
  CHECK(cannot(opus, {2048, {}}) == "Needs 4 GB of memory; this machine has 2.0 GB.");
  CHECK(cannot(llm, {16384, {"asimd", "asimddp"}}).empty());
  CHECK(cannot(llm, {16384, {"asimd"}}) == "Needs a processor with int8 acceleration (dot product).");
  CHECK(cannot(llm, {4096, {"asimddp"}}).find("memory") != std::string::npos);
  llm.cpu = {"avx2", "asimddp"};
  CHECK(cannot(llm, {16384, {}}) == "Needs processor features: avx2, asimddp.");
}
