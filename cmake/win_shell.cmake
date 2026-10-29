# Windows: what the WinUI shell (win/LexiPeek.vcxproj, built by MSBuild) needs of the core — its include dirs, its whole
# link closure and the sources it hands to programs (cmake/vulkan_win.cpp) — as MSBuild props, <build>/win/lexipeek-core.props.
include(${CMAKE_CURRENT_LIST_DIR}/link_closure.cmake)
find_package(Vulkan QUIET)  # llama's Vulkan::Vulkan is imported in its own directory only

lexipeek_collect(lexipeek_core LEXIPEEK_LIBS LEXIPEEK_FLAGS)
get_property(libs GLOBAL PROPERTY LEXIPEEK_LIBS)
get_property(genex GLOBAL PROPERTY LEXIPEEK_LIBS_GENEX)
get_property(flags GLOBAL PROPERTY LEXIPEEK_FLAGS)
get_property(sources GLOBAL PROPERTY LEXIPEEK_LIBS_SOURCES)
list(FILTER sources INCLUDE REGEX "\\.(c|cc|cpp)$")  # not nlohmann_json's .natvis
list(REMOVE_DUPLICATES libs)
list(REMOVE_DUPLICATES flags)
separate_arguments(standard NATIVE_COMMAND "${CMAKE_CXX_STANDARD_LIBRARIES}")  # kernel32.lib shell32.lib …: what CMake links by default
set(libs "$<TARGET_FILE:lexipeek_core>;${libs};${genex};${standard}")
string(REPLACE ";" " " flags "${flags}")
set(include "${PROJECT_SOURCE_DIR}/core/include")
set(deps "$<JOIN:$<REMOVE_DUPLICATES:$<FILTER:${libs},EXCLUDE,^$>>,;>")
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/win/lexipeek-core.props CONTENT "<?xml version=\"1.0\" encoding=\"utf-8\"?>
<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>${include};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <AdditionalDependencies>${deps};%(AdditionalDependencies)</AdditionalDependencies>
      <AdditionalOptions>${flags} %(AdditionalOptions)</AdditionalOptions>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include=\"${sources}\">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
  </ItemGroup>
</Project>
")
