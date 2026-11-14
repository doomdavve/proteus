# The dedicated room server.

# M3: the authority process and a scripted network client link the ENet transport; the
# protocol/session validator links only the display- and socket-free net library.
add_executable(boat_server boat_server_main.c ${PROJECT_SOURCE_DIR}/src/room/boat_server.c ${PROJECT_SOURCE_DIR}/src/platform/cli_usage.c
  ${PROJECT_SOURCE_DIR}/src/room/boat_room_package.c ${PROJECT_SOURCE_DIR}/src/room/boat_room_objective.c)
foreach(target boat_server boat_bot)
  target_link_libraries(${target} PRIVATE proteus_boat_net_enet)
endforeach()
# The authority evaluates room packages, but proteus_boat_net itself never links Lua.
target_link_libraries(boat_server PRIVATE proteus_plugins proteus_plugin_catalog)

# P5g: explicit room-objective checkpoints; data codecs only, no evaluator or files.
add_library(proteus_room_record_objective STATIC
  ${PROJECT_SOURCE_DIR}/src/room/boat_room_record_objective.c)
target_link_libraries(proteus_room_record_objective PUBLIC proteus_boat_net proteus_plugin_data)
target_compile_options(proteus_room_record_objective PRIVATE ${PROTEUS_WARNINGS})

# P5g: authority recordings and no-Lua reconstruction share the bounded byte codec.
add_library(proteus_session_record STATIC
  ${PROJECT_SOURCE_DIR}/src/room/boat_session_record.c)
target_link_libraries(proteus_session_record PUBLIC proteus_room_record_objective)
target_compile_options(proteus_session_record PRIVATE ${PROTEUS_WARNINGS})
target_link_libraries(boat_server PRIVATE proteus_session_record)
foreach(target wake_velocity wake_study)
  if(TARGET ${target})
    target_link_libraries(${target} PRIVATE proteus_session_record)
  endif()
endforeach()
add_executable(validate_boat_session_record ${PROJECT_SOURCE_DIR}/tools/validate_boat_session_record.c
  ${PROJECT_SOURCE_DIR}/src/room/boat_room_objective.c)
target_link_libraries(validate_boat_session_record PRIVATE proteus_session_record proteus_plugins)
target_compile_options(validate_boat_session_record PRIVATE ${PROTEUS_WARNINGS})

execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  OUTPUT_VARIABLE PROTEUS_RECORD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
target_compile_definitions(proteus_session_record PRIVATE
  PROTEUS_RECORD_BUILD="${PROTEUS_RECORD_COMMIT}/${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}/${CMAKE_SYSTEM_PROCESSOR}")
