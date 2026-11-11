# The voyage objective transaction shared by the direct path and the local authority (L1b).
add_library(proteus_wake_objective STATIC wake_velocity_objective.c
  wake_velocity_objective.h)
target_include_directories(proteus_wake_objective PUBLIC ${PROJECT_SOURCE_DIR}/tools)
target_link_libraries(proteus_wake_objective PUBLIC proteus_plugins)
target_compile_options(proteus_wake_objective PRIVATE ${PROTEUS_WARNINGS})

# Shared wake_velocity drive, state hashing and profile records for the study, benchmark and
# validators; executable-specific audio, music and asset paths stay with their consumers.
add_library(proteus_boat_drive STATIC
  wake_velocity_drive.c wake_velocity_state.c boat_profile.c)
target_link_libraries(proteus_boat_drive PUBLIC proteus_boat_session)
target_compile_options(proteus_boat_drive PRIVATE ${PROTEUS_WARNINGS})

# G0w report metadata: keep the revision and configuration explicit. Multi-config generators
# leave CMAKE_BUILD_TYPE empty, so record that fact instead of guessing a configuration.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  OUTPUT_VARIABLE PROTEUS_BUILD_REV OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT PROTEUS_BUILD_REV)
  set(PROTEUS_BUILD_REV unknown)
endif()
execute_process(COMMAND git status --porcelain --untracked-files=no
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE PROTEUS_BUILD_DIRTY)
if(PROTEUS_BUILD_DIRTY)
  string(APPEND PROTEUS_BUILD_REV "-dirty")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(PROTEUS_BUILD_CONFIG "multi-config")
else()
  set(PROTEUS_BUILD_CONFIG "${CMAKE_BUILD_TYPE}")
endif()

add_executable(wake_velocity wake_velocity.c wake_velocity_input.c
  wake_velocity_menu.c
  wake_velocity_lighting_review.c wake_velocity_lighting_review.h
  wake_velocity_work.h wake_velocity_scenery.h wake_velocity_app.h
  wake_velocity_controls.h
  boat_title.c
  ${PROJECT_SOURCE_DIR}/src/platform/cli_usage.c wake_velocity_performance.c ${PROJECT_SOURCE_DIR}/src/render/profiler.c ${PROJECT_SOURCE_DIR}/src/audio/boat_audio.c
  ${PROJECT_SOURCE_DIR}/src/audio/xm_audio.c ${PROJECT_SOURCE_DIR}/src/platform/boat_capture.c)
target_link_libraries(wake_velocity PRIVATE proteus_boat_drive proteus_gpu proteus_frame_pacer
  proteus_work_save)
# WORKING HARBOUR steps the work package's Lua, so it is native-only: the browser game links stubs
# (tools/check_web_wasm.py proves it carries no Lua).
if(EMSCRIPTEN)
  target_sources(wake_velocity PRIVATE wake_velocity_work_web.c
    wake_velocity_scenery_web.c wake_velocity_app_web.c
    wake_console_bridge_web.c wake_velocity_controls_web.c)
else()
  target_sources(wake_velocity PRIVATE wake_velocity_work.c
    wake_velocity_scenery.c wake_velocity_app.c wake_console_bridge.c
    wake_velocity_controls.c)
  target_link_libraries(wake_velocity PRIVATE proteus_work_session proteus_work_local)
endif()
target_include_directories(wake_velocity PRIVATE ${raylib_SOURCE_DIR}/src)
target_compile_options(wake_velocity PRIVATE ${PROTEUS_WARNINGS})
target_compile_definitions(wake_velocity PRIVATE
  WAKE_PERFORMANCE_REVISION="${PROTEUS_BUILD_REV}"
  WAKE_PERFORMANCE_BUILD_CONFIG="${PROTEUS_BUILD_CONFIG}"
  WAKE_PERFORMANCE_PLATFORM="${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}"
  WAKE_PERFORMANCE_COMPILER="${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}")
# Share the web packager's version convention. Source archives can supply an explicit value.
set(PROTEUS_VERSION "" CACHE STRING "Build version displayed on the title (empty uses Git)")
if(PROTEUS_VERSION STREQUAL "")
  execute_process(COMMAND git describe --tags --match "v[0-9]*" --always --dirty
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE PROTEUS_TITLE_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(PROTEUS_TITLE_VERSION STREQUAL "")
    set(PROTEUS_TITLE_VERSION "unknown")
  endif()
else()
  set(PROTEUS_TITLE_VERSION "${PROTEUS_VERSION}")
endif()
# The cache override is text, including when a source archive has no Git metadata.
string(REPLACE "\\" "\\\\" PROTEUS_TITLE_VERSION "${PROTEUS_TITLE_VERSION}")
string(REPLACE "\"" "\\\"" PROTEUS_TITLE_VERSION "${PROTEUS_TITLE_VERSION}")
configure_file(${PROJECT_SOURCE_DIR}/cmake/boat_version.h.in ${PROJECT_BINARY_DIR}/generated/boat_version.h @ONLY)
target_include_directories(wake_velocity PRIVATE "${PROJECT_BINARY_DIR}/generated")

# AR6 (L0): private single player as a local authority and client, native and (L4a) browser. The
# glue is a library so validate_boat_local drives the game's own code.
add_library(proteus_local_drive STATIC wake_velocity_local.c wake_velocity_local.h
  wake_velocity_course.c wake_velocity_course.h wake_velocity_voyage.c
  wake_velocity_voyage.h)
target_include_directories(proteus_local_drive PUBLIC ${PROJECT_SOURCE_DIR}/tools)
target_link_libraries(proteus_local_drive PUBLIC proteus_boat_drive proteus_boat_net
  proteus_wake_objective)
target_compile_options(proteus_local_drive PRIVATE ${PROTEUS_WARNINGS})
target_link_libraries(wake_velocity PRIVATE proteus_local_drive)

if(NOT EMSCRIPTEN)
  target_link_libraries(wake_velocity PRIVATE proteus_boat_net_enet)
  target_compile_definitions(wake_velocity PRIVATE PROTEUS_BOAT_NET=1)
  target_sources(wake_velocity PRIVATE boat_directory.c boat_directory.h
    wake_velocity_servers.c wake_velocity_servers.h)
  target_link_libraries(wake_velocity PRIVATE CURL::libcurl Threads::Threads proteus_json)
endif()

if(NOT EMSCRIPTEN)
  # The game hosts a room by running itself as `--serve`, boat_server's command line.
  target_sources(wake_velocity PRIVATE wake_velocity_net.c wake_velocity_net.h
    wake_velocity_host.c wake_velocity_host.h ${PROJECT_SOURCE_DIR}/src/room/boat_server.c ${PROJECT_SOURCE_DIR}/src/room/boat_server.h
    ${PROJECT_SOURCE_DIR}/src/room/boat_room_package.c ${PROJECT_SOURCE_DIR}/src/room/boat_room_package.h
    ${PROJECT_SOURCE_DIR}/src/room/boat_room_objective.c ${PROJECT_SOURCE_DIR}/src/room/boat_room_objective.h)
endif()

# Shared descriptor transactions; browser Lua evaluation is restricted to its isolated worker.
target_sources(wake_velocity PRIVATE wake_velocity_plugin.c wake_velocity_plugin.h
  wake_command.c wake_command.h wake_console.c wake_console.h
  wake_console_bridge.h)
target_link_libraries(wake_velocity PRIVATE proteus_plugins proteus_wake_objective proteus_plugin_catalog)
if(NOT EMSCRIPTEN)
  target_sources(wake_velocity PRIVATE wake_velocity_bridge.c wake_velocity_bridge.h)
endif()

if(EMSCRIPTEN)
  target_link_libraries(wake_velocity PRIVATE proteus_boat_net)
  target_compile_definitions(wake_velocity PRIVATE PROTEUS_BOAT_NET=1)
  target_sources(wake_velocity PRIVATE ${PROJECT_SOURCE_DIR}/src/net/boat_net_web.c wake_velocity_net.c
    wake_velocity_servers_web.c wake_velocity_plugin_web.c)
endif()

# Native only: the browser build returns before these in the project root.

if(NOT EMSCRIPTEN)
add_dependencies(wake_velocity proteus_assets)
# D6 (ledger L8): developer studies (--wake-field, --player-wake, --no-player-wake,
# --wake-objects-crossing) run on the direct path in wake_study, the game's own sources and
# settings with WAKE_VELOCITY_STUDY. The game rejects those options and names this tool. It is a
# developer build only: no package or release carries it.
if(NOT EMSCRIPTEN)
  get_target_property(WAKE_STUDY_SOURCES wake_velocity SOURCES)
  add_executable(wake_study ${WAKE_STUDY_SOURCES})
  foreach(property LINK_LIBRARIES INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
    get_target_property(value wake_velocity ${property})
    if(value)
      set_property(TARGET wake_study PROPERTY ${property} "${value}")
    endif()
  endforeach()
  target_compile_definitions(wake_study PRIVATE WAKE_VELOCITY_STUDY=1)
  add_dependencies(wake_study proteus_assets)
endif()

if(NOT EMSCRIPTEN)
# P4 native isolated authoring validator; intentionally absent from the browser runtime.
  add_executable(boat_plugin_worker boat_plugin_worker.c wake_velocity_plugin.c)
  target_link_libraries(boat_plugin_worker PRIVATE proteus_plugins proteus_wake_objective proteus_plugin_catalog proteus_boat_session)
  target_compile_options(boat_plugin_worker PRIVATE ${PROTEUS_WARNINGS})
endif()
endif()
