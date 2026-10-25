# One folder per module (src/<module>/*.cpp); Bergamot's glue builds apart (below).
file(GLOB LEXIPEEK_CORE_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(REMOVE_ITEM LEXIPEEK_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/mt/bergamot.cpp)
add_library(lexipeek_core STATIC ${LEXIPEEK_CORE_SOURCES})
target_include_directories(lexipeek_core PUBLIC include PRIVATE src ${PCRE2_INCLUDE_DIRS})
target_link_libraries(lexipeek_core
  PUBLIC Threads::Threads
  PRIVATE lexipeek_bergamot fasttext_lib llama pcre2-8-static nlohmann_json::nlohmann_json utf8proc::utf8proc
          SQLite::SQLite3 CURL::libcurl LibArchive::LibArchive unofficial::hash-library ZLIB::ZLIB $<IF:$<TARGET_EXISTS:zstd::libzstd_static>,zstd::libzstd_static,zstd::libzstd_shared>)
lexipeek_flags(lexipeek_core)

# Marian's headers need C++17 (std::result_of is gone in C++20): Bergamot's glue builds on its own.
add_library(lexipeek_bergamot STATIC src/mt/bergamot.cpp)
set_target_properties(lexipeek_bergamot PROPERTIES CXX_STANDARD 17)
target_include_directories(lexipeek_bergamot PRIVATE include src)
target_link_libraries(lexipeek_bergamot PRIVATE bergamot-translator)
lexipeek_flags(lexipeek_bergamot)
