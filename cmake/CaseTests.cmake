# One CTest entry per case of a validator built on tools/boat_test_cases.h.
#
#   proteus_add_case_tests(boat_banking validate_boat_banking LABELS cpu [WORKING_DIRECTORY dir])
#
# registers boat_banking.<case> for every line of `validate_boat_banking --list`, labelled with
# LABELS plus `quick` or `slow`. The list is regenerated after each build of the target, so
# case names live only in the C table; before the first build a single <prefix>_NOT_BUILT test
# fails in their place.

set(PROTEUS_CASE_TESTS_SCRIPT ${CMAKE_CURRENT_LIST_FILE})

function(proteus_add_case_tests prefix target)
  cmake_parse_arguments(ARG "" "WORKING_DIRECTORY" "LABELS" ${ARGN})
  if(NOT ARG_WORKING_DIRECTORY)
    set(ARG_WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  endif()
  set(cases ${CMAKE_CURRENT_BINARY_DIR}/case_tests/${prefix}.cmake)
  set(include ${CMAKE_CURRENT_BINARY_DIR}/case_tests/${prefix}_include.cmake)
  string(REPLACE ";" "," labels "${ARG_LABELS}")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -D PROTEUS_CASE_EXE=$<TARGET_FILE:${target}>
      -D PROTEUS_CASE_PREFIX=${prefix} -D PROTEUS_CASE_OUT=${cases}
      -D PROTEUS_CASE_LABELS=${labels} -D PROTEUS_CASE_DIR=${ARG_WORKING_DIRECTORY}
      -P ${PROTEUS_CASE_TESTS_SCRIPT}
    VERBATIM)
  file(WRITE ${include}
    "if(EXISTS \"${cases}\")\n"
    "  include(\"${cases}\")\n"
    "else()\n"
    "  add_test(${prefix}_NOT_BUILT ${prefix}_NOT_BUILT)\n"
    "endif()\n")
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${include})
endfunction()

# Script mode: list the cases and write their add_test calls.
if(CMAKE_SCRIPT_MODE_FILE AND PROTEUS_CASE_EXE)
  execute_process(COMMAND ${PROTEUS_CASE_EXE} --list WORKING_DIRECTORY ${PROTEUS_CASE_DIR}
    OUTPUT_VARIABLE listing RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${PROTEUS_CASE_EXE} --list failed: ${result}")
  endif()
  set(script "")
  string(REPLACE "," ";" PROTEUS_CASE_LABELS "${PROTEUS_CASE_LABELS}")
  string(REPLACE "\n" ";" lines "${listing}")
  foreach(line IN LISTS lines)
    if(line MATCHES "^([A-Za-z0-9_]+) (quick|slow)$")
      set(name ${PROTEUS_CASE_PREFIX}.${CMAKE_MATCH_1})
      string(APPEND script
        "add_test(${name} \"${PROTEUS_CASE_EXE}\" --case ${CMAKE_MATCH_1})\n"
        "set_tests_properties(${name} PROPERTIES LABELS \"${PROTEUS_CASE_LABELS};${CMAKE_MATCH_2}\""
        " WORKING_DIRECTORY \"${PROTEUS_CASE_DIR}\")\n")
    elseif(NOT line STREQUAL "")
      message(FATAL_ERROR "${PROTEUS_CASE_EXE} --list: unexpected line '${line}'")
    endif()
  endforeach()
  file(WRITE ${PROTEUS_CASE_OUT} "${script}")
endif()
