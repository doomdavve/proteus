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
