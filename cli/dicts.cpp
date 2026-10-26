// dicts: the own dictionaries (list, import, folders, on / off, remove) and the catalog of public sources.
#include <format>
#include <iostream>
#include "command.hpp"
#include "lexipeek/app.hpp"
#include "lexipeek/dicts.hpp"

namespace lexipeek::cli {

namespace {

int edit(const Args &a, const std::function<std::string(DictSettings &)> &f) {
  auto s = load_settings(a.data);
  auto e = f(s.dicts);
  if (e.empty()) e = save_settings(a.data, s);
  if (!e.empty()) std::cerr << e << "\n";
  return e.empty() ? 0 : 1;
}

int run(const Args &a) {
  auto cmd = a.rest.empty() ? "" : a.rest[0];
  auto arg = a.rest.size() > 1 ? a.rest[1] : "";
  if (cmd == "list") {
    auto v = dicts_view(a.data);
    for (auto &r : v.rows) std::cout << std::format("{} {} {:<40} {}\n", r.id, r.on ? "on " : "off", r.title, r.detail);
    for (auto &f : v.folders) std::cout << "folder " << f.path << "  " << f.detail << "\n";
    if (v.rows.empty()) std::cout << v.empty << "\n";
    return 0;
  }
  if (cmd == "import" && a.rest.size() > 1) {
    std::vector<std::filesystem::path> files(a.rest.begin() + 1, a.rest.end());
    auto r = import_dicts(files, own_dir(a.data));
    std::cout << app::imported_text(r) << "\n";
    return r.error.empty() ? 0 : 1;
  }
  if (cmd == "add-folder" && !arg.empty()) {
    std::string note;
    auto rc = edit(a, [&](DictSettings &d) { return add_folder(d, std::filesystem::absolute(arg).string(), note); });
    if (!note.empty()) std::cerr << note << "\n";
    return rc;
  }
  if (cmd == "remove-folder" && !arg.empty()) return edit(a, [&](DictSettings &d) { return remove_folder(d, arg), std::string(); });
  if ((cmd == "on" || cmd == "off") && !arg.empty()) return edit(a, [&](DictSettings &d) { return set_dict_on(d, arg, cmd == "on"), std::string(); });
  if (cmd == "remove" && !arg.empty()) return edit(a, [&](DictSettings &d) { return remove_dict(a.data, d, arg); });
  if (cmd == "catalog" || cmd == "get") {
    auto s = load_settings(a.data);
    auto list = fetch_catalog(s.dicts);
    for (auto &f : list.failed) std::cerr << f << "\n";
    if (cmd == "catalog") {
      auto v = catalog_view(list, s.dicts, s.answer, arg, present_names(a.data));
      for (auto &r : v.rows) std::cout << std::format("{:<36} {:<40} {}\n", r.title, r.detail, list.items[static_cast<size_t>(r.item)].url);
      std::cout << v.info << "\n";
      return 0;
    }
    CatalogItem item{"", "", "", arg, 0, ""};
    for (auto &i : list.items)
      if (i.url == arg) item = i;
    int last = -1;
    auto r = download_dict(a.data, item, [&](std::int64_t done, std::int64_t total) {
      int pct = total > 0 ? static_cast<int>(done * 100 / total) : 0;
      if (pct != last) std::cerr << "\r  " << (last = pct) << "%" << std::flush;
    });
    std::cerr << "\n";
    std::cout << app::imported_text(r) << "\n";
    return r.error.empty() ? 0 : 1;
  }
  std::cerr << "dicts: list | import <file>... | add-folder <dir> | remove-folder <dir> | on <id> | off <id> | remove <id> | catalog [filter] | get <url>\n";
  return 2;
}

const Register reg{{"dicts", "list | import <file>... | add-folder <dir> | remove-folder <dir> | on|off|remove <id> | catalog [filter] | get <url>", {}, run}};

}  // namespace

}  // namespace lexipeek::cli
