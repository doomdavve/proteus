# Test registration, near each owner. Included (not added as a subdirectory) so every test
# stays in the project root's directory scope: lanes, labels, CI selection and working
# directories apply to all of them at once.
set(BOAT_REPLAY_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/replay-tests")
include(${CMAKE_CURRENT_LIST_DIR}/core/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/scenery/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/render/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/audio/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/platform/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/net/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/plugins/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/session/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/room/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/fuzz/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/app/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/web/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/lint/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/tests.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/support/tests.cmake)

# Properties that span the tests of several owners.
set_tests_properties(boat_server_scripted_room_option_rejected boat_server_port_rejected
  boat_server_package_reload_rejected
  boat_server_bind_rejected boat_server_round_without_deathmatch_rejected boat_bot_endpoint_rejected
  boat_bot_discover_join_rejected boat_bot_snapshots_with_seconds_rejected
  boat_server_scripted_until_empty_rejected boat_net_study_mode_rejected
  boat_net_study_name_rejected boat_net_study_skin_rejected wake_velocity_host_race_location_rejected
  wake_velocity_host_hunt_location_rejected
  boat_server_ice_file_missing_rejected boat_server_ice_url_and_file_rejected
  wake_velocity_host_connect_rejected wake_velocity_port_without_host_rejected PROPERTIES WILL_FAIL TRUE)

set_tests_properties(boat_river_location_retired boat_server_river_location_retired PROPERTIES
  PASS_REGULAR_EXPRESSION "location is retired; --location outer")

# Test and application headers used by validators are private to those consumers.
get_property(PROTEUS_ROOT_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS PROTEUS_ROOT_TARGETS)
  get_target_property(kind ${target} TYPE)
  if(kind STREQUAL "EXECUTABLE")
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/apps)
  endif()
endforeach()
target_include_directories(validate_boat_plugin_race PRIVATE ${PROJECT_SOURCE_DIR})
