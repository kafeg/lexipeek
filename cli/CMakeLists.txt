# One file per group of commands (cli/*.cpp), each registers itself; memory.cpp has the OS headers.
file(GLOB LEXIPEEK_CLI_SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(lexipeek-cli ${LEXIPEEK_CLI_SOURCES} $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/utf8.manifest>)
target_link_libraries(lexipeek-cli PRIVATE lexipeek_core $<$<PLATFORM_ID:Windows>:psapi>)
lexipeek_flags(lexipeek-cli)
