# tests/core: registered from tests/tests.cmake in the project root's directory scope.

add_executable(validate_boat_wake tests/core/validate_boat_wake.c)
target_link_libraries(validate_boat_wake PRIVATE proteus_boat_core)
target_compile_options(validate_boat_wake PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
if(NOT MSVC)
  target_link_libraries(validate_boat_wake PRIVATE m)
endif()
add_test(NAME boat_wake_owned_field COMMAND validate_boat_wake)
add_executable(validate_boat_cargo tests/core/validate_boat_cargo.c)
target_link_libraries(validate_boat_cargo PRIVATE proteus_boat_core)
target_compile_options(validate_boat_cargo PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_cargo COMMAND validate_boat_cargo)
set_tests_properties(boat_cargo PROPERTIES LABELS "cpu;quick")
# Marina M3 groundwork: exact common tick period for planned route clearance, core only.
add_executable(validate_boat_schedule tests/core/validate_boat_schedule.c)
target_link_libraries(validate_boat_schedule PRIVATE proteus_boat_core)
target_compile_options(validate_boat_schedule PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_schedule COMMAND validate_boat_schedule)
set_tests_properties(boat_schedule PROPERTIES LABELS "core;cpu;quick")
add_executable(validate_boat_clock tests/core/validate_boat_clock.c)
target_link_libraries(validate_boat_clock PRIVATE proteus_boat_drive)
target_compile_options(validate_boat_clock PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
set(BOAT_CLOCK_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/boat-clock-test")
file(MAKE_DIRECTORY "${BOAT_CLOCK_TEST_DIR}")
proteus_add_case_tests(boat_clock validate_boat_clock LABELS cpu ARGS ${BOAT_CLOCK_TEST_DIR})
add_test(NAME boat_clock_soak_smoke
  COMMAND validate_boat_clock --soak 60 ${BOAT_CLOCK_TEST_DIR} --waves)
add_executable(validate_boat tests/core/validate_boat.c)
add_executable(validate_boat_banking tests/core/validate_boat_banking.c)
add_executable(validate_boat_turn_entry tests/core/validate_boat_turn_entry.c)
add_executable(validate_boat_environment tests/core/validate_boat_environment.c)
add_executable(validate_boat_world tests/core/validate_boat_world.c)
target_link_libraries(validate_boat_world PRIVATE proteus_boat_drive)
# Display-free core gate: links the core library alone and checks its source/symbol boundary.
add_executable(validate_boat_core tests/core/validate_boat_core.c)
target_link_libraries(validate_boat_core PRIVATE proteus_boat_core)
target_compile_options(validate_boat_core PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_core COMMAND validate_boat_core)
# Lua plugins P0: manifest, restricted evaluation, voyage decoding and parity with the compiled
# route; its file cases work in a scratch directory under the build tree.
# Plugin P5a: mark validation and the fleet entry detector, core only.
add_executable(validate_boat_marks tests/core/validate_boat_marks.c)
target_link_libraries(validate_boat_marks PRIVATE proteus_boat_core)
target_compile_options(validate_boat_marks PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_marks validate_boat_marks LABELS core cpu)
# Multiplayer M2: the local four-boat authority and its scripted server, also core-only.
add_executable(validate_boat_fleet tests/core/validate_boat_fleet.c)
target_link_libraries(validate_boat_fleet PRIVATE proteus_boat_core)
# Capsize deathmatch rules: credit, respawns, bailouts and rounds over a core-only fleet.
add_executable(validate_boat_combat tests/core/validate_boat_combat.c)
target_link_libraries(validate_boat_combat PRIVATE proteus_boat_net)
add_test(NAME boat_combat COMMAND validate_boat_combat)
set_tests_properties(boat_combat PROPERTIES PASS_REGULAR_EXPRESSION "boat_combat=ok")
add_executable(validate_boat_match tests/core/validate_boat_match.c)
target_link_libraries(validate_boat_match PRIVATE proteus_boat_core)
# River sprint rules: gates, grid hold, places, recovery, deadlines and a four-bot ferry race.
# P2: the native race-rule suite also runs against the owned Lua reference.
add_executable(validate_boat_race_plugin tests/core/validate_boat_race.c)
target_compile_definitions(validate_boat_race_plugin PRIVATE BOAT_RACE_PLUGIN_FIXTURE=1)
target_link_libraries(validate_boat_race_plugin PRIVATE proteus_plugins)
target_compile_options(validate_boat_race_plugin PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_race_plugin validate_boat_race_plugin LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(validate_boat_race tests/core/validate_boat_race.c)
target_link_libraries(validate_boat_race PRIVATE proteus_boat_core)
proteus_add_case_tests(boat_fleet validate_boat_fleet LABELS core cpu)
add_test(NAME boat_match COMMAND validate_boat_match)
set_tests_properties(boat_match PROPERTIES PASS_REGULAR_EXPRESSION "boat_match=ok")
proteus_add_case_tests(boat_race validate_boat_race LABELS core cpu)
proteus_add_case_tests(boat_handling validate_boat LABELS cpu)
proteus_add_case_tests(boat_banking validate_boat_banking LABELS cpu)
add_test(NAME boat_turn_entry COMMAND validate_boat_turn_entry)
proteus_add_case_tests(boat_environment validate_boat_environment LABELS cpu)
proteus_add_case_tests(boat_world_isolation validate_boat_world LABELS cpu)

add_executable(validate_harbor tests/core/validate_harbor.c)
target_link_libraries(validate_harbor PRIVATE proteus_boat_session)
target_compile_options(validate_harbor PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME harbor_geometry COMMAND validate_harbor)

add_executable(validate_traffic tests/core/validate_traffic.c)
target_link_libraries(validate_traffic PRIVATE proteus_boat_session)
target_compile_options(validate_traffic PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME harbor_traffic COMMAND validate_traffic)
