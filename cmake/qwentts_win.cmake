# Included into qwentts.cpp's own build (CMAKE_PROJECT_INCLUDE, Windows): its qwen.dll delay-loads vulkan-1.dll with the
# core's hook (cmake/vulkan_win.cpp): without a GPU driver it runs on the CPU instead of failing to load.
include_guard(GLOBAL)
set(LEXIPEEK_VULKAN_HOOK ${CMAKE_CURRENT_LIST_DIR}/vulkan_win.cpp)
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL target_sources qwen PRIVATE ${LEXIPEEK_VULKAN_HOOK})
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL target_link_options qwen PRIVATE /DELAYLOAD:vulkan-1.dll)
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL target_link_libraries qwen PRIVATE delayimp)
