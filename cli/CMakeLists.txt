add_executable(lexipeek-cli main.cpp memory.cpp)
target_link_libraries(lexipeek-cli PRIVATE lexipeek_core $<$<PLATFORM_ID:Windows>:psapi>)
lexipeek_flags(lexipeek-cli)
