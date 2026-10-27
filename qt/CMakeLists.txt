# The Qt shell: Windows and Linux (on macOS only to work on it; the Mac app is mac/). It draws the core's views.
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
set(CMAKE_AUTOMOC ON)
file(GLOB LEXIPEEK_QT_SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(lexipeek-qt WIN32 ${LEXIPEEK_QT_SOURCES})
target_link_libraries(lexipeek-qt PRIVATE lexipeek_core Qt6::Widgets Qt6::Concurrent)
lexipeek_flags(lexipeek-qt)
# Snapshots draw on the "minimal" platform (in memory, nothing on the screen).
qt_import_plugins(lexipeek-qt INCLUDE Qt6::QMinimalIntegrationPlugin)
