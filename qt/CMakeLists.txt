# The Qt shell: Linux, and Windows' fallback (on macOS only to work on it; the Mac app is mac/). It draws the core's views.
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
file(GLOB LEXIPEEK_QT_SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(lexipeek-qt WIN32 ${LEXIPEEK_QT_SOURCES} lexipeek.qrc $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/cmake/utf8.manifest>)
target_link_libraries(lexipeek-qt PRIVATE lexipeek_platform Qt6::Widgets Qt6::Concurrent)
lexipeek_flags(lexipeek-qt)
lexipeek_qwentts_dll(lexipeek-qt)
# 0.2.<git commit count>, as InkPeek's and the Mac app's (Makefile's VERSION): version.h, checked at every build.
add_custom_target(lexipeek-version COMMAND ${CMAKE_COMMAND} -DSOURCE=${PROJECT_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/version.h
                  -P ${PROJECT_SOURCE_DIR}/cmake/version.cmake BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.h)
add_dependencies(lexipeek-qt lexipeek-version)
target_include_directories(lexipeek-qt PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# The .rc's numbers: from the configure (the file version of a release build).
execute_process(COMMAND git rev-list --count HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE LEXIPEEK_COMMITS
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
# Snapshots draw on the "minimal" platform (in memory, nothing on the screen).
qt_import_plugins(lexipeek-qt INCLUDE Qt6::QMinimalIntegrationPlugin)
# Windows: LexiPeek.exe with its icon and version.
if(WIN32)
  set(LEXIPEEK_ICO ${CMAKE_CURRENT_SOURCE_DIR}/icons/lexipeek.ico)
  configure_file(lexipeek.rc.in ${CMAKE_CURRENT_BINARY_DIR}/lexipeek.rc @ONLY)
  target_sources(lexipeek-qt PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/lexipeek.rc)
  set_target_properties(lexipeek-qt PROPERTIES OUTPUT_NAME LexiPeek)
endif()
# Linux: `lexipeek`, Qt linked in with its X11 platform; the C++ runtime too (any distribution's), the libraries a desktop
# may lack beside it in lib/ (scripts/build-linux.sh).
if(LINUX)
  qt_import_plugins(lexipeek-qt INCLUDE Qt6::QXcbIntegrationPlugin)
  set_target_properties(lexipeek-qt PROPERTIES OUTPUT_NAME lexipeek BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib")
  target_link_options(lexipeek-qt PRIVATE -static-libstdc++ -static-libgcc)
endif()
