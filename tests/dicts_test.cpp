// Other (own) dictionaries: short meaning, library order and on / off, import of archives, the catalog — small fixtures made here.
#include <archive.h>
#include <archive_entry.h>
#include <doctest/doctest.h>
#include "temp.hpp"
#include <fstream>
#include "dict/library.hpp"
#include "lexipeek/app.hpp"
#include "lexipeek/core.hpp"
#include "lexipeek/dicts.hpp"
#include "lexipeek/views.hpp"

using namespace lexipeek;
namespace fs = std::filesystem;

namespace {

using Words = std::vector<std::pair<std::string, std::string>>;

void be32(std::string &s, uint32_t v) {
  for (int i = 3; i >= 0; i--) s += static_cast<char>((v >> (i * 8)) & 0xff);
}

// A StarDict dictionary <dir>/<base>.{ifo,idx,dict}, sorted as given.
void make_dict(const fs::path &dir, const std::string &base, const std::string &name, const Words &words) {
  fs::create_directories(dir);
  std::string idx, dict;
  for (auto &[w, a] : words) {
    idx += w + '\0';
    be32(idx, static_cast<uint32_t>(dict.size()));
    be32(idx, static_cast<uint32_t>(a.size()));
    dict += a;
  }
  std::ofstream(dir / (base + ".idx"), std::ios::binary) << idx;
  std::ofstream(dir / (base + ".dict"), std::ios::binary) << dict;
  std::ofstream(dir / (base + ".ifo")) << "StarDict's dict ifo file\nversion=3.0.0\nbookname=" << name << "\nwordcount=" << words.size()
                                       << "\nidxfilesize=" << idx.size() << "\nsametypesequence=h\n";
}

std::string slurp(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

fs::path fresh(const std::string &name) {
  auto d = fs::temp_directory_path() / ("lexipeek-dicts-" + name);
  wipe(d);
  fs::create_directories(d);
  return d;
}

// A data dir with the de-ru pack (Haus) and an own WikDict-like de-en dictionary (Haus, Baum).
fs::path library_data(const std::string &name) {
  auto d = fresh(name);
  make_dict(d / "dicts" / "dict-de-ru", "dict-de-ru", "de-ru",
            {{"Haus", "<p class=\"head\">das Haus (Hauses, pl. Häuser) — дом</p>"}, {"und", "<p class=\"head\">und — и</p>"}});
  make_dict(d / "dicts-own" / "WikDict de-en", "de-en", "WikDict de-en",
            {{"Baum", "<span class=\"grammar\">noun, masculine</span><div>tree</div>"},
             {"Haus", "<span class=\"grammar\">noun, neutral</span><div>house</div><div>home</div><div>house</div>"}});
  return d;
}

// files: archive path → bytes; filter: "gzip", "bzip2", "xz", "zstd" for a tar, "" for a zip.
void archive_to(const fs::path &out, const std::string &filter, const std::vector<std::pair<std::string, std::string>> &files) {
  auto *a = archive_write_new();
  if (filter.empty()) archive_write_set_format_zip(a);
  else {
    if (filter == "gzip") archive_write_add_filter_gzip(a);
    if (filter == "bzip2") archive_write_add_filter_bzip2(a);
    if (filter == "xz") archive_write_add_filter_xz(a);
    if (filter == "zstd") archive_write_add_filter_zstd(a);
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

// A small dictionary's files, read back for an archive under prefix.
std::vector<std::pair<std::string, std::string>> dict_files(const std::string &prefix, const std::string &base, const std::string &name) {
  auto tmp = fresh("src-" + base);
  make_dict(tmp, base, name, {{"Haus", "<b>house</b>"}});
  std::vector<std::pair<std::string, std::string>> out;
  for (auto ext : {".ifo", ".idx", ".dict"}) out.emplace_back(prefix + base + ext, slurp(tmp / (base + ext)));
  return out;
}

std::string url(const fs::path &p) { return "file://" + p.generic_string(); }

std::vector<std::string> ids(const DictsView &v) {
  std::vector<std::string> out;
  for (auto &r : v.rows) out.push_back(r.id);
  return out;
}

}  // namespace

TEST_CASE("short meaning of other dictionaries (InkPeek's Short.kt)") {
  // WikDict: the <div> translations of the first part of speech (or the one asked), distinct, up to 3.
  auto wik = "<span class=\"grammar\">noun, neutral</span><div>house</div><div>home</div><div>house</div><div>building</div><div>hut</div>"
             "<span class=\"grammar\">verb</span><div>to dwell</div>";
  CHECK(dict::short_meaning(wik, "Haus") == "house, home, building");
  CHECK(dict::short_meaning(wik, "Haus", "verb") == "to dwell");
  // Wiktionary: the first sense of the first <ol>, up to ";"; the <ul> before it is not a sense.
  CHECK(dict::short_meaning("<ul><li>IPA: /haʊs/</li></ul><ol><li>A building for living in; a home.<ol><li>sub</li></ol></li></ol>", "house") ==
        "A building for living in");
  // Mueller: [transcription], _n., 1> marks out.
  CHECK(dict::short_meaning("house<br>[haʊs] _n. 1> дом; жилище<br>2> семья", "house") == "дом");
  CHECK(dict::short_meaning("<b>Haus</b><br>Noun<br>(n.) a building", "Haus") == "a building");
  CHECK(dict::short_meaning("", "Haus").empty());
  auto h = dict::head({"Haus", "<div>no grammar</div>1. house"});
  REQUIRE(h);
  CHECK(h->line == "Haus — house");
  CHECK(dict::head({"Haus", "<p class=\"head\">das Haus — дом</p>"})->line == "das Haus — дом");
}

TEST_CASE("own dictionaries in the card: after the packs, their first meaning, order and on / off") {
  auto d = library_data("card");
  CoreOptions o;
  o.data = d;
  {
    Core core(o);
    auto c = core.lookup({"Haus", Source::cli, "ru"});
    REQUIRE(c.entries.size() == 2);
    CHECK(c.entries[0].dictionary == "InkPeek · de → ru");
    CHECK(c.entries[1].dictionary == "WikDict de-en");
    CHECK(c.entries[1].line == "Haus — house, home");
    CHECK(c.entries[1].articles.empty());
    CHECK(c.entries[1].article.find("<div>house</div>") != std::string::npos);
    // Only the own one knows Baum.
    auto b = core.lookup({"Baum", Source::cli, "ru"});
    REQUIRE(b.entries.size() == 1);
    CHECK(b.entries[0].line == "Baum — tree");
    // A phrase's gloss takes an own dictionary's first meaning.
    auto p = core.lookup({"Baum und Baum", Source::cli, "ru"});
    REQUIRE(!p.gloss.empty());
    CHECK(p.gloss[0].meaning == "tree");
  }
  auto v = dicts_view(d);
  REQUIRE(v.rows.size() == 2);
  CHECK(v.rows[0].pack);
  CHECK(v.rows[0].title == "InkPeek · German → Russian");
  CHECK(v.rows[1].title == "WikDict de-en");
  CHECK(v.rows[1].detail == "2 words · Imported");
  CHECK(v.rows[1].removable);
  auto s = load_settings(d);
  move_dict(s.dicts, ids(v), v.rows[1].id, -1);
  CHECK(s.dicts.order == std::vector<std::string>{v.rows[1].id, v.rows[0].id});
  save_settings(d, s);
  CHECK(dicts_view(d).rows[0].id == v.rows[1].id);
  {
    Core core(o);
    core.configure(load_settings(d));
    auto c = core.lookup({"Haus", Source::cli, "ru"});
    REQUIRE(c.entries.size() == 2);
    CHECK(c.entries[0].dictionary == "WikDict de-en");
    set_dict_on(s.dicts, v.rows[1].id, false);
    core.configure(s);
    c = core.lookup({"Haus", Source::cli, "ru"});
    REQUIRE(c.entries.size() == 1);
    CHECK(c.entries[0].dictionary == "InkPeek · de → ru");
  }
  save_settings(d, s);
  CHECK(!dicts_view(d).rows[0].on);
  CHECK(load_settings(d).dicts.off == std::vector<std::string>{v.rows[1].id});
  // A drag to the end; the ids not shown keep their place after it.
  s.dicts.order.push_back("gone");
  move_dict_to(s.dicts, ids(dicts_view(d)), v.rows[1].id, 5);
  CHECK(s.dicts.order == std::vector<std::string>{v.rows[0].id, v.rows[1].id, "gone"});
  // Delete: an imported one only; its files and order go.
  CHECK(!remove_dict(d, s.dicts, v.rows[0].id).empty());
  CHECK(remove_dict(d, s.dicts, v.rows[1].id).empty());
  CHECK(!fs::exists(d / "dicts-own" / "WikDict de-en"));
  CHECK(s.dicts.off.empty());
  CHECK(s.dicts.order == std::vector<std::string>{v.rows[0].id, "gone"});
}

TEST_CASE("a folder read in place") {
  auto d = library_data("folder");
  auto koreader = fresh("koreader");
  make_dict(koreader / "Mueller", "mueller", "Mueller en-ru", {{"house", "[haʊs] _n. 1> дом; жилище"}});
  auto s = load_settings(d);
  std::string note;
  CHECK(!add_folder(s.dicts, (koreader / "missing").string(), note).empty());
  CHECK(add_folder(s.dicts, koreader.string(), note).empty());
  CHECK(note.empty());
  CHECK(add_folder(s.dicts, koreader.string(), note).empty());
  REQUIRE(s.dicts.folders.size() == 1);
  auto empty = fresh("empty-folder");
  CHECK(add_folder(s.dicts, empty.string(), note).empty());
  CHECK(note.starts_with("No StarDict dictionaries in"));
  save_settings(d, s);
  auto v = dicts_view(d);
  REQUIRE(v.rows.size() == 3);
  CHECK(v.rows[1].title == "Mueller en-ru");
  CHECK(!v.rows[1].removable);
  CHECK(v.rows[1].detail.starts_with("1 words · "));
  REQUIRE(v.folders.size() == 2);
  CHECK(v.folders[0].detail == "1 dictionary");
  CHECK(v.folders[1].detail == "No StarDict dictionaries yet");
  CHECK(!remove_dict(d, s.dicts, v.rows[1].id).empty());
  CHECK(fs::exists(koreader / "Mueller" / "mueller.ifo"));
  CoreOptions o;
  o.data = d;
  o.dicts = s.dicts;
  Core core(o);
  auto c = core.lookup({"house", Source::cli, "ru"});
  REQUIRE(c.entries.size() == 1);
  CHECK(c.entries[0].line == "house — дом");
  remove_folder(s.dicts, s.dicts.folders[0]);
  core.configure(s);
  CHECK(core.lookup({"house", Source::cli, "ru"}).entries.empty());
}

TEST_CASE("import: archives and loose files into dicts-own, each once") {
  auto d = fresh("import");
  auto src = fresh("import-src");
  archive_to(src / "a.zip", "", dict_files("deep/folder/", "zipdict", "Zip Dict"));
  archive_to(src / "b.tar.gz", "gzip", dict_files("", "gzdict", "Gz Dict"));
  archive_to(src / "c.tar.bz2", "bzip2", dict_files("x/", "bzdict", "Bz/Dict"));
  archive_to(src / "d.tar.xz", "xz", dict_files("", "xzdict", "Xz Dict"));
  archive_to(src / "e.tar.zst", "zstd", dict_files("", "zstdict", "Zst Dict"));
  auto target = own_dir(d);
  for (auto f : {"a.zip", "b.tar.gz", "c.tar.bz2", "d.tar.xz", "e.tar.zst"}) {
    auto r = import_dicts({src / f}, target);
    CHECK_MESSAGE(r.error.empty(), f, ": ", r.error);
    CHECK(r.added.size() == 1);
  }
  CHECK(fs::exists(target / "Zip Dict" / "zipdict.ifo"));
  CHECK(fs::exists(target / "Zip Dict" / "zipdict.dict"));
  CHECK(fs::exists(target / "Bz_Dict" / "bzdict.idx"));
  // Again: already there, nothing added.
  auto again = import_dicts({src / "a.zip"}, target);
  CHECK(again.added.empty());
  CHECK(again.already == std::vector<std::string>{"Zip Dict"});
  CHECK(app::imported_text(again) == "Already there: Zip Dict.");
  // A picked .ifo brings its siblings.
  make_dict(src / "loose", "loose", "Loose Dict", {{"Haus", "house"}});
  auto l = import_dicts({src / "loose" / "loose.ifo"}, target);
  CHECK(l.added == std::vector<std::string>{"Loose Dict"});
  CHECK(fs::exists(target / "Loose Dict" / "loose.dict"));
  // The same name, other words: a second folder.
  make_dict(src / "loose2", "loose", "Loose Dict", {{"Baum", "tree"}, {"Haus", "house"}});
  CHECK(import_dicts({src / "loose2" / "loose.idx"}, target).added.size() == 1);
  CHECK(fs::exists(target / "Loose Dict-2" / "loose.ifo"));
  // Not a dictionary, an archive without one, a name leaving the folder.
  std::ofstream(src / "notes.txt") << "x";
  CHECK(!import_dicts({src / "notes.txt"}, target).error.empty());
  archive_to(src / "empty.zip", "", {{"readme.txt", "x"}});
  CHECK(import_dicts({src / "empty.zip"}, target).error.starts_with("No StarDict dictionary"));
  archive_to(src / "evil.zip", "", {{"../evil.ifo", "bookname=evil\n"}});
  CHECK(!import_dicts({src / "evil.zip"}, target).error.empty());
  CHECK(!fs::exists(d / "evil.ifo"));
  CHECK((!fs::exists(d / "tmp") || fs::is_empty(d / "tmp")));
  // The imported ones are in the library.
  CHECK(dicts_view(d).rows.size() == 7);
}

TEST_CASE("catalog: the sources' listings") {
  auto w = parse_catalog("wikdict",
                         "<a href=\"wikdict-de-en.zip\">wikdict-de-en.zip</a>   2024-01-01 10:00  4.2M\n"
                         "<a href=\"wikdict-de-ru.zip\">wikdict-de-ru.zip</a>   2024-01-01 10:00  552K\r\n"
                         "<a href=\"other.zip\">other.zip</a>  1M\n");
  REQUIRE(w.size() == 2);
  CHECK(w[0].from == "de");
  CHECK(w[0].to == "en");
  CHECK(w[0].size == 4200000);
  CHECK(w[0].url == "https://download.wikdict.com/dictionaries/stardict/wikdict-de-en.zip");
  CHECK(w[1].size == 552000);
  auto k = parse_catalog("wiktionary", R"({"assets":[{"name":"de-ru.tar.zst","browser_download_url":"https://x/de-ru.tar.zst","size":7},
                                        {"name":"de-ru.zip","browser_download_url":"https://x/de-ru.zip","size":1}]})");
  REQUIRE(k.size() == 1);
  CHECK(k[0].to == "ru");
  CHECK(k[0].size == 7);
  auto f = parse_catalog("freedict", R"([{"name":"deu-eng","releases":[{"platform":"src","URL":"s"},{"platform":"stardict","URL":"https://x/deu-eng.tar.xz","size":"123"}]},
                                       {"name":"kha-eng","releases":[{"platform":"stardict","URL":"u","size":"x"}]},{"name":"nothing"}])");
  REQUIRE(f.size() == 2);
  CHECK(f[0].from == "de");
  CHECK(f[0].to == "en");
  CHECK(f[0].size == 123);
  CHECK(f[1].from == "kha");
  CHECK_THROWS(parse_catalog("wiktionary", "not json"));
  CHECK(two_letter("zho") == "zh");
  CHECK(catalog_language("de") == "German");
  CHECK(catalog_language("kha") == "kha");
}

TEST_CASE("catalog: rows into the answer language first, WikDict recommended, a filter, ✓ while its dictionary is there") {
  CatalogList l;
  l.items = {{"freedict", "de", "ru", "u1", 0, "GPL"}, {"wiktionary", "de", "ru", "u2", 2000000, "CC"}, {"wikdict", "fr", "ru", "u3", 1000000, "CC"},
             {"wikdict", "de", "en", "u4", 1000000, "CC"}, {"wikdict", "de", "ru", "u5", 1000000, "CC"}};
  DictSettings s;
  s.got = {"u2\tWiktionary de-ru", "u3\tGone"};
  auto v = catalog_view(l, s, "ru", "", {"Wiktionary de-ru"});
  REQUIRE(v.rows.size() == 5);
  CHECK(v.rows[0].title == "★ French → Russian");
  CHECK(v.rows[1].title == "★ German → Russian");
  CHECK(v.rows[1].item == 4);
  CHECK(v.rows[2].detail == "Wiktionary (xxyzz) · 2.0 MB · ✓ downloaded");
  CHECK(v.rows[3].detail == "FreeDict");
  CHECK(v.rows[4].title == "German → English");
  CHECK(v.info == "5 of 5");
  CHECK(v.rows[0].about.ends_with("1.0 MB · CC"));
  CHECK(catalog_view(l, s, "ru", " de-ru ", {}).rows.size() == 3);
  CHECK(catalog_view(l, s, "ru", "french", {}).rows.size() == 1);
  set_source_on(s, "wikdict", false);
  set_source_on(s, "wiktionary", false);
  set_source_on(s, "freedict", false);
  CHECK(catalog_view({}, s, "ru", "", {}).empty == "All sources are off");
  CHECK(fetch_catalog(s).items.empty());
}

TEST_CASE("catalog: a download is imported and marked") {
  auto d = fresh("download");
  auto src = fresh("download-src");
  archive_to(src / "wikdict-de-en.zip", "", dict_files("", "de-en", "WikDict de-en"));
  CatalogList l;
  l.items = {{"wikdict", "de", "en", url(src / "wikdict-de-en.zip"), static_cast<std::int64_t>(fs::file_size(src / "wikdict-de-en.zip")), "CC"}};
  app::Download dl;
  auto r = app::download_dict(d.string(), l, 0, dl);
  CHECK(r.error.empty());
  CHECK(r.added == std::vector<std::string>{"WikDict de-en"});
  CHECK(dl.get_done() == dl.get_total());
  CHECK(fs::exists(own_dir(d) / "WikDict de-en" / "de-en.ifo"));
  CHECK(load_settings(d).dicts.got.size() == 1);
  CHECK(app::catalog_view(d.string(), l, "").rows[0].detail.ends_with("✓ downloaded"));
  CHECK(app::download_dict(d.string(), l, 0, dl).already.size() == 1);
  CHECK(load_settings(d).dicts.got.size() == 1);
  l.items[0].url = url(src / "missing.zip");
  CHECK(!app::download_dict(d.string(), l, 0, dl).error.empty());
  CHECK(!app::download_dict(d.string(), l, 3, dl).error.empty());
}

TEST_CASE("dictionary settings round trip") {
  auto d = fresh("settings");
  Settings s;
  s.dicts = {{"a", "b"}, {"b"}, {"/x"}, {"freedict"}, {"u\tn"}};
  save_settings(d, s);
  auto t = load_settings(d);
  CHECK(t.dicts.order == s.dicts.order);
  CHECK(t.dicts.off == s.dicts.off);
  CHECK(t.dicts.folders == s.dicts.folders);
  CHECK(t.dicts.sources_off == s.dicts.sources_off);
  CHECK(t.dicts.got == s.dicts.got);
}
