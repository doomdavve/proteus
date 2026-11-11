# tests/plugins: registered from tests/tests.cmake in the project root's directory scope.

# AR5: a data-only consumer. It links the catalog and plugin data only; no Lua or workers.
add_executable(validate_boat_plugin_data tests/plugins/validate_boat_plugin_data.c)
target_link_libraries(validate_boat_plugin_data PRIVATE proteus_plugin_catalog)
target_compile_options(validate_boat_plugin_data PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_plugin_data COMMAND validate_boat_plugin_data)
set_tests_properties(boat_plugin_data PROPERTIES LABELS "cpu;quick")
add_executable(validate_boat_plugin tests/plugins/validate_boat_plugin.c)
target_link_libraries(validate_boat_plugin PRIVATE proteus_plugins)
target_compile_options(validate_boat_plugin PRIVATE ${PROTEUS_WARNINGS})
set(BOAT_PLUGIN_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-test")
file(MAKE_DIRECTORY "${BOAT_PLUGIN_TEST_DIR}")
target_compile_definitions(validate_boat_plugin PRIVATE
  BOAT_PLUGIN_TEST_DIR="${BOAT_PLUGIN_TEST_DIR}")
proteus_add_case_tests(boat_plugin validate_boat_plugin LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# P3.1: isolated objective transactions; P3.2 live adapter checks are in boat_plugin_session.
add_executable(validate_boat_plugin_state tests/plugins/validate_boat_plugin_state.c)
target_link_libraries(validate_boat_plugin_state PRIVATE proteus_plugins)
target_compile_options(validate_boat_plugin_state PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_plugin_state validate_boat_plugin_state LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Lua plugins P1: staging against the map, reload policies on a live session, Undo, save polling
# on a virtual clock, and tick-for-tick parity with the compiled voyage.
# Its game_controls case drives wake_velocity's --plugin glue on files in a scratch directory.
add_executable(validate_boat_plugin_session tests/plugins/validate_boat_plugin_session.c
  apps/wake_velocity/wake_velocity_plugin.c)
target_link_libraries(validate_boat_plugin_session PRIVATE proteus_plugins proteus_wake_objective proteus_plugin_catalog proteus_boat_session)
target_compile_options(validate_boat_plugin_session PRIVATE ${PROTEUS_WARNINGS})
set(BOAT_PLUGIN_SESSION_DIR "${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-session-test")
file(MAKE_DIRECTORY "${BOAT_PLUGIN_SESSION_DIR}")
target_compile_definitions(validate_boat_plugin_session PRIVATE
  BOAT_PLUGIN_SESSION_DIR="${BOAT_PLUGIN_SESSION_DIR}")
proteus_add_case_tests(boat_plugin_session validate_boat_plugin_session LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# PD1: bundled catalog entries equal their evaluated sources and compiled defaults; no Lua VM.
add_executable(validate_boat_plugin_catalog tests/plugins/validate_boat_plugin_catalog.c)
target_link_libraries(validate_boat_plugin_catalog PRIVATE proteus_plugins proteus_plugin_catalog)
target_compile_options(validate_boat_plugin_catalog PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_plugin_catalog validate_boat_plugin_catalog LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT EMSCRIPTEN)
  add_executable(validate_boat_plugin_eval tests/plugins/validate_boat_plugin_eval.c)
  target_link_libraries(validate_boat_plugin_eval PRIVATE proteus_plugins)
  target_compile_options(validate_boat_plugin_eval PRIVATE ${PROTEUS_WARNINGS})
  add_test(NAME boat_plugin_eval COMMAND validate_boat_plugin_eval)
  set_tests_properties(boat_plugin_eval PROPERTIES LABELS "cpu;quick" TIMEOUT 10)
  add_executable(validate_boat_plugin_stage tests/plugins/validate_boat_plugin_stage.c
    apps/wake_velocity/wake_velocity_plugin.c)
  target_link_libraries(validate_boat_plugin_stage PRIVATE proteus_plugins proteus_wake_objective proteus_plugin_catalog proteus_boat_session)
  target_compile_options(validate_boat_plugin_stage PRIVATE ${PROTEUS_WARNINGS})
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-stage-test")
  target_compile_definitions(validate_boat_plugin_stage PRIVATE
    BOAT_PLUGIN_STAGE_DIR="${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-stage-test")
  add_test(NAME boat_plugin_stage COMMAND validate_boat_plugin_stage)
  set_tests_properties(boat_plugin_stage PROPERTIES LABELS "cpu"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 30)
  # AR4: the browser backend and common staging, compiled natively against a C model of the
  # page transport; the archive's own staging objects are not pulled in.
  add_executable(validate_boat_plugin_stage_web tests/plugins/validate_boat_plugin_stage_web.c
    src/plugins/boat_plugin_stage.c src/plugins/boat_plugin_stage_web.c)
  target_link_libraries(validate_boat_plugin_stage_web PRIVATE proteus_plugins)
  target_compile_options(validate_boat_plugin_stage_web PRIVATE ${PROTEUS_WARNINGS})
  add_test(NAME boat_plugin_stage_web COMMAND validate_boat_plugin_stage_web)
  set_tests_properties(boat_plugin_stage_web PROPERTIES LABELS "cpu;quick"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 10)
endif()
add_executable(validate_boat_plugin_race tests/plugins/validate_boat_plugin_race.c tools/boat_race_plugin.c)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/race-plugin-test")
target_compile_definitions(validate_boat_plugin_race PRIVATE
  BOAT_RACE_PLUGIN_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/race-plugin-test")
target_link_libraries(validate_boat_plugin_race PRIVATE proteus_plugins)
target_compile_options(validate_boat_plugin_race PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_plugin_race validate_boat_plugin_race LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(validate_harbor_life tests/plugins/validate_harbor_life.c)
target_link_libraries(validate_harbor_life PRIVATE proteus_plugins proteus_boat_session)
target_compile_options(validate_harbor_life PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME harbor_life_qualification COMMAND validate_harbor_life)
set_tests_properties(harbor_life_qualification PROPERTIES LABELS "cpu;slow"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
