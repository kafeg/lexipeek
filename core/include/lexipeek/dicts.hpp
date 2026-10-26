// Other (own) StarDict dictionaries, as InkPeek's: <data>/dicts-own (imported, downloaded) and folders read in place;
// import of archives, the catalog of public sources, on / off and the order with the packs' (Settings → Other dictionaries).
#pragma once
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include "lexipeek/packs.hpp"
#include "lexipeek/settings.hpp"

namespace lexipeek {

// Where imports and downloads go.
std::filesystem::path own_dir(const std::filesystem::path &data);

// What an import brought: the new dictionaries' names, the names of those that were there already; error: none came.
struct Imported {
  std::vector<std::string> added, already;
  std::string error;
};

// StarDict files (.ifo, .idx[.gz], .dict[.dz], .syn[.dz]: a picked one brings its siblings) or archives of them
// (.zip, .tar, .tar.gz, .tgz, .tar.bz2, .tar.xz, .tar.zst) → <target>/<bookname>/; one already there is not added twice.
Imported import_dicts(const std::vector<std::filesystem::path> &files, const std::filesystem::path &target);
// A StarDict file by its name (not an archive).
bool is_dict_file(const std::string &name);

// A downloadable dictionary of a public source; from, to: two-letter codes where one exists.
struct CatalogItem {
  std::string source, from, to, url;
  std::int64_t size = 0;
  std::string license;
};

struct CatalogSource {
  std::string id, title, url, about;
};

// WikDict, Wiktionary (xxyzz), FreeDict (InkPeek's Catalog.SOURCES).
const std::vector<CatalogSource> &catalog_sources();
// A source's listing → its dictionaries (WikDict: the server's file index; the others: JSON); throws on a broken one.
std::vector<CatalogItem> parse_catalog(const std::string &source, const std::string &body);
// "deu" → "de"; a code without a two-letter one stays.
std::string two_letter(const std::string &code);
// The language's English name ("German"), the code when unknown.
std::string catalog_language(const std::string &code);

struct CatalogList {
  std::vector<CatalogItem> items;
  std::vector<std::string> failed;  // the sources whose list did not come ("WikDict: …")
};

// The switched-on sources' lists (network, blocking).
CatalogList fetch_catalog(const DictSettings &s);
// Downloads an item into <data>/tmp, imports it into dicts-own, notes what it brought (its ✓) in the settings.
Imported download_dict(const std::filesystem::path &data, const CatalogItem &item, const Progress &progress = {}, const Cancel &cancel = {});

// Settings → Other dictionaries: every dictionary in one list (packs and others, in the card's order).
struct DictRow {
  std::string id, title, detail;  // "InkPeek · German → Russian" / "Language pack"; "WikDict de-en" / "12,345 words · Imported"
  bool pack = false;
  bool on = true;          // in the card (packs: always)
  bool removable = false;  // imported or downloaded (in dicts-own): ✕ deletes its files
};

struct FolderRow {
  std::string path, title, detail;  // title: the path with ~; detail: "3 dictionaries"
};

struct DictsView {
  std::vector<DictRow> rows;
  std::vector<FolderRow> folders;
  std::string note;  // under the list: what the order means
  std::string empty;  // when there is no own dictionary
  std::string import_note;
};

DictsView dicts_view(const std::filesystem::path &data);
// The settings' edits (the shell saves them, then configures the core).
void set_dict_on(DictSettings &s, const std::string &id, bool on);
// One step up (-1) or down (+1) in the list as shown (ids), or to an index (a drag); the others keep their place after it.
void move_dict(DictSettings &s, const std::vector<std::string> &shown, const std::string &id, int by);
void move_dict_to(DictSettings &s, const std::vector<std::string> &shown, const std::string &id, int index);
// "" or why not ("Not a folder: …"); a folder with no dictionary is added with a note in note.
std::string add_folder(DictSettings &s, const std::string &path, std::string &note);
void remove_folder(DictSettings &s, const std::string &path);
// Deletes an imported dictionary's files (only in dicts-own) and its index; "" or the error.
std::string remove_dict(const std::filesystem::path &data, DictSettings &s, const std::string &id);

struct CatalogRow {
  int item = 0;  // its index in the list
  std::string title, detail;  // "★ German → Russian"; "WikDict · 4.2 MB · ✓ downloaded"
  std::string about;          // the source's about, its size and license (the download's confirmation)
};

struct SourceRow {
  std::string id, title, about;
  bool on = true;
};

struct CatalogView {
  std::vector<CatalogRow> rows;  // into the answer language first, recommended (WikDict) first among them
  std::vector<SourceRow> sources;
  std::string info;    // "12 of 340"
  std::string legend = "★ — recommended: compact translations into the app's language.";
  std::string filter_hint = "Filter: de-ru, en, ru…";
  std::string empty;   // "All sources are off", "Loading lists…" is the shell's
};

// present: the names of the dictionaries on disk now (a download's ✓ only while one it brought is there).
CatalogView catalog_view(const CatalogList &list, const DictSettings &s, const std::string &answer, const std::string &filter,
                         const std::set<std::string> &present);
std::set<std::string> present_names(const std::filesystem::path &data);
void set_source_on(DictSettings &s, const std::string &id, bool on);

}  // namespace lexipeek
