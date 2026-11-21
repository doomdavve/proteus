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
  # M5 W3: a package with modules through the read, worker, host and query carriers.
  add_executable(validate_boat_plugin_modules tests/plugins/validate_boat_plugin_modules.c
    apps/wake_velocity/wake_velocity_plugin.c apps/wake_velocity/wake_command.c)
  target_link_libraries(validate_boat_plugin_modules PRIVATE proteus_plugins proteus_wake_objective proteus_plugin_catalog proteus_boat_session)
  target_compile_options(validate_boat_plugin_modules PRIVATE ${PROTEUS_WARNINGS})
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-modules-test")
  target_compile_definitions(validate_boat_plugin_modules PRIVATE
    BOAT_PLUGIN_MODULES_DIR="${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-modules-test")
  add_test(NAME boat_plugin_modules COMMAND validate_boat_plugin_modules)
  set_tests_properties(boat_plugin_modules PROPERTIES LABELS "cpu"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 60)
  # M5 W3: the generator carries a package with modules (a test-only fixture, not a shipped
  # plugin), and an invalid module fails the generator with the module's file named.
  set(BOAT_MODULES_FIXTURES tests/support/fixtures/plugins)
  set(BOAT_MODULES_CATALOG_C "${CMAKE_CURRENT_BINARY_DIR}/generated/boat_plugin_catalog_modules.c")
  file(GLOB_RECURSE BOAT_MODULES_FIXTURE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/${BOAT_MODULES_FIXTURES}/modules-route/*)
  add_custom_command(OUTPUT ${BOAT_MODULES_CATALOG_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND boat_plugin_catalog_gen --output ${BOAT_MODULES_CATALOG_C}
      ${BOAT_MODULES_FIXTURES}/modules-route
    DEPENDS boat_plugin_catalog_gen ${BOAT_MODULES_FIXTURE_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating the modules fixture catalog"
    VERBATIM)
  add_executable(validate_boat_plugin_catalog_modules
    tests/plugins/validate_boat_plugin_catalog_modules.c src/plugins/boat_plugin_catalog.c
    ${BOAT_MODULES_CATALOG_C})
  target_link_libraries(validate_boat_plugin_catalog_modules PRIVATE proteus_plugins)
  target_compile_options(validate_boat_plugin_catalog_modules PRIVATE ${PROTEUS_WARNINGS})
  add_test(NAME boat_plugin_catalog_modules COMMAND validate_boat_plugin_catalog_modules)
  set_tests_properties(boat_plugin_catalog_modules PROPERTIES LABELS "cpu;quick"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 10)
  foreach(bad syntax runtime)
    add_test(NAME boat_plugin_catalog_modules_${bad}
      COMMAND boat_plugin_catalog_gen ${BOAT_MODULES_FIXTURES}/modules-bad-${bad})
    set_tests_properties(boat_plugin_catalog_modules_${bad} PROPERTIES LABELS "cpu;quick"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 10
      PASS_REGULAR_EXPRESSION "modules-bad-${bad}: [a-z]+ error in modules/route\\.lua:2 ")
  endforeach()
  # AR4: the browser backend and common staging, compiled natively against a C model of the
  # page transport; the archive's own staging objects are not pulled in.
  add_executable(validate_boat_plugin_stage_web tests/plugins/validate_boat_plugin_stage_web.c
    src/plugins/boat_plugin_stage.c src/plugins/boat_plugin_stage_web.c)
  target_link_libraries(validate_boat_plugin_stage_web PRIVATE proteus_plugins)
  target_compile_options(validate_boat_plugin_stage_web PRIVATE ${PROTEUS_WARNINGS})
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-stage-web-test")
  target_compile_definitions(validate_boat_plugin_stage_web PRIVATE
    BOAT_PLUGIN_STAGE_WEB_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-stage-web-test")
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
# M5 W6: a manual measurement tool (not registered with CTest) for the cost of a marina package.
add_executable(measure_marina_package tests/plugins/measure_marina_package.c)
target_link_libraries(measure_marina_package PRIVATE proteus_plugins proteus_boat_session)
target_compile_options(measure_marina_package PRIVATE ${PROTEUS_WARNINGS})
# M5 W5: harbor-life and harbor-room decode to the scene the single-file packages produced, and
# the directory, catalog and room paths agree.
add_executable(validate_marina_shared tests/plugins/validate_marina_shared.c
  src/room/boat_room_package.c)
target_link_libraries(validate_marina_shared PRIVATE proteus_plugins proteus_plugin_catalog
  proteus_boat_net)
target_compile_options(validate_marina_shared PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME marina_shared COMMAND validate_marina_shared)
set_tests_properties(marina_shared PROPERTIES LABELS "core;cpu" PASS_REGULAR_EXPRESSION
  "marina_shared=ok" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
