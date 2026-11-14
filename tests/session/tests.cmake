# tests/session: registered from tests/tests.cmake in the project root's directory scope.

add_executable(validate_boat_sandbox_save tests/session/validate_boat_sandbox_save.c)
target_link_libraries(validate_boat_sandbox_save PRIVATE proteus_sandbox_save)
target_compile_options(validate_boat_sandbox_save PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_sandbox_save COMMAND validate_boat_sandbox_save)
set_tests_properties(boat_sandbox_save PROPERTIES LABELS "cpu;quick")
# SV1h: discovery over build-tree directories and the SAVE row's confirmations.
add_executable(validate_boat_work_menu tests/session/validate_boat_work_menu.c)
target_link_libraries(validate_boat_work_menu PRIVATE proteus_work_save proteus_plugin_catalog)
target_compile_options(validate_boat_work_menu PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_work_menu COMMAND validate_boat_work_menu ${CMAKE_CURRENT_BINARY_DIR}/work-menu-tests)
set_tests_properties(boat_work_menu PROPERTIES LABELS "cpu;quick")
# L3: the working harbour on its private local endpoint against the direct session.
add_executable(validate_boat_work_local tests/session/validate_boat_work_local.c)
target_link_libraries(validate_boat_work_local PRIVATE proteus_work_local proteus_render
  proteus_plugin_catalog)
target_compile_options(validate_boat_work_local PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_work_local validate_boat_work_local LABELS core cpu)
# The work session's daylight clock: fixed ticks, save/resume and the idle autosave watch.
add_executable(validate_boat_work_clock tests/session/validate_boat_work_clock.c)
target_link_libraries(validate_boat_work_clock PRIVATE proteus_work_session proteus_plugin_catalog)
target_compile_options(validate_boat_work_clock PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_work_clock COMMAND validate_boat_work_clock)
set_tests_properties(boat_work_clock PROPERTIES LABELS "cpu;quick")
add_executable(validate_boat_profile tests/session/validate_boat_profile.c)
add_executable(validate_boat_replay tests/session/validate_boat_replay.c)
target_link_libraries(validate_boat_profile PRIVATE proteus_boat_drive)
# Play-testing rates preserve fixed-tick physics.
add_executable(validate_boat_time_control tests/session/validate_boat_time_control.c)
target_link_libraries(validate_boat_time_control PRIVATE proteus_boat_session)
target_compile_options(validate_boat_time_control PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_time_control COMMAND validate_boat_time_control)
# Adventure A3.4: the outer voyage's shelters, exposed water, arrival and recovery.
add_executable(validate_boat_voyage tests/session/validate_boat_voyage.c)
target_link_libraries(validate_boat_voyage PRIVATE proteus_boat_session)
target_compile_options(validate_boat_voyage PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_voyage COMMAND validate_boat_voyage)
set_tests_properties(boat_voyage PROPERTIES PASS_REGULAR_EXPRESSION "boat_voyage=ok")
add_test(NAME boat_profile_records COMMAND validate_boat_profile)
add_test(NAME boat_replay COMMAND validate_boat_replay ${BOAT_REPLAY_TEST_DIR})
set_tests_properties(boat_replay PROPERTIES FIXTURES_SETUP boat_recording)
