# macOS: one static library of the core, the engines and the vcpkg libraries for the Swift app (mac/), plus what it links.
include(${CMAKE_CURRENT_LIST_DIR}/link_closure.cmake)

lexipeek_collect(lexipeek_core LEXIPEEK_LIBS LEXIPEEK_FLAGS)
get_property(libs GLOBAL PROPERTY LEXIPEEK_LIBS)
get_property(flags GLOBAL PROPERTY LEXIPEEK_FLAGS)
list(REMOVE_DUPLICATES libs)
list(REMOVE_DUPLICATES flags)
string(REPLACE ";" " " flags "${flags}")
set(out ${CMAKE_BINARY_DIR}/mac)
file(GENERATE OUTPUT ${out}/link.xcconfig CONTENT "LEXIPEEK_LDFLAGS = -llexipeek_all ${flags} -lc++\n")
add_custom_command(OUTPUT ${out}/liblexipeek_all.a
  COMMAND libtool -static -no_warning_for_no_symbols -o ${out}/liblexipeek_all.a ${libs}
  DEPENDS ${libs} lexipeek_core
  COMMAND_EXPAND_LISTS VERBATIM)
add_custom_target(lexipeek_all ALL DEPENDS ${out}/liblexipeek_all.a)
