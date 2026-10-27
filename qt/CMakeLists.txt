# The Qt shell: Windows and Linux (on macOS only to work on it; the Mac app is mac/). It draws the core's views.
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
file(GLOB LEXIPEEK_QT_SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(lexipeek-qt WIN32 ${LEXIPEEK_QT_SOURCES} lexipeek.qrc $<$<PLATFORM_ID:Windows>:${PROJECT_SOURCE_DIR}/cmake/utf8.manifest>)
target_link_libraries(lexipeek-qt PRIVATE lexipeek_core Qt6::Widgets Qt6::Concurrent $<$<PLATFORM_ID:Windows>:dwmapi ole32 oleaut32 advapi32>)
lexipeek_flags(lexipeek-qt)
# 0.1.<git commit count>, as InkPeek's and the Mac app's (Makefile's VERSION).
execute_process(COMMAND git rev-list --count HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE LEXIPEEK_COMMITS
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
target_compile_definitions(lexipeek-qt PRIVATE LEXIPEEK_VERSION="0.1.${LEXIPEEK_COMMITS}")
# Snapshots draw on the "minimal" platform (in memory, nothing on the screen).
qt_import_plugins(lexipeek-qt INCLUDE Qt6::QMinimalIntegrationPlugin)
# Windows: LexiPeek.exe with its icon and version.
if(WIN32)
  set(LEXIPEEK_ICO ${CMAKE_CURRENT_SOURCE_DIR}/icons/lexipeek.ico)
  configure_file(lexipeek.rc.in ${CMAKE_CURRENT_BINARY_DIR}/lexipeek.rc @ONLY)
  target_sources(lexipeek-qt PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/lexipeek.rc)
  set_target_properties(lexipeek-qt PROPERTIES OUTPUT_NAME LexiPeek)
endif()
