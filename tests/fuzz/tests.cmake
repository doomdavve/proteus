# tests/fuzz: registered from tests/tests.cmake in the project root's directory scope.
#
# Each harness is a libFuzzer entry point. By default it links tests/fuzz/fuzz_driver.c, a
# deterministic replay-and-mutate driver, and runs as an ordinary test. With
# -DPROTEUS_FUZZ=ON and Clang, it links libFuzzer instead, for coverage-guided runs:
#   ./build/fuzz_net_decode -max_total_time=600 corpus/net_decode
# (`<name>_seeds --runs 0 --write-seeds DIR` writes the starting corpus).
# PROTEUS_FUZZ is defined in cmake/Sanitize.cmake, which also instruments the whole build.

function(proteus_fuzz_harness name source runs)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
  target_compile_options(${name} PRIVATE ${PROTEUS_WARNINGS})
  target_link_libraries(${name} PRIVATE ${ARGN})
  if(PROTEUS_FUZZ)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
    # libFuzzer starts from a corpus directory; this writes the harness's seeds into one:
    #   ./build/<name>_seeds --runs 0 --write-seeds DIR
    add_executable(${name}_seeds ${source} tests/fuzz/fuzz_driver.c)
    target_include_directories(${name}_seeds PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_link_libraries(${name}_seeds PRIVATE ${ARGN})
  else()
    target_sources(${name} PRIVATE tests/fuzz/fuzz_driver.c)
    add_test(NAME ${name} COMMAND ${name} --runs ${runs})
    set_tests_properties(${name} PROPERTIES LABELS "core;cpu;quick;fuzz" TIMEOUT 120
      PASS_REGULAR_EXPRESSION "fuzz_driver: corpus=[0-9]+ runs=${runs} ok")
  endif()
endfunction()

if(NOT EMSCRIPTEN)
  proteus_fuzz_harness(fuzz_net_decode tests/fuzz/fuzz_net_decode.c 20000 proteus_boat_net)
  proteus_fuzz_harness(fuzz_net_server tests/fuzz/fuzz_net_server.c 300 proteus_boat_net)
  if(NOT PROTEUS_FUZZ)
    # The server harness must reach past the handshake, or it tests nothing. Its summary prints
    # only on a normal exit, so matching it also fails the test on a crash.
    set_tests_properties(fuzz_net_server PROPERTIES PASS_REGULAR_EXPRESSION
      "spawns=[1-9][0-9]* resyncs=[1-9][0-9]*")
  endif()
  proteus_fuzz_harness(fuzz_session_record tests/fuzz/fuzz_session_record.c 5000
    proteus_session_record)
endif()
