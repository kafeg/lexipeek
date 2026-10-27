// Settings → Other dictionaries (InkPeek's LanguagesSettings.buildOther, OrderActivity, CatalogActivity): rows and edits.
#include <algorithm>
#include <cstdlib>
#include <map>
#include <tuple>
#include "dict/library.hpp"
#include "lexipeek/dicts.hpp"
#include "lexipeek/l10n.hpp"
#include "lexipeek/views.hpp"
#include "text/text.hpp"

namespace lexipeek {

namespace fs = std::filesystem;

namespace {

dict::Library library(const fs::path &data, const DictSettings &s) {
  dict::Own o{{own_dir(data)}, s.off, s.order};
  for (auto &f : s.folders) o.roots.emplace_back(f);
  return dict::Library(data / "dicts", data / "index", o);
}

// "/Users/x/Dictionaries" → "~/Dictionaries".
std::string tilde(const std::string &path) {
  const char *home = std::getenv("HOME");
  if (!home || !*home) return path;
  std::string h = home;
  return path == h ? "~" : path.starts_with(h + "/") ? "~" + path.substr(h.size()) : path;
}

// By path parts, not by text: "/" on macOS and Linux, "\\" on Windows.
bool under(const fs::path &file, const fs::path &root) {
  std::error_code ec;
  auto r = fs::weakly_canonical(root, ec);
  if (r.empty()) return false;
  auto rel = fs::weakly_canonical(file, ec).lexically_relative(r);
  return !rel.empty() && *rel.begin() != ".." && rel != ".";
}

std::string mb(std::int64_t b) {
  if (b <= 0) return "";
  return tr("size_mb", {decimal(static_cast<double>(b) / 1e6)});
}

bool has(const std::vector<std::string> &v, const std::string &x) { return std::find(v.begin(), v.end(), x) != v.end(); }

}  // namespace

DictsView dicts_view(const fs::path &data) {
  auto s = load_settings(data);
  auto lib = library(data, s.dicts);
  DictsView v;
  v.note = tr("dicts_note");
  v.empty = tr("own_none");
  v.import_note = tr("dicts_import_d");
  std::map<std::string, int> count;
  for (auto *d : lib.found()) {
    DictRow r{d->id(), d->name(), "", d->pack(), true, false};
    if (d->pack()) {
      r.title = "InkPeek · " + language_name(d->book()) + " → " + language_name(d->answer());
      r.detail = tr("language_pack");
    } else {
      r.on = !has(s.dicts.off, d->id());
      r.removable = under(d->ifo(), own_dir(data));
      std::vector<std::string> parts;
      if (d->words() > 0) parts.push_back(tr("own_words", {digits(d->words())}));
      if (r.removable) parts.push_back(tr("imported_t"));
      for (auto &f : s.dicts.folders)
        if (under(d->ifo(), f)) {
          parts.push_back(tilde(f));
          count[f]++;
          break;
        }
      for (auto &p : parts) r.detail += (r.detail.empty() ? "" : " · ") + p;
    }
    v.rows.push_back(r);
  }
  for (auto &f : s.dicts.folders) {
    auto n = count[f];
    std::error_code ec;
    v.folders.push_back({f, tilde(f), !fs::is_directory(f, ec) ? tr("folder_missing") : n == 0 ? tr("folder_none") : trn("folder_dicts", n)});
  }
  return v;
}

void set_dict_on(DictSettings &s, const std::string &id, bool on) {
  std::erase(s.off, id);
  if (!on) s.off.push_back(id);
}

void move_dict_to(DictSettings &s, const std::vector<std::string> &shown, const std::string &id, int index) {
  auto ids = shown;
  auto i = std::find(ids.begin(), ids.end(), id);
  if (i == ids.end()) return;
  ids.erase(i);
  ids.insert(ids.begin() + std::clamp<std::ptrdiff_t>(index, 0, static_cast<std::ptrdiff_t>(ids.size())), id);
  for (auto &o : s.order)
    if (!has(ids, o)) ids.push_back(o);
  s.order = ids;
}

void move_dict(DictSettings &s, const std::vector<std::string> &shown, const std::string &id, int by) {
  auto i = std::find(shown.begin(), shown.end(), id);
  if (i == shown.end()) return;
  auto j = (i - shown.begin()) + by;
  if (j < 0 || j >= static_cast<std::ptrdiff_t>(shown.size())) return;
  move_dict_to(s, shown, id, static_cast<int>(j));
}

std::string add_folder(DictSettings &s, const std::string &path, std::string &note) {
  std::error_code ec;
  if (path.empty() || !fs::is_directory(text::path_of(path), ec)) return tr("not_folder", {path});
  auto p = text::utf8_of(fs::weakly_canonical(text::path_of(path), ec));
  if (!has(s.folders, p)) s.folders.push_back(p);
  note = dict::scan(p).empty() ? tr("folder_empty", {tilde(p)}) : "";
  return "";
}

void remove_folder(DictSettings &s, const std::string &path) { std::erase(s.folders, path); }

std::string remove_dict(const fs::path &data, DictSettings &s, const std::string &id) {
  auto lib = library(data, s);
  for (auto *d : lib.found()) {
    if (d->id() != id) continue;
    if (d->pack() || !under(d->ifo(), own_dir(data))) return tr("only_imported");
    auto dir = d->ifo().parent_path();
    auto stem = d->ifo().filename().string();
    stem.resize(stem.size() - 4);
    std::error_code ec;
    for (auto ext : {".ifo", ".idx", ".idx.gz", ".dict", ".dict.dz", ".syn", ".syn.dz"}) fs::remove(dir / (stem + ext), ec);
    if (fs::weakly_canonical(dir, ec) != fs::weakly_canonical(own_dir(data), ec) && fs::is_empty(dir, ec)) fs::remove(dir, ec);
    fs::remove_all(data / "index" / id, ec);
    std::erase(s.off, id);
    std::erase(s.order, id);
    return "";
  }
  return tr("no_dict");
}

std::set<std::string> present_names(const fs::path &data) {
  auto s = load_settings(data);
  auto lib = library(data, s.dicts);
  std::set<std::string> out;
  for (auto *d : lib.found()) out.insert(d->name());
  return out;
}

void set_source_on(DictSettings &s, const std::string &id, bool on) {
  std::erase(s.sources_off, id);
  if (!on) s.sources_off.push_back(id);
}

CatalogView catalog_view(const CatalogList &list, const DictSettings &s, const std::string &answer, const std::string &filter,
                         const std::set<std::string> &present) {
  CatalogView v;
  v.legend = tr("cat_recommended"), v.filter_hint = tr("catalog_filter");
  auto &sources = catalog_sources();
  for (auto &src : sources) v.sources.push_back({src.id, src.title, tr("cat_" + src.id), !has(s.sources_off, src.id)});
  if (std::none_of(v.sources.begin(), v.sources.end(), [](auto &x) { return x.on; })) v.empty = tr("no_sources");
  auto rank = [&](const CatalogItem &i) {
    for (size_t k = 0; k < sources.size(); k++)
      if (sources[k].id == i.source) return k;
    return sources.size();
  };
  auto recommended = [&](const CatalogItem &i) { return i.source == "wikdict" && i.to == answer; };
  // The addresses whose download brought a dictionary that is on disk now.
  std::set<std::string> got;
  for (auto &g : s.got) {
    auto tab = g.find('\t');
    for (size_t at = tab; at != std::string::npos;) {
      auto e = g.find('\t', at + 1);
      if (present.contains(g.substr(at + 1, e == std::string::npos ? std::string::npos : e - at - 1))) got.insert(g.substr(0, tab));
      at = e;
    }
  }
  std::vector<int> order(list.items.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = static_cast<int>(i);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    auto &x = list.items[static_cast<size_t>(a)], &y = list.items[static_cast<size_t>(b)];
    auto kx = std::make_tuple(x.to != answer, !recommended(x), language_name(x.from), rank(x));
    auto ky = std::make_tuple(y.to != answer, !recommended(y), language_name(y.from), rank(y));
    return kx < ky;
  });
  auto f = text::trim(text::lower(filter));
  for (int i : order) {
    auto &it = list.items[static_cast<size_t>(i)];
    auto &src = sources[std::min(rank(it), sources.size() - 1)];
    auto pair = language_name(it.from) + " → " + language_name(it.to);
    if (!f.empty() && text::lower(it.from + "-" + it.to + " " + src.title + " " + pair).find(f) == std::string::npos) continue;
    CatalogRow r{i, (recommended(it) ? "★ " : "") + pair, "", ""};
    for (auto &p : {src.title, mb(it.size), got.contains(it.url) ? "✓ " + tr("got_mark") : std::string()})
      if (!p.empty()) r.detail += (r.detail.empty() ? "" : " · ") + p;
    r.about = tr("cat_" + src.id) + "\n\n" + (it.size > 0 ? mb(it.size) + " · " : "") + it.license;
    v.rows.push_back(r);
  }
  v.info = tr("n_of_m", {digits(static_cast<long long>(v.rows.size())), digits(static_cast<long long>(list.items.size()))});
  return v;
}

}  // namespace lexipeek
