// merge, history.
#include <format>
#include <iostream>
#include "command.hpp"
#include "lexipeek/settings.hpp"
#include "lexipeek/store.hpp"

namespace lexipeek::cli {

namespace {

const Register merge{{"merge", "FOLDER   (the merged state of FOLDER/inkpeek-log/*/, as InkPeek's tests/sync/reader/*/expected.json)", {},
                      [](const Args &a) {
                        if (a.rest.size() != 1) return std::cerr << "merge: one FOLDER\n", 2;
                        std::error_code ec;
                        if (!std::filesystem::is_directory(a.rest[0], ec)) return std::cerr << "not a folder: " << a.rest[0] << "\n", 1;
                        std::cout << records_json(merge_folder(a.rest[0])) << "\n";
                        return 0;
                      }}};

const Register history{{"history", "[-n 20]   (the newest looks of DIR's store)", {}, [](const Args &a) {
                          auto sync = load_settings(a.data).sync;
                          Store st({a.data, std::filesystem::path(std::u8string(sync.begin(), sync.end())), {}, {}});
                          st.sync();
                          if (!st.error().empty()) return std::cerr << "error: " << st.error() << "\n", 1;
                          for (auto &h : st.history(std::stoi(a.opt("-n", "20"))))
                            std::cout << std::format("{}\t{}\t{}\t{}\n", h.count, h.text, h.base, h.meaning);
                          return 0;
                        }}};

}  // namespace

}  // namespace lexipeek::cli
