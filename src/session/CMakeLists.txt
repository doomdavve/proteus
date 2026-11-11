# SV1b: the sandbox save's byte format. Buffers only: no files and no Lua (plugin data only).
add_library(proteus_sandbox_save STATIC boat_sandbox_save.c boat_sandbox_save.h)
target_link_libraries(proteus_sandbox_save PUBLIC proteus_plugin_data)
target_compile_options(proteus_sandbox_save PRIVATE ${PROTEUS_WARNINGS})

add_library(proteus_work_session STATIC boat_work_session.c boat_work_session.h)
# The work session steps the package's objective, so it links the Lua host.
target_link_libraries(proteus_work_session PUBLIC proteus_sandbox_save proteus_plugins
  proteus_boat_core)
target_compile_options(proteus_work_session PRIVATE ${PROTEUS_WARNINGS})

# L3: the working harbour's private local endpoint (authority session, client mirror, memory
# transport). It links the work session's Lua; proteus_boat_net never links it.
add_library(proteus_work_local STATIC boat_work_local.c boat_work_local.h)
target_link_libraries(proteus_work_local PUBLIC proteus_work_session proteus_boat_net)
target_compile_options(proteus_work_local PRIVATE ${PROTEUS_WARNINGS})

add_library(proteus_work_save STATIC boat_work_save.c boat_work_save.h
  boat_work_menu.c boat_work_menu.h)
target_link_libraries(proteus_work_save PUBLIC proteus_work_session proteus_file)
target_compile_options(proteus_work_save PRIVATE ${PROTEUS_WARNINGS})

# Single-player session orchestration and replay persistence.
add_library(proteus_boat_session STATIC boat_replay.c boat_session.c)
target_link_libraries(proteus_boat_session PUBLIC proteus_render proteus_file)
target_compile_options(proteus_boat_session PRIVATE ${PROTEUS_WARNINGS})
