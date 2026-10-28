# Writes OUT (a header with LEXIPEEK_VERSION "0.1.<git commit count>", InkPeek's numbering) when the count changed:
# run at every build, so the number follows the commits without a new configure.
execute_process(COMMAND git rev-list --count HEAD WORKING_DIRECTORY ${SOURCE} OUTPUT_VARIABLE COMMITS
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT COMMITS)
  set(COMMITS 0)
endif()
set(TEXT "#pragma once\n#define LEXIPEEK_VERSION \"0.1.${COMMITS}\"\n#define LEXIPEEK_COMMITS ${COMMITS}\n")
if(EXISTS ${OUT})
  file(READ ${OUT} OLD)
endif()
if(NOT "${OLD}" STREQUAL "${TEXT}")
  file(WRITE ${OUT} "${TEXT}")
endif()
