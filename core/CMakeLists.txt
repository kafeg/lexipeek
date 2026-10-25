add_library(lexipeek_core STATIC
  src/packs/packs.cpp
  src/mt/text.cpp
  src/mt/model_host.cpp
  src/mt/llm.cpp
  src/mt/langid.cpp
  src/mt/translator.cpp)
target_include_directories(lexipeek_core PUBLIC include PRIVATE src)
target_link_libraries(lexipeek_core
  PUBLIC Threads::Threads
  PRIVATE lexipeek_bergamot ssplit fasttext_lib llama pcre2-8-static nlohmann_json::nlohmann_json utf8proc::utf8proc
          SQLite::SQLite3 CURL::libcurl $<IF:$<TARGET_EXISTS:zstd::libzstd_static>,zstd::libzstd_static,zstd::libzstd_shared>)
lexipeek_flags(lexipeek_core)

# Marian's headers need C++17 (std::result_of is gone in C++20): Bergamot's glue builds on its own.
add_library(lexipeek_bergamot STATIC src/mt/bergamot.cpp)
set_target_properties(lexipeek_bergamot PROPERTIES CXX_STANDARD 17)
target_include_directories(lexipeek_bergamot PRIVATE include src)
target_link_libraries(lexipeek_bergamot PRIVATE bergamot-translator)
lexipeek_flags(lexipeek_bergamot)
