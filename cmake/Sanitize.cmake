# H3: opt-in sanitizer build, for example -DPROTEUS_SANITIZE=address,undefined.
# Included before the dependencies so they are instrumented too. Findings abort the process
# (-fno-sanitize-recover), so any test that hits one fails.
set(PROTEUS_SANITIZE "" CACHE STRING "Comma-separated -fsanitize list, e.g. address,undefined")

if(PROTEUS_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT EMSCRIPTEN)
  add_compile_options(-fsanitize=${PROTEUS_SANITIZE} -fno-sanitize-recover=all
    -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${PROTEUS_SANITIZE})
  set(PROTEUS_SANITIZED TRUE)
endif()

# Coverage-guided fuzzing needs the code under test instrumented, not only the harness, so the
# libFuzzer build instruments everything (with the sanitizers) and links the fuzzer runtime into
# the harnesses only (tests/fuzz/tests.cmake).
option(PROTEUS_FUZZ "Link the fuzz harnesses with libFuzzer (Clang)" OFF)
if(PROTEUS_FUZZ AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT EMSCRIPTEN)
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all
    -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
  set(PROTEUS_SANITIZED TRUE)
endif()

# A sanitized build runs several times slower, so tests whose own TIMEOUT is tight fail on speed
# alone (ctest --timeout only sets the default). Call at the end of the top-level project, after
# every test is registered: -DPROTEUS_TEST_TIMEOUT_SCALE=4 multiplies each TIMEOUT.
set(PROTEUS_TEST_TIMEOUT_SCALE "1" CACHE STRING "Multiplier for every test's TIMEOUT property")
function(proteus_scale_test_timeouts)
  if(PROTEUS_TEST_TIMEOUT_SCALE LESS_EQUAL 1)
    return()
  endif()
  get_property(tests DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY TESTS)
  foreach(test IN LISTS tests)
    get_test_property(${test} TIMEOUT timeout)
    if(timeout)
      math(EXPR scaled "${timeout} * ${PROTEUS_TEST_TIMEOUT_SCALE}")
      set_tests_properties(${test} PROPERTIES TIMEOUT ${scaled})
    endif()
  endforeach()
endfunction()
