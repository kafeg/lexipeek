# Engines from engines/ (submodules at InkPeek's commits), configured as in InkPeek's app/src/main/cpp/CMakeLists.txt.
set(E ${PROJECT_SOURCE_DIR}/engines)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # old sub-projects under CMake 4
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" LEXIPEEK_CPU)
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  string(TOLOWER "${CMAKE_OSX_ARCHITECTURES}" LEXIPEEK_CPU)
endif()
if(LEXIPEEK_CPU MATCHES "arm64|aarch64")
  set(LEXIPEEK_ARM ON)
endif()

foreach(opt COMPILE_CUDA USE_TCMALLOC BUILD_SHARED_LIBS USE_WASM_COMPATIBLE_SOURCE SPM_ENABLE_SHARED SPM_BUILD_TEST
    SPM_ENABLE_TCMALLOC COMPILE_TESTS COMPILE_UNIT_TESTS USE_MKL USE_FBGEMM GIT_SUBMODULE PCRE2_BUILD_PCRE2GREP
    PCRE2_BUILD_TESTS SSPLIT_USE_INTERNAL_PCRE2 USE_DOXYGEN COMPILE_PYTHON)
  set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(opt USE_STATIC_LIBS THREADS_PREFER_PTHREAD_FLAG USE_PATHIECPP PCRE2_SUPPORT_JIT PCRE2_STATIC_PIC COMPILE_LIBRARY_ONLY
    SSPLIT_COMPILE_LIBRARY_ONLY)
  set(${opt} ON CACHE BOOL "" FORCE)
endforeach()

# PCRE2 (BSD): ssplit's sentence splitting and the core's regex; ssplit takes it as a found library.
add_subdirectory(${E}/pcre2 ${CMAKE_BINARY_DIR}/engines/pcre2 EXCLUDE_FROM_ALL SYSTEM)
set(PCRE2_LIBRARIES pcre2-8-static CACHE STRING "" FORCE)
set(PCRE2_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/engines/pcre2 CACHE PATH "" FORCE)

# Bergamot (MPL-2.0) with its marian: ruy on ARM, intgemm on x86-64; a portable baseline, not this machine's CPU.
if(LEXIPEEK_ARM)
  set(BUILD_ARCH armv8-a CACHE STRING "" FORCE)
else()
  set(BUILD_ARCH x86-64-v2 CACHE STRING "" FORCE)
endif()
add_subdirectory(${E}/bergamot ${CMAKE_BINARY_DIR}/engines/bergamot EXCLUDE_FROM_ALL SYSTEM)

# fastText (MIT): its library sources only (no main.cc).
file(GLOB FASTTEXT_SOURCES ${E}/fasttext/src/*.cc)
list(FILTER FASTTEXT_SOURCES EXCLUDE REGEX ".*/main\\.cc$")
add_library(fasttext_lib STATIC ${FASTTEXT_SOURCES})
target_include_directories(fasttext_lib SYSTEM PUBLIC ${E}/fasttext/src)
find_package(Threads REQUIRED)
target_link_libraries(fasttext_lib PUBLIC Threads::Threads)
set_target_properties(fasttext_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# llama.cpp (MIT): Metal on macOS; weights mapped as they are, not repacked (see mt/llm.cpp).
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
if(LEXIPEEK_ARM)
  set(GGML_CPU_ARM_ARCH "armv8.2-a+dotprod" CACHE STRING "" FORCE)
endif()
foreach(opt GGML_OPENMP GGML_LLAMAFILE GGML_BACKEND_DL GGML_CPU_KLEIDIAI LLAMA_CURL LLAMA_OPENSSL LLAMA_BUILD_TESTS
    LLAMA_BUILD_EXAMPLES LLAMA_BUILD_TOOLS LLAMA_BUILD_SERVER LLAMA_BUILD_COMMON GGML_BUILD_TESTS GGML_BUILD_EXAMPLES)
  set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(${E}/llama ${CMAKE_BINARY_DIR}/engines/llama EXCLUDE_FROM_ALL SYSTEM)
