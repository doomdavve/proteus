# tests/support: registered from tests/tests.cmake in the project root's directory scope.

# Fixture data for validators and review tools: the shipped voyage, course, race course and rule
# values that packages replace. Production targets never link this library.
add_library(proteus_boat_fixtures STATIC tests/support/boat_fixtures.c tests/support/boat_fixtures.h)
target_include_directories(proteus_boat_fixtures PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tools)
target_link_libraries(proteus_boat_fixtures PUBLIC proteus_boat_core)
target_compile_options(proteus_boat_fixtures PRIVATE ${PROTEUS_WARNINGS})
foreach(target validate_boat validate_boat_banking validate_boat_turn_entry validate_boat_replay
    validate_boat_environment)
  target_link_libraries(${target} PRIVATE proteus_boat_session)
endforeach()
foreach(target validate_boat validate_boat_banking validate_boat_turn_entry validate_boat_profile
    validate_boat_environment validate_boat_replay validate_boat_world wake_baseline
    boat_water_review boat_banking_review boat_flag_review boat_fleet_review)
  target_compile_options(${target} PRIVATE ${PROTEUS_WARNINGS})
endforeach()
foreach(target validate_boat_combat validate_boat_fleet validate_boat_match validate_boat_race boat_server boat_bot validate_boat_net)
  target_compile_options(${target} PRIVATE ${PROTEUS_WARNINGS})
endforeach()
# Validators and review tools compare packages against the shipped fixture values.
foreach(target validate_boat validate_boat_banking validate_boat_clock validate_boat_world
    validate_boat_replay validate_boat_core validate_boat_voyage validate_boat_plugin
    validate_boat_plugin_session validate_boat_plugin_catalog validate_boat_minimap
    validate_boat_combat validate_boat_match validate_boat_plugin_race validate_boat_race
    validate_boat_race_plugin validate_boat_net validate_boat_net_enet validate_boat_room_package
    boat_race_trial boat_hud_review boat_fleet_review)
  target_link_libraries(${target} PRIVATE proteus_boat_fixtures)
endforeach()
