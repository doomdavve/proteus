# tests/room: registered from tests/tests.cmake in the project root's directory scope.

add_test(NAME boat_server_scripted_deathmatch
  COMMAND boat_server --scripted --mode deathmatch --round-seconds 10 --ticks 3000)
set_tests_properties(boat_server_scripted_deathmatch PROPERTIES
  PASS_REGULAR_EXPRESSION "round 1 end:.*round 2 start.*deathmatch: round=2")
add_test(NAME boat_server_round_without_deathmatch_rejected
  COMMAND boat_server --scripted --round-seconds 60)
add_test(NAME boat_server_scripted COMMAND boat_server --scripted --ticks 2400)
set_tests_properties(boat_server_scripted PROPERTIES
  PASS_REGULAR_EXPRESSION "boat_server scripted: boats=4 ticks=2400")
add_test(NAME boat_server_scripted_room_option_rejected
  COMMAND boat_server --scripted --port 24680)
add_test(NAME boat_server_port_rejected COMMAND boat_server --port 0)
add_test(NAME boat_server_package_reload_rejected COMMAND boat_server --package-reload --ticks 1)
add_test(NAME boat_server_bind_rejected COMMAND boat_server --bind not-an-address --ticks 1)
add_test(NAME boat_server_ice_file_missing_rejected
  COMMAND boat_server --ice-server-file ${CMAKE_BINARY_DIR}/does-not-exist --ticks 1)
add_test(NAME boat_server_ice_url_and_file_rejected
  COMMAND boat_server --ice-server turn:u:p@127.0.0.1:3478 --ice-server-file ${CMAKE_BINARY_DIR}/x
    --ticks 1)
add_test(NAME boat_bot_endpoint_rejected COMMAND boat_bot --connect 127.0.0.1)
add_test(NAME boat_bot_discover_join_rejected
  COMMAND boat_bot --discover --connect 127.0.0.1:24680)
add_test(NAME boat_bot_snapshots_with_seconds_rejected
  COMMAND boat_bot --connect 127.0.0.1:24680 --snapshots 3 --seconds 2)
add_test(NAME boat_server_scripted_until_empty_rejected
  COMMAND boat_server --scripted --until-empty)
# PD2/PD3: a room built from an edited package reaches clients as data.
add_executable(validate_boat_room_package tests/room/validate_boat_room_package.c
  src/room/boat_room_package.c)
target_link_libraries(validate_boat_room_package PRIVATE proteus_plugins proteus_plugin_catalog
  proteus_boat_net)
target_compile_options(validate_boat_room_package PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_room_package validate_boat_room_package LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# P5b: a free package's room objective runs only behind the authority's rules adapter.
add_executable(validate_boat_room_objective tests/room/validate_boat_room_objective.c
  src/room/boat_room_package.c src/room/boat_room_objective.c)
target_link_libraries(validate_boat_room_objective PRIVATE proteus_plugins proteus_plugin_catalog
  proteus_boat_net)
target_compile_options(validate_boat_room_objective PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_room_objective validate_boat_room_objective LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# No networked client path may read the compiled river course.
add_test(NAME boat_room_course_source COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/room/check_room_course_source.py)
set_tests_properties(boat_room_course_source PROPERTIES LABELS "cpu;quick")
# Event-driven process smoke: boat_server and boat_bot connect, discover and exit. Session rules
# over the real transport are the boat_net_enet cases, on a virtual clock.
add_test(NAME boat_net_process COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/room/boat_net_process.py --server $<TARGET_FILE:boat_server>
  --bot $<TARGET_FILE:boat_bot>)
set_tests_properties(boat_net_process PROPERTIES PASS_REGULAR_EXPRESSION "boat_net_process=ok"
  TIMEOUT 60)
add_test(NAME boat_server_river_location_retired COMMAND boat_server --location river)

add_executable(validate_boat_room_record_objective
  tests/room/validate_boat_room_record_objective.c src/room/boat_room_objective.c)
target_link_libraries(validate_boat_room_record_objective PRIVATE
  proteus_room_record_objective proteus_plugins)
target_compile_options(validate_boat_room_record_objective PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_room_record_objective COMMAND validate_boat_room_record_objective)
set_tests_properties(boat_room_record_objective PROPERTIES LABELS "core;cpu")

add_executable(validate_boat_session_record_contract tests/room/validate_boat_session_record.c
  src/room/boat_room_objective.c src/room/boat_room_package.c)
target_link_libraries(validate_boat_session_record_contract PRIVATE proteus_session_record
  proteus_plugins proteus_plugin_catalog)
target_compile_options(validate_boat_session_record_contract PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_session_record validate_boat_session_record_contract LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(BOAT_SESSION_RECORD_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/session-record-hunt.p5gr")
add_test(NAME boat_session_record.hunt COMMAND validate_boat_session_record_contract
  --write "${BOAT_SESSION_RECORD_FIXTURE}")
set_tests_properties(boat_session_record.hunt PROPERTIES FIXTURES_SETUP session_record_hunt
  LABELS "core;cpu;slow" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME boat_session_record.playback COMMAND validate_boat_session_record
  "${BOAT_SESSION_RECORD_FIXTURE}")
add_test(NAME boat_session_record.diagnostic COMMAND validate_boat_session_record
  "${BOAT_SESSION_RECORD_FIXTURE}" --diagnostic)
add_test(NAME boat_session_record.seek COMMAND validate_boat_session_record
  "${BOAT_SESSION_RECORD_FIXTURE}" --seek 700)
set_tests_properties(boat_session_record.playback boat_session_record.diagnostic
  boat_session_record.seek PROPERTIES FIXTURES_REQUIRED session_record_hunt LABELS "core;cpu;slow"
  PASS_REGULAR_EXPRESSION "boat_session_record=ok")
add_test(NAME boat_session_record_process COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/room/check_session_record_process.py
  --server $<TARGET_FILE:boat_server> --bot $<TARGET_FILE:boat_bot>
  --validator $<TARGET_FILE:validate_boat_session_record>)
set_tests_properties(boat_session_record_process PROPERTIES LABELS "cpu" TIMEOUT 60)
