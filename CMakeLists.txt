cmake_minimum_required(VERSION 3.24)
# Link each static library once where the linker resolves them in any order (Apple ld warns
# about repeats, e.g. proteus_json from both the game and the plugin host).
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()
project(proteus LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PROTEUS_WEBRTC "Build the optional native browser multiplayer endpoint" OFF)

option(PROTEUS_GPU_TESTS "Enable GPU-context CTest cases" OFF)
option(PROTEUS_WAKE_FIELD_TESTS "Enable retired physical wake-field and prototype experiments" OFF)
option(PROTEUS_OPTIMIZE_TESTS "Optimize numerical validators in Debug, retaining assertions" ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
  # GLFW selects native Wayland in Wayland sessions, with X11 available for X11 desktops.
  # Keep these cache options overridable for builds targeting only one window system.
  option(GLFW_BUILD_WAYLAND "Build support for Wayland" ON)
  option(GLFW_BUILD_X11 "Build support for X11" ON)
endif()

if(EMSCRIPTEN)
  set(PLATFORM Web CACHE STRING "" FORCE)
  set(OPENGL_VERSION "ES 3.0" CACHE STRING "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

# Round every float operation separately on every platform. Clang fuses multiply-adds on ARM by
# default, which changed map and scenery digests between macOS and x86 Linux. Set before raylib
# so its compiled raymath rounds like the inline copies in our validators.
add_compile_options($<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-ffp-contract=off>)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 6.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN AND GLFW_BUILD_WAYLAND)
  include(cmake/PatchGlfwWayland.cmake)
  proteus_patch_glfw_wayland("${raylib_SOURCE_DIR}/src/external/glfw/src/wl_init.c")
endif()

if(APPLE)
  link_libraries("-framework Accelerate") # Double cosine backend; portable FFT elsewhere.
endif()

set(PROTEUS_WARNINGS $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)

# Boating code links from static libraries, once per executable, without cycles:
# core <- render <- gpu, and render <- session <- drive.
# Core: clock, physics, pose, locations/world/shoreline, water queries, wakes, course crossing,
# harbor/traffic contacts and boat/city geometry data. It must not draw, read input, open
# windows or audio, or touch files; boat_core_boundary checks its sources and symbols.
set(PROTEUS_BOAT_CORE_SOURCES
  src/boat_vessel_wake.c src/boat_vessel_wake.h
  src/boat.c src/boat.h src/boat_clock.h src/boat_course.c src/boat_course.h
  src/boat_combat.c src/boat_combat.h src/boat_fleet.c src/boat_fleet.h src/boat_match.c src/boat_match.h src/boat_race.c src/boat_race.h src/boat_location.c src/boat_location.h src/boat_map.c
  src/boat_map.h src/boat_map_tiles.c src/boat_map_tiles.h
  src/boat_model.c src/boat_model.h src/boat_pose.c src/boat_pose.h
  src/boat_scene_definition.c src/boat_scene_definition.h
  src/waterfront_flock.c src/waterfront_flock.h
  src/boat_input.h src/boat_reset_hold.h src/boat_throttle.h src/boat_steering.h src/boat_view_state.h src/boat_wake.c src/boat_wake.h src/boat_wake_objects.c
  src/boat_wake_objects.h src/boat_wake_player.c src/boat_wake_player.h src/boat_water.c
  src/boat_voyage.c src/boat_voyage.h src/boat_water.h src/boat_world.c src/boat_world.h
  src/geometry3d.h
  src/waterfront_city.c src/waterfront_city.h src/waterfront_harbor.c src/waterfront_harbor.h
  src/waterfront_outer.c src/waterfront_outer.h
  src/waterfront_route.c src/waterfront_route.h
  src/waterfront_traffic.c src/waterfront_traffic.h
  src/waterfront_upstream.c src/waterfront_upstream.h)
add_library(proteus_boat_core STATIC ${PROTEUS_BOAT_CORE_SOURCES})
target_include_directories(proteus_boat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Types and raymath only; Debug builds resolve raymath's out-of-line definitions from raylib.
target_link_libraries(proteus_boat_core PUBLIC raylib)
target_compile_options(proteus_boat_core PRIVATE ${PROTEUS_WARNINGS})

# Scenery packs: each mapped location's whole bake (assets/geometry/<location>/scenery.bin), loaded
# at run time for presentation. The core keeps only the compiled water, so it never reads them.
add_library(proteus_boat_scenery STATIC src/boat_scenery.c src/boat_scenery.h)
target_link_libraries(proteus_boat_scenery PUBLIC proteus_boat_core)
# Validators and review tools load the packs from the source tree (tools/boat_test_scenery.h).
target_compile_definitions(proteus_boat_scenery INTERFACE PROTEUS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(proteus_boat_scenery PRIVATE ${PROTEUS_WARNINGS})

# Strict, bounded JSON over the vendored jsmn tokenizer (MIT, third_party/jsmn/LICENSE): the
# server directory and plugin manifests.
add_library(proteus_json STATIC src/boat_json.c src/boat_json.h)
target_include_directories(proteus_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(proteus_json PRIVATE ${PROTEUS_WARNINGS})

# Lua plugins P0 (docs/PLUGIN_PLAN.md, docs/PLUGIN_API.md). Lua 5.4.9 (MIT; notice at the end of
# lua.h), pinned by lua.org's published SHA-256. Only the VM and the base, string, table and math
# libraries are compiled: io, os, debug, package/require, coroutine and utf8 do not exist. A fixed
# string-hash seed keeps table traversal, and so diagnostics, identical from run to run.
FetchContent_Declare(
  lua
  URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
  URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua)
# Lua's MIT notice, from the end of lua.h, for the native packages (lua-LICENSE.txt).
file(READ ${lua_SOURCE_DIR}/src/lua.h PROTEUS_LUA_HEADER)
string(REGEX MATCH "\\* Copyright \\(C\\)[^\n]*\n(\\*[^\n]*\n)*" PROTEUS_LUA_NOTICE
  "${PROTEUS_LUA_HEADER}")
string(REGEX REPLACE "(^|\n)\\*+ ?" "\\1" PROTEUS_LUA_NOTICE "${PROTEUS_LUA_NOTICE}")
string(STRIP "${PROTEUS_LUA_NOTICE}" PROTEUS_LUA_NOTICE)
if(NOT PROTEUS_LUA_NOTICE MATCHES "^Copyright \\(C\\) 1994-[0-9]+ Lua.org, PUC-Rio\\..*NONINFRINGEMENT")
  message(FATAL_ERROR "Cannot find Lua's license notice in lua.h")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/lua-LICENSE.txt "${PROTEUS_LUA_NOTICE}\n")
set(PROTEUS_LUA_SOURCES lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes
  lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lstrlib ltablib lmathlib)
list(TRANSFORM PROTEUS_LUA_SOURCES PREPEND ${lua_SOURCE_DIR}/src/)
list(TRANSFORM PROTEUS_LUA_SOURCES APPEND .c)
add_library(proteus_lua STATIC ${PROTEUS_LUA_SOURCES})
target_include_directories(proteus_lua SYSTEM PUBLIC ${lua_SOURCE_DIR}/src)
set_target_properties(proteus_lua PROPERTIES C_STANDARD 99)
target_compile_options(proteus_lua PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-w;-include;${CMAKE_CURRENT_SOURCE_DIR}/cmake/proteus_lua.h>
  $<$<C_COMPILER_ID:MSVC>:/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/proteus_lua.h>)
if(NOT MSVC)
  target_link_libraries(proteus_lua PRIVATE m)
endif()

# The plugin host: manifest, restricted Lua evaluation and voyage decoding over immutable bytes,
# plus boat_plugin_io for reading a package directory. Core and net never link it.
add_library(proteus_plugins STATIC src/boat_plugin.c src/boat_plugin.h src/boat_plugin_lua.c
  src/boat_plugin_lua.h src/boat_plugin_io.c src/boat_plugin_io.h src/boat_plugin_host.c
  src/boat_plugin_host.h src/boat_plugin_race.c src/boat_plugin_state.c src/boat_plugin_state.h)
target_link_libraries(proteus_plugins PUBLIC proteus_boat_core PRIVATE proteus_json proteus_lua)
target_compile_options(proteus_plugins PRIVATE ${PROTEUS_WARNINGS})
target_sources(proteus_plugins PRIVATE src/boat_plugin_stage.c src/boat_plugin_stage.h
  src/boat_plugin_eval.c src/boat_plugin_eval.h)

# Presentation: cosmetic simulation, CPU renderer, boat/course views, water meshes and scenes.
option(PROTEUS_WARM_MENU "Warm daylight menus (OFF selects the cyan dusk menus)" ON)
add_library(proteus_render STATIC
  src/boat_ui.c src/boat_ui.h
  src/boat_hud.c src/boat_hud.h src/boat_cabin.c src/boat_cabin.h src/boat_skin.c src/boat_skin.h src/boat_course_view.c
  src/boat_voyage_view.c src/boat_voyage_view.h src/boat_minimap.c src/boat_minimap.h
  src/boat_flag.c src/boat_presentation.c src/boat_view.c
  src/boat_water_mesh.c src/renderer3d.c src/waterfront.c src/waterfront_corner.c
  src/waterfront_scene.c)
target_link_libraries(proteus_render PUBLIC proteus_boat_core proteus_boat_scenery)
target_compile_definitions(proteus_render PUBLIC PROTEUS_WARM_MENU=$<BOOL:${PROTEUS_WARM_MENU}>)
target_compile_options(proteus_render PRIVATE ${PROTEUS_WARNINGS})

# GPU renderers, linked only by graphics executables.
add_library(proteus_gpu STATIC src/boat_gpu.c src/renderer_gpu.c src/boat_rearview.c src/boat_rearview.h)
target_link_libraries(proteus_gpu PUBLIC proteus_render)
target_compile_options(proteus_gpu PRIVATE ${PROTEUS_WARNINGS})

# Single-player session orchestration and replay persistence.
add_library(proteus_boat_session STATIC src/boat_replay.c src/boat_session.c)
target_link_libraries(proteus_boat_session PUBLIC proteus_render)
target_compile_options(proteus_boat_session PRIVATE ${PROTEUS_WARNINGS})

# Shared wake_velocity drive, state hashing and profile records for the study, benchmark and
# validators; executable-specific audio, music and asset paths stay with their consumers.
add_library(proteus_boat_drive STATIC
  tools/wake_velocity_drive.c tools/wake_velocity_state.c src/boat_profile.c)
target_link_libraries(proteus_boat_drive PUBLIC proteus_boat_session)
target_compile_options(proteus_boat_drive PRIVATE ${PROTEUS_WARNINGS})

# Absolute-deadline frame pacing with a vsync backstop for windowed loops (sleeps; not core).
add_library(proteus_frame_pacer STATIC src/frame_pacer.c src/frame_pacer.h)
target_include_directories(proteus_frame_pacer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(proteus_frame_pacer PUBLIC raylib)
target_compile_options(proteus_frame_pacer PRIVATE ${PROTEUS_WARNINGS})
add_executable(validate_frame_pacer tools/validate_frame_pacer.c)
target_link_libraries(validate_frame_pacer PRIVATE proteus_frame_pacer)
target_compile_options(validate_frame_pacer PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME frame_pacer COMMAND validate_frame_pacer)

add_executable(wake_velocity tools/wake_velocity.c tools/wake_velocity_menu.c tools/boat_title.c
  tools/cli_usage.c src/profiler.c src/boat_audio.c src/xm_audio.c)
target_link_libraries(wake_velocity PRIVATE proteus_boat_drive proteus_gpu proteus_frame_pacer)
target_compile_options(wake_velocity PRIVATE ${PROTEUS_WARNINGS})
# Share the web packager's version convention. Source archives can supply an explicit value.
set(PROTEUS_VERSION "" CACHE STRING "Build version displayed on the title (empty uses Git)")
if(PROTEUS_VERSION STREQUAL "")
  execute_process(COMMAND git describe --tags --match "v[0-9]*" --always --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
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
configure_file(cmake/boat_version.h.in generated/boat_version.h @ONLY)
target_include_directories(wake_velocity PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Race R0: measured shortened river route, source contracts, captures and playable trial.
add_executable(boat_race_trial tools/boat_race_trial.c tools/boat_vessel_wake_gpu.c
  tools/boat_race_plugin.c tools/boat_race_plugin.h)
target_include_directories(boat_race_trial PRIVATE ${raylib_SOURCE_DIR}/src)
target_link_libraries(boat_race_trial PRIVATE proteus_boat_drive proteus_gpu proteus_plugins)
target_compile_options(boat_race_trial PRIVATE ${PROTEUS_WARNINGS})

# Shared multiplayer codec, authority/client sessions, prediction and memory/multiplexed
# transports obey the core boundary. Native ENet, optional native WebRTC and the browser
# bridge own platform networking separately.
set(PROTEUS_BOAT_NET_SOURCES
  src/boat_net_mux.c src/boat_net_mux.h
  src/boat_net_client.c src/boat_net_client.h src/boat_net_memory.c src/boat_net_protocol.c
  src/boat_net_protocol.h src/boat_net_server.c src/boat_net_server.h src/boat_net_transport.h
  src/boat_prediction.c src/boat_prediction.h src/boat_remote_view.c src/boat_remote_view.h)
add_library(proteus_boat_net STATIC ${PROTEUS_BOAT_NET_SOURCES})
target_link_libraries(proteus_boat_net PUBLIC proteus_boat_core)
target_compile_options(proteus_boat_net PRIVATE ${PROTEUS_WARNINGS})

if(NOT EMSCRIPTEN)
  # ENet v1.3.18 (MIT, LICENSE in its source tree), pinned to the tag's commit. Its sources are
  # compiled here as a static library with its own configure checks and no project warnings.
  FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG 2662c0de09e36f2a2030ccc2c528a3e4c9e8138a # v1.3.18
    SOURCE_SUBDIR no-cmake-build # Fetch only; the target is defined below.
  )
  FetchContent_MakeAvailable(enet)
  add_library(proteus_enet STATIC
    ${enet_SOURCE_DIR}/callbacks.c ${enet_SOURCE_DIR}/compress.c ${enet_SOURCE_DIR}/host.c
    ${enet_SOURCE_DIR}/list.c ${enet_SOURCE_DIR}/packet.c ${enet_SOURCE_DIR}/peer.c
    ${enet_SOURCE_DIR}/protocol.c ${enet_SOURCE_DIR}/unix.c ${enet_SOURCE_DIR}/win32.c)
  target_include_directories(proteus_enet SYSTEM PUBLIC ${enet_SOURCE_DIR}/include)
  set_target_properties(proteus_enet PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)
  if(WIN32)
    target_link_libraries(proteus_enet PUBLIC ws2_32 winmm)
  else()
    include(CheckFunctionExists)
    include(CheckStructHasMember)
    include(CheckTypeSize)
    foreach(function fcntl poll getaddrinfo getnameinfo gethostbyname_r gethostbyaddr_r
        inet_pton inet_ntop)
      string(TOUPPER "HAS_${function}" variable)
      check_function_exists(${function} ${variable})
      if(${variable})
        target_compile_definitions(proteus_enet PRIVATE ${variable}=1)
      endif()
    endforeach()
    check_struct_has_member("struct msghdr" "msg_flags" "sys/types.h;sys/socket.h"
      HAS_MSGHDR_FLAGS)
    set(CMAKE_EXTRA_INCLUDE_FILES "sys/types.h" "sys/socket.h")
    check_type_size("socklen_t" HAS_SOCKLEN_T BUILTIN_TYPES_ONLY)
    unset(CMAKE_EXTRA_INCLUDE_FILES)
    foreach(variable HAS_MSGHDR_FLAGS HAS_SOCKLEN_T)
      if(${variable})
        target_compile_definitions(proteus_enet PRIVATE ${variable}=1)
      endif()
    endforeach()
  endif()
  target_compile_options(proteus_enet PRIVATE $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-w>)

  add_library(proteus_boat_net_enet STATIC src/boat_net_enet.c src/boat_net_enet.h)
  target_link_libraries(proteus_boat_net_enet PUBLIC proteus_boat_net PRIVATE proteus_enet)
  target_compile_options(proteus_boat_net_enet PRIVATE ${PROTEUS_WARNINGS})
  target_link_libraries(wake_velocity PRIVATE proteus_boat_net_enet)
  target_compile_definitions(wake_velocity PRIVATE PROTEUS_BOAT_NET=1)
  find_package(CURL REQUIRED)
  find_package(Threads REQUIRED)
  target_sources(wake_velocity PRIVATE tools/boat_directory.c tools/boat_directory.h)
  target_link_libraries(wake_velocity PRIVATE CURL::libcurl Threads::Threads proteus_json)
  add_executable(validate_boat_directory tools/validate_boat_directory.c tools/boat_directory.c)
  target_link_libraries(validate_boat_directory PRIVATE proteus_boat_net_enet CURL::libcurl
    Threads::Threads proteus_json)
  target_compile_options(validate_boat_directory PRIVATE ${PROTEUS_WARNINGS})
  add_test(NAME boat_directory COMMAND validate_boat_directory)
  set_tests_properties(boat_directory PROPERTIES LABELS "cpu;quick")
  # The game hosts a room by running itself as `--serve`, boat_server's command line.
  target_sources(wake_velocity PRIVATE tools/wake_velocity_net.c tools/wake_velocity_net.h
    tools/wake_velocity_host.c tools/wake_velocity_host.h tools/boat_server.c tools/boat_server.h)
endif()

if(NOT EMSCRIPTEN)
  add_executable(boat_combat_review tools/boat_combat_review.c tools/wake_velocity_net.c src/boat_audio.c)
  target_link_libraries(boat_combat_review PRIVATE proteus_boat_drive proteus_gpu proteus_boat_net_enet)
  target_compile_options(boat_combat_review PRIVATE ${PROTEUS_WARNINGS})
endif()

# Shared descriptor transactions; browser Lua evaluation is restricted to its isolated worker.
target_sources(wake_velocity PRIVATE tools/wake_velocity_plugin.c tools/wake_velocity_plugin.h)
target_link_libraries(wake_velocity PRIVATE proteus_plugins)
target_compile_definitions(wake_velocity PRIVATE PROTEUS_BOAT_PLUGINS=1)
if(NOT EMSCRIPTEN)
  target_sources(wake_velocity PRIVATE tools/wake_velocity_bridge.c tools/wake_velocity_bridge.h)
endif()

if(EMSCRIPTEN)
  target_link_libraries(wake_velocity PRIVATE proteus_boat_net)
  target_compile_definitions(wake_velocity PRIVATE PROTEUS_BOAT_NET=1)
  target_sources(wake_velocity PRIVATE src/boat_net_web.c tools/wake_velocity_net.c)
  include(cmake/Web.cmake)
  return() # Desktop studies and CTest tools remain native targets.
endif()


add_custom_target(proteus_assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:wake_velocity>/assets
)

# Reproducible, scenery-only title sample at the original bridge-lookback camera.
add_executable(title_backdrop tools/title_backdrop.c)
target_link_libraries(title_backdrop PRIVATE proteus_gpu)
target_compile_options(title_backdrop PRIVATE ${PROTEUS_WARNINGS})

add_executable(gpu_fixture tools/gpu_fixture.c)
target_include_directories(gpu_fixture PRIVATE ${raylib_SOURCE_DIR}/src)
target_link_libraries(gpu_fixture PRIVATE proteus_gpu)
target_compile_definitions(gpu_fixture PRIVATE
  PROTEUS_ASSET_DIR="$<TARGET_FILE_DIR:gpu_fixture>/assets"
)
target_compile_options(gpu_fixture PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_dependencies(gpu_fixture proteus_assets)

add_executable(validate_xm
  tools/validate_xm.c
  src/xm_audio.c
)
target_include_directories(validate_xm PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${raylib_SOURCE_DIR}/src
  ${raylib_SOURCE_DIR}/src/external
)
if(NOT MSVC)
  target_link_libraries(validate_xm PRIVATE m)
endif()

enable_testing()
include(cmake/CaseTests.cmake)
add_executable(validate_boat_wake tools/validate_boat_wake.c)
target_link_libraries(validate_boat_wake PRIVATE proteus_boat_core)
target_compile_options(validate_boat_wake PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
if(NOT MSVC)
  target_link_libraries(validate_boat_wake PRIVATE m)
endif()
add_test(NAME boat_wake_owned_field COMMAND validate_boat_wake)
add_executable(wake_field_review tools/wake_field_review.c)
target_link_libraries(wake_field_review PRIVATE proteus_boat_core)
target_compile_options(wake_field_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
if(NOT MSVC)
  target_link_libraries(wake_field_review PRIVATE m)
endif()
add_test(NAME wake_field_refinement COMMAND wake_field_review --check)
add_executable(wake_runtime_review tools/wake_runtime_review.c)
target_link_libraries(wake_runtime_review PRIVATE proteus_boat_core)
target_compile_options(wake_runtime_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
if(NOT MSVC)
  target_link_libraries(wake_runtime_review PRIVATE m)
endif()
add_test(NAME wake_runtime_cadence COMMAND wake_runtime_review --check)

# Compact representation diagnostic only; no player or renderer dependency.
add_executable(wake_packet_review tools/wake_packet_review.c)
target_link_libraries(wake_packet_review PRIVATE proteus_boat_core)
target_compile_options(wake_packet_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
if(NOT MSVC)
  target_link_libraries(wake_packet_review PRIVATE m)
endif()
add_test(NAME wake_packet_contracts COMMAND wake_packet_review --check)

add_executable(wake_candidate_review tools/wake_candidate_review.c)
target_link_libraries(wake_candidate_review PRIVATE proteus_boat_core)
target_compile_options(wake_candidate_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
if(NOT MSVC)
  target_link_libraries(wake_candidate_review PRIVATE m)
endif()
add_test(NAME wake_candidate_backend_queries COMMAND wake_candidate_review)
add_test(NAME wake_candidate_moving_source COMMAND wake_candidate_review --source)
set_tests_properties(wake_candidate_moving_source PROPERTIES TIMEOUT 600)
# Isolated desktop GL 3.3 experiment. It is deliberately not linked into wake_velocity.
add_executable(wake_gpu_runtime_review tools/wake_gpu_runtime_review.c)
target_link_libraries(wake_gpu_runtime_review PRIVATE proteus_boat_core)
target_compile_options(wake_gpu_runtime_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
if(PROTEUS_GPU_TESTS)
  add_test(NAME wake_gpu_runtime COMMAND wake_gpu_runtime_review --check)
  set_tests_properties(wake_gpu_runtime PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
endif()
add_executable(validate_boat_flag tools/validate_boat_flag.c)
target_link_libraries(validate_boat_flag PRIVATE proteus_render)
target_compile_options(validate_boat_flag PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME boat_flag_cloth COMMAND validate_boat_flag)
add_executable(validate_boat_audio tools/validate_boat_audio.c src/boat_audio.c)
target_include_directories(validate_boat_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(validate_boat_audio PRIVATE raylib)
target_compile_options(validate_boat_audio PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME boat_audio_is_valid COMMAND validate_boat_audio)

add_dependencies(wake_velocity proteus_assets)
add_dependencies(title_backdrop proteus_assets)
add_dependencies(boat_race_trial proteus_assets)
add_test(NAME boat_race_plugin_preview COMMAND boat_race_trial
  --plugin plugins/river-sprint --renderer cpu --output ${CMAKE_CURRENT_BINARY_DIR}/race-plugin.png --time 3)
set_tests_properties(boat_race_plugin_preview PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} LABELS "cpu;render;slow")
add_test(NAME boat_race_plugin_voyage_rejected COMMAND boat_race_trial
  --plugin plugins/langedrag --renderer cpu --output ${CMAKE_CURRENT_BINARY_DIR}/race-invalid.png)
set_tests_properties(boat_race_plugin_voyage_rejected PROPERTIES WILL_FAIL TRUE
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} LABELS "cpu;quick")
add_test(NAME boat_race_trial COMMAND boat_race_trial --check)
set_tests_properties(boat_race_trial PROPERTIES
  PASS_REGULAR_EXPRESSION "boat_race_trial=ok" LABELS "core;cpu")
set_tests_properties(boat_race_trial PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
if(PROTEUS_GPU_TESTS)
  add_test(NAME boat_race_trial_gpu COMMAND boat_race_trial --gpu-check)
  set_tests_properties(boat_race_trial_gpu PROPERTIES LABELS "gpu;render" RESOURCE_LOCK gpu
    PASS_REGULAR_EXPRESSION "ferry_visibility=ok" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
add_executable(wake_play_review tools/wake_play_review.c)
target_include_directories(wake_play_review PRIVATE ${raylib_SOURCE_DIR}/src)
target_link_libraries(wake_play_review PRIVATE proteus_boat_session proteus_gpu)
target_compile_options(wake_play_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_dependencies(wake_play_review proteus_assets)
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE BOAT_BENCH_REV OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT BOAT_BENCH_REV)
  set(BOAT_BENCH_REV unknown)
endif()
execute_process(COMMAND git status --porcelain --untracked-files=no
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE BOAT_BENCH_DIRTY)
if(BOAT_BENCH_DIRTY)
  string(APPEND BOAT_BENCH_REV "-dirty")
endif()
add_executable(boat_benchmark tools/boat_benchmark.c)
target_link_libraries(boat_benchmark PRIVATE proteus_boat_drive)
target_compile_definitions(boat_benchmark PRIVATE
  BOAT_BENCH_BUILD="${BOAT_BENCH_REV}/${CMAKE_BUILD_TYPE}/${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}"
  BOAT_BENCH_PLATFORM="${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
target_compile_options(boat_benchmark PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)

add_executable(validate_boat_drive tools/validate_boat_drive.c)
target_link_libraries(validate_boat_drive PRIVATE proteus_boat_drive)
target_compile_options(validate_boat_drive PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME boat_drive_extraction COMMAND validate_boat_drive)
add_executable(validate_boat_clock tools/validate_boat_clock.c)
target_link_libraries(validate_boat_clock PRIVATE proteus_boat_drive)
target_compile_options(validate_boat_clock PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
set(BOAT_CLOCK_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/boat-clock-test")
file(MAKE_DIRECTORY "${BOAT_CLOCK_TEST_DIR}")
proteus_add_case_tests(boat_clock validate_boat_clock LABELS cpu ARGS ${BOAT_CLOCK_TEST_DIR})
add_test(NAME boat_clock_soak_smoke
  COMMAND validate_boat_clock --soak 60 ${BOAT_CLOCK_TEST_DIR} --waves)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME release_command COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_cut_release.py)
set_tests_properties(release_command PROPERTIES LABELS "cpu")
add_test(NAME web_endpoint_configuration COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_configure_web_endpoint.py)
set_tests_properties(web_endpoint_configuration PROPERTIES LABELS "cpu")
add_test(NAME boat_benchmark_contract COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_boat_benchmark.py $<TARGET_FILE:boat_benchmark>)

add_executable(wake_objects_review tools/wake_objects_review.c)
target_include_directories(wake_objects_review PRIVATE ${raylib_SOURCE_DIR}/src)
target_link_libraries(wake_objects_review PRIVATE proteus_boat_session proteus_gpu)
target_compile_options(wake_objects_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_dependencies(wake_objects_review proteus_assets)
proteus_add_case_tests(wake_objects wake_objects_review LABELS cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
if(PROTEUS_GPU_TESTS)
  add_test(NAME wake_objects_gpu COMMAND wake_objects_review --gpu)
  set_tests_properties(wake_objects_gpu PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    LABELS "gpu" RESOURCE_LOCK gpu TIMEOUT 120)
endif()
add_test(NAME wake_objects_course_rejected COMMAND wake_velocity --wake-objects --course)
add_test(NAME wake_objects_mixed_rejected COMMAND wake_velocity --wake-objects --wake-field)
add_test(NAME wake_objects_lifetime_rejected COMMAND wake_velocity --wake-objects --wake-life-ticks 1921)
set_tests_properties(wake_objects_course_rejected wake_objects_mixed_rejected
  wake_objects_lifetime_rejected PROPERTIES WILL_FAIL TRUE)
add_test(NAME wake_candidate_boat COMMAND wake_play_review)
add_test(NAME wake_candidate_crossing COMMAND wake_play_review --crossing)
set_tests_properties(wake_candidate_boat PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
set_tests_properties(wake_candidate_crossing PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
if(PROTEUS_GPU_TESTS)
  add_test(NAME wake_candidate_gpu COMMAND wake_play_review --gpu)
  set_tests_properties(wake_candidate_gpu PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    LABELS "gpu" RESOURCE_LOCK gpu TIMEOUT 180)
endif()
add_test(NAME wake_candidate_course_rejected COMMAND wake_velocity --wake-field --course)
add_test(NAME wake_candidate_map_rejected COMMAND wake_velocity --wake-field --location river)
add_test(NAME wake_candidate_current_rejected COMMAND wake_velocity --wake-field --current)
set_tests_properties(wake_candidate_course_rejected wake_candidate_map_rejected
  wake_candidate_current_rejected PROPERTIES WILL_FAIL TRUE)
add_executable(validate_boat tools/validate_boat.c)
add_executable(validate_boat_banking tools/validate_boat_banking.c)
add_executable(validate_boat_turn_entry tools/validate_boat_turn_entry.c)
add_executable(validate_boat_profile tools/validate_boat_profile.c)
add_executable(validate_boat_environment tools/validate_boat_environment.c)
add_executable(validate_boat_replay tools/validate_boat_replay.c)
add_executable(validate_boat_world tools/validate_boat_world.c)
add_executable(wake_baseline tools/wake_baseline.c)
add_executable(boat_water_review tools/boat_water_review.c)
add_dependencies(boat_water_review proteus_assets)
target_compile_definitions(boat_water_review PRIVATE
  BOAT_BENCH_BUILD="${BOAT_BENCH_REV}/${CMAKE_BUILD_TYPE}/${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}"
  BOAT_BENCH_PLATFORM="${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
add_executable(boat_banking_review tools/boat_banking_review.c)
add_dependencies(boat_banking_review proteus_assets)
add_executable(validate_boat_skin tools/validate_boat_skin.c)
target_link_libraries(validate_boat_skin PRIVATE proteus_render)
target_compile_options(validate_boat_skin PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_skin COMMAND validate_boat_skin)
set_tests_properties(boat_skin PROPERTIES LABELS "render;cpu")
add_test(NAME boat_fleet_render COMMAND boat_fleet_review --check)
set_tests_properties(boat_fleet_render PROPERTIES LABELS "render;cpu"
  PASS_REGULAR_EXPRESSION "boat_fleet_render=ok")
find_program(PROTEUS_NODE_EXECUTABLE node)
if(PROTEUS_NODE_EXECUTABLE)
  add_test(NAME web_plugin_author COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_web_plugin_author.cjs)
  set_tests_properties(web_plugin_author PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_plugin_author=ok")
  add_test(NAME web_server_directory COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_web_server_directory.cjs)
  set_tests_properties(web_server_directory PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_server_directory=ok")
  add_test(NAME web_rtc_transport COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_web_rtc_transport.cjs)
  set_tests_properties(web_rtc_transport PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_rtc_transport=ok")
  add_test(NAME web_mobile_controls COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_web_mobile_controls.cjs)
  set_tests_properties(web_mobile_controls PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_mobile_controls=ok")
  add_test(NAME web_mouse_controls COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_web_mouse_controls.cjs)
  set_tests_properties(web_mouse_controls PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_mouse_controls=ok")
endif()

add_executable(boat_hud_review tools/boat_hud_review.c)
target_link_libraries(boat_hud_review PRIVATE proteus_render)
target_compile_options(boat_hud_review PRIVATE ${PROTEUS_WARNINGS})

add_executable(boat_flag_review tools/boat_flag_review.c)
add_dependencies(boat_flag_review proteus_assets)
add_executable(boat_fleet_review tools/boat_fleet_review.c)
add_dependencies(boat_fleet_review proteus_assets)
foreach(target validate_boat validate_boat_banking validate_boat_turn_entry validate_boat_replay
    validate_boat_environment)
  target_link_libraries(${target} PRIVATE proteus_boat_session)
endforeach()
target_link_libraries(validate_boat_profile PRIVATE proteus_boat_drive)
target_link_libraries(validate_boat_world PRIVATE proteus_boat_drive)
target_link_libraries(wake_baseline PRIVATE proteus_boat_core)
foreach(target boat_banking_review boat_flag_review boat_fleet_review)
  target_link_libraries(${target} PRIVATE proteus_boat_session proteus_gpu)
endforeach()
# The drive library adds boat_profile statistics to the session.
target_link_libraries(boat_water_review PRIVATE proteus_boat_drive proteus_gpu proteus_frame_pacer)
foreach(target validate_boat validate_boat_banking validate_boat_turn_entry validate_boat_profile
    validate_boat_environment validate_boat_replay validate_boat_world wake_baseline
    boat_water_review boat_banking_review boat_flag_review boat_fleet_review)
  target_compile_options(${target} PRIVATE ${PROTEUS_WARNINGS})
endforeach()
# Display-free core gate: links the core library alone and checks its source/symbol boundary.
add_executable(validate_boat_core tools/validate_boat_core.c)
target_link_libraries(validate_boat_core PRIVATE proteus_boat_core)
target_compile_options(validate_boat_core PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_core COMMAND validate_boat_core)
# Adventure A1.1: explicit map descriptions and independently owned collision caches.
add_executable(validate_boat_map tools/validate_boat_map.c)
target_link_libraries(validate_boat_map PRIVATE proteus_boat_scenery)
target_compile_options(validate_boat_map PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_map validate_boat_map LABELS core cpu)
# Adventure A3.1: the baked first outer-river slice answers the A0.3 route and shelter queries.
add_executable(validate_boat_outer_map tools/validate_boat_outer_map.c)
target_link_libraries(validate_boat_outer_map PRIVATE proteus_boat_scenery)
target_compile_options(validate_boat_outer_map PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_outer_map COMMAND validate_boat_outer_map)
set_tests_properties(boat_outer_map PROPERTIES PASS_REGULAR_EXPRESSION "boat_outer_map=ok")
# Play-testing rates preserve fixed-tick physics.
add_executable(validate_boat_time_control tools/validate_boat_time_control.c)
target_link_libraries(validate_boat_time_control PRIVATE proteus_boat_session)
target_compile_options(validate_boat_time_control PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_time_control COMMAND validate_boat_time_control)
# Adventure A3.4: the outer voyage's shelters, exposed water, arrival and recovery.
add_executable(validate_boat_voyage tools/validate_boat_voyage.c)
target_link_libraries(validate_boat_voyage PRIVATE proteus_boat_session)
target_compile_options(validate_boat_voyage PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_voyage COMMAND validate_boat_voyage)
set_tests_properties(boat_voyage PROPERTIES PASS_REGULAR_EXPRESSION "boat_voyage=ok")
# Lua plugins P0: manifest, restricted evaluation, voyage decoding and parity with the compiled
# route; its file cases work in a scratch directory under the build tree.
add_executable(validate_boat_plugin tools/validate_boat_plugin.c)
target_link_libraries(validate_boat_plugin PRIVATE proteus_plugins)
target_compile_options(validate_boat_plugin PRIVATE ${PROTEUS_WARNINGS})
set(BOAT_PLUGIN_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-test")
file(MAKE_DIRECTORY "${BOAT_PLUGIN_TEST_DIR}")
target_compile_definitions(validate_boat_plugin PRIVATE
  BOAT_PLUGIN_TEST_DIR="${BOAT_PLUGIN_TEST_DIR}")
proteus_add_case_tests(boat_plugin validate_boat_plugin LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# P3.1: isolated objective transactions; P3.2 live adapter checks are in boat_plugin_session.
add_executable(validate_boat_plugin_state tools/validate_boat_plugin_state.c)
target_link_libraries(validate_boat_plugin_state PRIVATE proteus_plugins)
target_compile_options(validate_boat_plugin_state PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_plugin_state validate_boat_plugin_state LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Lua plugins P1: staging against the map, reload policies on a live session, Undo, save polling
# on a virtual clock, and tick-for-tick parity with the compiled voyage.
# Its game_controls case drives wake_velocity's --plugin glue on files in a scratch directory.
add_executable(validate_boat_plugin_session tools/validate_boat_plugin_session.c
  tools/wake_velocity_plugin.c)
target_link_libraries(validate_boat_plugin_session PRIVATE proteus_plugins proteus_boat_session)
target_compile_options(validate_boat_plugin_session PRIVATE ${PROTEUS_WARNINGS})
set(BOAT_PLUGIN_SESSION_DIR "${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-session-test")
file(MAKE_DIRECTORY "${BOAT_PLUGIN_SESSION_DIR}")
target_compile_definitions(validate_boat_plugin_session PRIVATE
  BOAT_PLUGIN_SESSION_DIR="${BOAT_PLUGIN_SESSION_DIR}")
proteus_add_case_tests(boat_plugin_session validate_boat_plugin_session LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# P3.3b display-free objective inspection and native checkpoint/seek review.
add_executable(boat_plugin_review tools/boat_plugin_review.c tools/wake_velocity_plugin.c)
target_link_libraries(boat_plugin_review PRIVATE proteus_plugins proteus_boat_session)
target_compile_options(boat_plugin_review PRIVATE ${PROTEUS_WARNINGS})
# P4 native isolated authoring validator; intentionally absent from the browser runtime.
if(NOT EMSCRIPTEN)
  add_executable(boat_plugin_worker tools/boat_plugin_worker.c tools/wake_velocity_plugin.c)
  target_link_libraries(boat_plugin_worker PRIVATE proteus_plugins proteus_boat_session)
  target_compile_options(boat_plugin_worker PRIVATE ${PROTEUS_WARNINGS})
  add_executable(validate_boat_plugin_eval tools/validate_boat_plugin_eval.c)
  target_link_libraries(validate_boat_plugin_eval PRIVATE proteus_plugins)
  target_compile_options(validate_boat_plugin_eval PRIVATE ${PROTEUS_WARNINGS})
  add_test(NAME boat_plugin_eval COMMAND validate_boat_plugin_eval)
  set_tests_properties(boat_plugin_eval PROPERTIES LABELS "cpu;quick" TIMEOUT 10)
  add_executable(validate_boat_plugin_stage tools/validate_boat_plugin_stage.c
    tools/wake_velocity_plugin.c)
  target_link_libraries(validate_boat_plugin_stage PRIVATE proteus_plugins proteus_boat_session)
  target_compile_options(validate_boat_plugin_stage PRIVATE ${PROTEUS_WARNINGS})
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-stage-test")
  target_compile_definitions(validate_boat_plugin_stage PRIVATE
    BOAT_PLUGIN_STAGE_DIR="${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-stage-test")
  add_test(NAME boat_plugin_stage COMMAND validate_boat_plugin_stage)
  set_tests_properties(boat_plugin_stage PROPERTIES LABELS "cpu"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 30)
  # P4.6 live tool bridge over request files; --serve backs the client's end-to-end checks.
  add_executable(validate_wake_bridge tools/validate_wake_bridge.c tools/wake_velocity_bridge.c
    tools/wake_velocity_plugin.c)
  target_link_libraries(validate_wake_bridge PRIVATE proteus_plugins proteus_boat_session)
  target_compile_options(validate_wake_bridge PRIVATE ${PROTEUS_WARNINGS})
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wake-bridge-test")
  target_compile_definitions(validate_wake_bridge PRIVATE
    BOAT_BRIDGE_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/wake-bridge-test")
  add_test(NAME wake_velocity_bridge COMMAND validate_wake_bridge)
  set_tests_properties(wake_velocity_bridge PROPERTIES LABELS "cpu"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 30)
  add_test(NAME boat_plugin_author COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_plugin_author.py
    --worker $<TARGET_FILE:boat_plugin_worker>
    --bridge-server $<TARGET_FILE:validate_wake_bridge>)
  set_tests_properties(boat_plugin_author PROPERTIES LABELS "cpu" TIMEOUT 90)
  # P4.5 pinned frame captures through the game's CPU screenshot path, also from a P4.7
  # release layout of the authoring tools.
  add_test(NAME boat_plugin_capture COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_plugin_author.py
    --worker $<TARGET_FILE:boat_plugin_worker> --game $<TARGET_FILE:wake_velocity> Capture
    Packaged)
  set_tests_properties(boat_plugin_capture PROPERTIES LABELS "render" TIMEOUT 120)
endif()

# P3 runtime qualification: explicit measurements, never timing-sensitive CTest assertions.
add_executable(boat_plugin_bench tools/boat_plugin_bench.c)
target_link_libraries(boat_plugin_bench PRIVATE proteus_plugins proteus_boat_session)
target_compile_options(boat_plugin_bench PRIVATE ${PROTEUS_WARNINGS})
# Solo minimap: baked chart, projection, markers and trim; --screenshot writes review captures.
# Adventure A3.5i: scenery packs load once, match the compiled water and reject bad input.
add_executable(validate_boat_scenery tools/validate_boat_scenery.c)
target_link_libraries(validate_boat_scenery PRIVATE proteus_render)
target_compile_options(validate_boat_scenery PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_scenery COMMAND validate_boat_scenery)
set_tests_properties(boat_scenery PROPERTIES PASS_REGULAR_EXPRESSION "boat_scenery=ok")
add_test(NAME map_pack COMMAND ${Python3_EXECUTABLE} -I
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/map_pack.py --self-test)
set_tests_properties(map_pack PROPERTIES PASS_REGULAR_EXPRESSION "map pack fixtures OK")

add_executable(validate_boat_minimap tools/validate_boat_minimap.c)
target_link_libraries(validate_boat_minimap PRIVATE proteus_render)
target_compile_options(validate_boat_minimap PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_minimap COMMAND validate_boat_minimap)
set_tests_properties(boat_minimap PROPERTIES PASS_REGULAR_EXPRESSION "boat_minimap=ok")
# Adventure A2.2: in-memory tile catalogue and indexed collision, exact against the full scan.
add_executable(validate_boat_map_index tools/validate_boat_map_index.c)
target_link_libraries(validate_boat_map_index PRIVATE proteus_boat_scenery)
target_compile_options(validate_boat_map_index PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_map_index validate_boat_map_index LABELS core cpu)
# Adventure A2.5: the local view's queries, tiles and water do not grow with distant map.
add_executable(validate_boat_map_scaling tools/validate_boat_map_scaling.c)
target_link_libraries(validate_boat_map_scaling PRIVATE proteus_render)
target_compile_options(validate_boat_map_scaling PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_map_scaling COMMAND validate_boat_map_scaling)
set_tests_properties(boat_map_scaling PROPERTIES PASS_REGULAR_EXPRESSION "boat_map_scaling=ok")
# Multiplayer M2: the local four-boat authority and its scripted server, also core-only.
add_executable(validate_boat_fleet tools/validate_boat_fleet.c)
target_link_libraries(validate_boat_fleet PRIVATE proteus_boat_core)
# Capsize deathmatch rules: credit, respawns, bailouts and rounds over a core-only fleet.
add_executable(validate_boat_combat tools/validate_boat_combat.c)
target_link_libraries(validate_boat_combat PRIVATE proteus_boat_net)
add_test(NAME boat_combat COMMAND validate_boat_combat)
set_tests_properties(boat_combat PROPERTIES PASS_REGULAR_EXPRESSION "boat_combat=ok")
add_executable(validate_boat_match tools/validate_boat_match.c)
target_link_libraries(validate_boat_match PRIVATE proteus_boat_core)
# River sprint rules: gates, grid hold, places, recovery, deadlines and a four-bot ferry race.
# P2: the native race-rule suite also runs against the owned Lua reference.
add_executable(validate_boat_race_plugin tools/validate_boat_race.c)
target_compile_definitions(validate_boat_race_plugin PRIVATE BOAT_RACE_PLUGIN_FIXTURE=1)
target_link_libraries(validate_boat_race_plugin PRIVATE proteus_plugins)
target_compile_options(validate_boat_race_plugin PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_race_plugin validate_boat_race_plugin LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(validate_boat_plugin_race tools/validate_boat_plugin_race.c tools/boat_race_plugin.c)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/race-plugin-test")
target_compile_definitions(validate_boat_plugin_race PRIVATE
  BOAT_RACE_PLUGIN_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/race-plugin-test")
target_link_libraries(validate_boat_plugin_race PRIVATE proteus_plugins)
target_compile_options(validate_boat_plugin_race PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_plugin_race validate_boat_plugin_race LABELS core cpu
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(validate_boat_race tools/validate_boat_race.c)
target_link_libraries(validate_boat_race PRIVATE proteus_boat_core)
# M3: the authority process and a scripted network client link the ENet transport; the
# protocol/session validator links only the display- and socket-free net library.
add_executable(boat_server tools/boat_server_main.c tools/boat_server.c tools/cli_usage.c)
add_executable(boat_bot tools/boat_bot.c tools/cli_usage.c)
foreach(target boat_server boat_bot)
  target_link_libraries(${target} PRIVATE proteus_boat_net_enet)
endforeach()
if(PROTEUS_WEBRTC)
  include(cmake/WebRTC.cmake)
endif()
add_executable(validate_boat_net_mux tools/validate_boat_net_mux.c)
target_link_libraries(validate_boat_net_mux PRIVATE proteus_boat_net)
target_compile_options(validate_boat_net_mux PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_net_mux COMMAND validate_boat_net_mux)
set_tests_properties(boat_net_mux PROPERTIES LABELS "core;cpu" PASS_REGULAR_EXPRESSION "boat_net_mux=ok")
add_executable(validate_boat_net tools/validate_boat_net.c)
target_link_libraries(validate_boat_net PRIVATE proteus_boat_net)
# The same room harness over real ENet sockets on 127.0.0.1, still on a virtual clock. Cases
# that need the memory network's impairment are left out, hence -Wno-unused-function.
add_executable(validate_boat_net_enet tools/validate_boat_net.c)
target_link_libraries(validate_boat_net_enet PRIVATE proteus_boat_net_enet)
target_compile_definitions(validate_boat_net_enet PRIVATE BOAT_NET_TEST_ENET)
target_compile_options(validate_boat_net_enet PRIVATE ${PROTEUS_WARNINGS} -Wno-unused-function)
foreach(target validate_boat_combat validate_boat_fleet validate_boat_match validate_boat_race boat_server boat_bot validate_boat_net)
  target_compile_options(${target} PRIVATE ${PROTEUS_WARNINGS})
endforeach()
proteus_add_case_tests(boat_fleet validate_boat_fleet LABELS core cpu)
add_test(NAME boat_match COMMAND validate_boat_match)
set_tests_properties(boat_match PROPERTIES PASS_REGULAR_EXPRESSION "boat_match=ok")
proteus_add_case_tests(boat_race validate_boat_race LABELS core cpu)
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
add_test(NAME boat_server_bind_rejected COMMAND boat_server --bind not-an-address --ticks 1)
add_test(NAME boat_bot_endpoint_rejected COMMAND boat_bot --connect 127.0.0.1)
add_test(NAME boat_bot_discover_join_rejected
  COMMAND boat_bot --discover --connect 127.0.0.1:24680)
add_test(NAME boat_bot_snapshots_with_seconds_rejected
  COMMAND boat_bot --connect 127.0.0.1:24680 --snapshots 3 --seconds 2)
add_test(NAME boat_server_scripted_until_empty_rejected
  COMMAND boat_server --scripted --until-empty)
add_test(NAME boat_net_study_mode_rejected COMMAND wake_velocity --connect 127.0.0.1:24680 --waves)
add_test(NAME boat_net_study_name_rejected COMMAND wake_velocity --name lonely)
add_test(NAME boat_net_study_skin_rejected
  COMMAND wake_velocity --connect 127.0.0.1:24680 --skin norway)
# Hosting from the game: wake_velocity --serve is the room, --host starts it as a child and joins.
add_test(NAME wake_velocity_serve_scripted COMMAND wake_velocity --serve --scripted --ticks 240)
set_tests_properties(wake_velocity_serve_scripted PROPERTIES
  PASS_REGULAR_EXPRESSION "boat_server scripted: boats=4 ticks=240")
add_test(NAME wake_velocity_host_race_location_rejected
  COMMAND wake_velocity --host race --location basin)
add_test(NAME wake_velocity_host_connect_rejected
  COMMAND wake_velocity --host free --connect 127.0.0.1:24680)
add_test(NAME wake_velocity_port_without_host_rejected COMMAND wake_velocity --port 24680)
add_test(NAME boat_host COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/boat_host_check.py --study $<TARGET_FILE:wake_velocity>
  --bot $<TARGET_FILE:boat_bot>)
set_tests_properties(boat_host PROPERTIES PASS_REGULAR_EXPRESSION "boat_host=ok" TIMEOUT 30)
if(PROTEUS_GPU_TESTS)
  # A windowed host session on a private port: start the room, join it, leave, close it.
  add_test(NAME wake_velocity_host_gpu
    COMMAND wake_velocity --host deathmatch --location river --port 24779 --frames 120)
  set_tests_properties(wake_velocity_host_gpu PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu TIMEOUT 60
    PASS_REGULAR_EXPRESSION "standard input closed; closing the room")
endif()
set_tests_properties(boat_server_scripted_room_option_rejected boat_server_port_rejected
  boat_server_bind_rejected boat_server_round_without_deathmatch_rejected boat_bot_endpoint_rejected
  boat_bot_discover_join_rejected boat_bot_snapshots_with_seconds_rejected
  boat_server_scripted_until_empty_rejected boat_net_study_mode_rejected
  boat_net_study_name_rejected boat_net_study_skin_rejected wake_velocity_host_race_location_rejected
  wake_velocity_host_connect_rejected wake_velocity_port_without_host_rejected PROPERTIES WILL_FAIL TRUE)
proteus_add_case_tests(boat_net validate_boat_net LABELS core cpu)
proteus_add_case_tests(boat_net_enet validate_boat_net_enet LABELS cpu)
# Event-driven process smoke: boat_server and boat_bot connect, discover and exit. Session rules
# over the real transport are the boat_net_enet cases, on a virtual clock.
add_test(NAME boat_net_process COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/boat_net_process.py --server $<TARGET_FILE:boat_server>
  --bot $<TARGET_FILE:boat_bot>)
set_tests_properties(boat_net_process PROPERTIES PASS_REGULAR_EXPRESSION "boat_net_process=ok"
  TIMEOUT 60)
set(BOAT_CORE_BOUNDARY_NM)
if(CMAKE_NM AND NOT MSVC)
  set(BOAT_CORE_BOUNDARY_NM --nm ${CMAKE_NM} --archive $<TARGET_FILE:proteus_boat_core>)
endif()
# No test may wait for wall-clock time to pass (docs/TESTING.md, "No wall time").
add_test(NAME test_time COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_test_time.py --root ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(test_time PROPERTIES PASS_REGULAR_EXPRESSION "test_time=ok")
add_test(NAME boat_core_boundary COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_core_boundary.py --root ${CMAKE_CURRENT_SOURCE_DIR}
  ${BOAT_CORE_BOUNDARY_NM} ${PROTEUS_BOAT_CORE_SOURCES})
set_tests_properties(boat_core_boundary PROPERTIES PASS_REGULAR_EXPRESSION "core_boundary=ok")
# The net library (codec, sessions, memory transport) obeys the same rules, with no sockets.
set(BOAT_NET_BOUNDARY_NM)
if(CMAKE_NM AND NOT MSVC)
  set(BOAT_NET_BOUNDARY_NM --nm ${CMAKE_NM} --archive $<TARGET_FILE:proteus_boat_core>
    --archive $<TARGET_FILE:proteus_boat_net>)
endif()
add_test(NAME boat_net_boundary COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_core_boundary.py --root ${CMAKE_CURRENT_SOURCE_DIR}
  ${BOAT_NET_BOUNDARY_NM} ${PROTEUS_BOAT_CORE_SOURCES} ${PROTEUS_BOAT_NET_SOURCES})
set_tests_properties(boat_net_boundary PROPERTIES PASS_REGULAR_EXPRESSION "core_boundary=ok")
# Neither boundary library may link Lua or the plugin host (docs/PLUGIN_PLAN.md).
foreach(target proteus_boat_core proteus_boat_net)
  get_target_property(links ${target} LINK_LIBRARIES)
  if(links MATCHES "proteus_lua|proteus_plugins")
    message(FATAL_ERROR "${target} must not link Lua or the plugin host")
  endif()
endforeach()
proteus_add_case_tests(boat_handling validate_boat LABELS cpu)
proteus_add_case_tests(boat_banking validate_boat_banking LABELS cpu)
add_test(NAME boat_turn_entry COMMAND validate_boat_turn_entry)
add_test(NAME boat_banking_presentation COMMAND boat_banking_review)
add_test(NAME boat_flag_cpu COMMAND boat_flag_review --scenario cruise --time 5
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-flag-cpu.png)
set_tests_properties(boat_flag_cpu PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME boat_profile_records COMMAND validate_boat_profile)
proteus_add_case_tests(boat_environment validate_boat_environment LABELS cpu)
proteus_add_case_tests(boat_world_isolation validate_boat_world LABELS cpu)
# Lua plugin P1: the reference plugin's outing at the outer location, and the modes it refuses.
add_test(NAME wake_velocity_plugin_screenshot COMMAND wake_velocity --renderer cpu
  --plugin plugins/langedrag --time 3 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-plugin.png)
add_test(NAME wake_velocity_plugin_location_rejected COMMAND wake_velocity
  --plugin plugins/langedrag --location river)
add_test(NAME wake_velocity_plugin_course_rejected COMMAND wake_velocity
  --plugin plugins/langedrag --course)
add_test(NAME wake_velocity_plugin_room_rejected COMMAND wake_velocity
  --plugin plugins/langedrag --connect 127.0.0.1:24680)
add_test(NAME wake_velocity_plugin_missing_rejected COMMAND wake_velocity
  --plugin plugins/missing --renderer cpu --time 1 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-missing.png)
set_tests_properties(wake_velocity_plugin_location_rejected wake_velocity_plugin_course_rejected
  wake_velocity_plugin_room_rejected wake_velocity_plugin_missing_rejected PROPERTIES WILL_FAIL TRUE)
set_tests_properties(wake_velocity_plugin_screenshot wake_velocity_plugin_location_rejected
  wake_velocity_plugin_course_rejected wake_velocity_plugin_room_rejected
  wake_velocity_plugin_missing_rejected PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME boat_player_wake_screenshot COMMAND wake_velocity --renderer cpu --player-wake --time 9
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-player-wake.png)
add_test(NAME boat_player_wake_course_rejected COMMAND wake_velocity --player-wake --course)
set_tests_properties(boat_player_wake_course_rejected PROPERTIES WILL_FAIL TRUE)
add_test(NAME boat_current_screenshot COMMAND wake_velocity --renderer cpu --current --time 5
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-current.png)
add_test(NAME boat_crossing_wake_screenshot COMMAND wake_velocity --renderer cpu --crossing-wake --time 9
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-crossing-wake.png)
add_test(NAME boat_environment_screenshot COMMAND wake_velocity --renderer cpu --current --crossing-wake
  --waves --time 9 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-environment.png)
add_test(NAME boat_environment_course_rejected COMMAND wake_velocity --current --course)
add_test(NAME boat_environment_river_screenshot COMMAND wake_velocity --renderer cpu
  --location river --current --crossing-wake --time 3
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-river-environment.png)
set_tests_properties(boat_environment_course_rejected PROPERTIES WILL_FAIL TRUE)
add_test(NAME boat_default_player_wake_screenshot COMMAND wake_velocity --renderer cpu --time 9
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-default-player-wake.png)
add_test(NAME boat_object_wake_screenshot COMMAND wake_velocity --renderer cpu --wake-objects --time 9
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-object-wake.png)
set_tests_properties(boat_object_wake_screenshot PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(boat_default_player_wake_screenshot boat_object_wake_screenshot
  PROPERTIES FIXTURES_SETUP player_wake_defaults)
add_test(NAME boat_default_wake_matches_explicit COMMAND ${CMAKE_COMMAND} -E compare_files
  ${CMAKE_CURRENT_BINARY_DIR}/boat-default-player-wake.png
  ${CMAKE_CURRENT_BINARY_DIR}/boat-object-wake.png)
set_tests_properties(boat_default_wake_matches_explicit PROPERTIES FIXTURES_REQUIRED player_wake_defaults)
add_test(NAME boat_river_route COMMAND boat_water_review --check)
add_test(NAME boat_river_wave_route COMMAND boat_water_review --check --waves)
add_test(NAME boat_river_benchmark_options COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_boat_water_benchmark.py
  $<TARGET_FILE:boat_water_review> --options)
set(BOAT_REPLAY_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/replay-tests")
add_test(NAME boat_replay COMMAND validate_boat_replay ${BOAT_REPLAY_TEST_DIR})
set_tests_properties(boat_replay PROPERTIES FIXTURES_SETUP boat_recording)
add_test(NAME boat_replay_screenshot COMMAND wake_velocity --renderer cpu
  --replay ${BOAT_REPLAY_TEST_DIR}/replay-fixture.run --time 5
  --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-replay.png)
add_test(NAME boat_ghost_screenshot COMMAND wake_velocity --renderer cpu
  --ghost ${BOAT_REPLAY_TEST_DIR}/replay-fixture.run --time 0
  --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-ghost.png)
add_test(NAME boat_saved_best_screenshot COMMAND wake_velocity --renderer cpu --course --time 0
  --data-dir ${BOAT_REPLAY_TEST_DIR}
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-saved-best.png)
add_test(NAME boat_ghost_wrong_water COMMAND wake_velocity --renderer cpu --waves
  --ghost ${BOAT_REPLAY_TEST_DIR}/replay-fixture.run --time 0
  --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-ghost-invalid.png)
set_tests_properties(boat_ghost_wrong_water PROPERTIES WILL_FAIL TRUE)
set_tests_properties(boat_replay_screenshot boat_ghost_screenshot boat_saved_best_screenshot boat_ghost_wrong_water
  PROPERTIES FIXTURES_REQUIRED boat_recording)
add_test(NAME boat_course_screenshot COMMAND wake_velocity --renderer cpu --course --time 0
  --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-course.png)
add_test(NAME boat_wave_screenshot COMMAND wake_velocity --renderer cpu --waves --time 1.8
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-wave.png)
add_test(NAME boat_screenshot COMMAND wake_velocity --renderer cpu --time 3
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-screenshot.png)
add_test(NAME boat_cpu_aa_option COMMAND wake_velocity --renderer cpu --gpu-aa --time 0
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-cpu-aa-option.png)
add_test(NAME boat_cpu_aa_option_reversed COMMAND wake_velocity --gpu-aa --renderer cpu --time 0
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-cpu-aa-option-reversed.png)
add_test(NAME boat_river_screenshot COMMAND wake_velocity --renderer cpu --location river --time 0
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-river.png)

add_executable(dance_study tools/dance_study.c src/dance.c src/choreography.c src/xm_audio.c)
target_link_libraries(dance_study PRIVATE proteus_render)
target_compile_definitions(dance_study PRIVATE
  PROTEUS_ASSET_DIR="$<TARGET_FILE_DIR:dance_study>/assets"
)
target_compile_options(dance_study PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_dependencies(dance_study proteus_assets)

add_executable(validate_dance tools/validate_dance.c src/dance.c src/choreography.c)
target_link_libraries(validate_dance PRIVATE proteus_render)
target_compile_options(validate_dance PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME dance_constraints COMMAND validate_dance)
add_test(NAME house_dance_constraints COMMAND validate_dance ${CMAKE_CURRENT_SOURCE_DIR}/assets/dance/house.dance)
add_test(NAME house_groove_constraints COMMAND validate_dance ${CMAKE_CURRENT_SOURCE_DIR}/assets/dance/house-groove.dance)
add_executable(validate_choreography tools/validate_choreography.c src/choreography.c
  src/dance.c)
target_link_libraries(validate_choreography PRIVATE proteus_render)
target_compile_options(validate_choreography PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME choreography_language COMMAND validate_choreography
  ${CMAKE_CURRENT_BINARY_DIR}/choreography-validation.dance)
add_test(NAME dance_screenshot
  COMMAND dance_study --time 8.8 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/dance-screenshot.png
)

add_executable(validate_waterfront_actors tools/validate_waterfront_actors.c)
target_link_libraries(validate_waterfront_actors PRIVATE proteus_render)
target_compile_options(validate_waterfront_actors PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME waterfront_actors COMMAND validate_waterfront_actors)

add_executable(validate_city_dressing tools/validate_city_dressing.c)
target_link_libraries(validate_city_dressing PRIVATE proteus_render)
target_compile_options(validate_city_dressing PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME city_dressing COMMAND validate_city_dressing)

add_executable(validate_harbor tools/validate_harbor.c)
target_link_libraries(validate_harbor PRIVATE proteus_boat_session)
target_compile_options(validate_harbor PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME harbor_geometry COMMAND validate_harbor)

# L1.1 planned marina site: compiled-water and native hull-recipe checks before scene grants.
add_executable(validate_scene_site tools/validate_scene_site.c)
add_executable(validate_harbor_life tools/validate_harbor_life.c)
target_link_libraries(validate_harbor_life PRIVATE proteus_plugins proteus_boat_session)
target_compile_options(validate_harbor_life PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME harbor_life_qualification COMMAND validate_harbor_life)
set_tests_properties(harbor_life_qualification PROPERTIES LABELS "cpu;slow"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(validate_scene_site PRIVATE proteus_boat_core)
target_compile_options(validate_scene_site PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME scene_site COMMAND validate_scene_site)
set_tests_properties(scene_site PROPERTIES LABELS "core;cpu;quick")

add_executable(validate_traffic tools/validate_traffic.c)
target_link_libraries(validate_traffic PRIVATE proteus_boat_session)
target_compile_options(validate_traffic PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME harbor_traffic COMMAND validate_traffic)

add_executable(harbor_review tools/harbor_review.c)
add_dependencies(harbor_review proteus_assets)
add_test(NAME ferry_cpu_screenshot COMMAND harbor_review --renderer cpu --view ferry
  --time 40 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/ferry-cpu.png)
add_test(NAME harbor_cpu_screenshot COMMAND harbor_review --renderer cpu --view marina
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/harbor-marina-cpu.png)
target_link_libraries(harbor_review PRIVATE proteus_gpu)
target_compile_options(harbor_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)

add_executable(validate_renderer3d tools/validate_renderer3d.c)
target_link_libraries(validate_renderer3d PRIVATE proteus_render)
target_compile_options(validate_renderer3d PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME renderer3d_is_valid COMMAND validate_renderer3d)
add_test(NAME starter_xm_is_valid
  COMMAND validate_xm ${CMAKE_CURRENT_SOURCE_DIR}/assets/music/starter.xm
)
if(PROTEUS_GPU_TESTS)
  add_test(NAME ferry_gpu_screenshot COMMAND harbor_review --renderer gpu --view ferry
    --waves --time 40 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/ferry-gpu.png)
  set_tests_properties(ferry_gpu_screenshot PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME harbor_gpu_screenshot COMMAND harbor_review --renderer gpu --view marina
    --waves --time 2 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/harbor-marina-gpu.png)
  set_tests_properties(harbor_gpu_screenshot PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME boat_player_wake_gpu_screenshot COMMAND wake_velocity --player-wake --time 9
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-player-wake-gpu.png)
  add_test(NAME boat_player_wake_gpu_aa_screenshot COMMAND wake_velocity --player-wake --current --waves
    --gpu-aa --time 9 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-player-wake-gpu-aa.png)
  set_tests_properties(boat_player_wake_gpu_screenshot boat_player_wake_gpu_aa_screenshot
    PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME boat_environment_gpu_screenshot COMMAND wake_velocity --current --crossing-wake
    --waves --gpu-aa --time 9 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-environment-gpu.png)
  set_tests_properties(boat_environment_gpu_screenshot PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME boat_environment_gpu COMMAND validate_boat_environment --gpu)
  set_tests_properties(boat_environment_gpu PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME boat_flag_gpu COMMAND boat_flag_review --renderer gpu --scenario turn --time 5
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-flag-gpu.png)
  set_tests_properties(boat_flag_gpu PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME boat_banking_gpu COMMAND boat_banking_review --gpu
    --output ${CMAKE_CURRENT_BINARY_DIR}/banking-gpu)
  set_tests_properties(boat_banking_gpu PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME gpu_water_surface COMMAND boat_water_review --check-gpu)
  set_tests_properties(gpu_water_surface PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME gpu_view_binding COMMAND boat_water_review --check-gpu-views)
  set_tests_properties(gpu_view_binding PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  # Adventure A2.3: per-tile culling draws the same GPU and CPU pixels as drawing every tile.
  add_test(NAME tile_culling COMMAND boat_water_review --check-culling)
  set_tests_properties(tile_culling PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
    PASS_REGULAR_EXPRESSION "tile_culling=ok")
  add_test(NAME gpu_view_lifecycle COMMAND boat_water_review --check-gpu-lifecycle)
  set_tests_properties(gpu_view_lifecycle PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME boat_river_benchmark_contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_boat_water_benchmark.py
    $<TARGET_FILE:boat_water_review>)
  set_tests_properties(boat_river_benchmark_contract PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
    TIMEOUT 240)
  add_test(NAME boat_ghost_gpu_screenshot COMMAND wake_velocity --gpu-aa
    --ghost ${BOAT_REPLAY_TEST_DIR}/replay-fixture.run --time 0
    --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-ghost-gpu.png)
  add_test(NAME boat_replay_gpu_screenshot COMMAND wake_velocity --gpu-aa
    --replay ${BOAT_REPLAY_TEST_DIR}/replay-fixture.run --time 5
    --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-replay-gpu.png)
  set_tests_properties(boat_ghost_gpu_screenshot boat_replay_gpu_screenshot
    PROPERTIES FIXTURES_REQUIRED boat_recording LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME boat_gpu_screenshot COMMAND wake_velocity --location river --gpu --time 3
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-gpu.png)
  add_test(NAME boat_gpu_aa_screenshot COMMAND wake_velocity --location river --gpu --gpu-aa --time 3
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-gpu-aa.png)
  add_test(NAME boat_wave_gpu_screenshot COMMAND wake_velocity --location river --gpu-aa --waves
    --camera-distance 12 --camera-height 6 --time 2.9
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-wave-gpu.png)
  add_test(NAME boat_basin_gpu_screenshot COMMAND wake_velocity --time 0
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-basin-gpu.png)
  add_test(NAME boat_basin_gpu_aa_screenshot COMMAND wake_velocity --gpu-aa --time 0
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-basin-gpu-aa.png)
  add_test(NAME boat_course_gpu_screenshot COMMAND wake_velocity --course --gpu-aa --time 0
    --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-course-gpu.png)
  add_test(NAME boat_basin_wave_gpu_screenshot COMMAND wake_velocity --waves --gpu-aa --time 1.8
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-basin-wave-gpu.png)
  set_tests_properties(boat_basin_wave_gpu_screenshot PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  set_tests_properties(boat_basin_gpu_screenshot boat_basin_gpu_aa_screenshot boat_course_gpu_screenshot
    PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  set_tests_properties(boat_gpu_screenshot boat_gpu_aa_screenshot boat_wave_gpu_screenshot
    PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME gpu_surface_materials COMMAND gpu_fixture --city --check)
  set_tests_properties(gpu_surface_materials PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
endif()

add_test(NAME boating_xm_is_valid
  COMMAND validate_xm ${CMAKE_CURRENT_SOURCE_DIR}/assets/music/a-little-boat.xm --background)
add_test(NAME acid_xm_is_valid
  COMMAND validate_xm ${CMAKE_CURRENT_SOURCE_DIR}/assets/music/acid-wake.xm --acid)

# W1 stays isolated from player water and shaders; Python uses only the standard library.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME wake_front_prototype COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/wake_front_review.py --check
    --baseline $<TARGET_FILE:wake_baseline>)
  add_test(NAME wake_heightfield_prototype COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/wake_heightfield_review.py --check)
endif()

# Retired physical-field research is opt-in; arcade objects stay in the normal suite.
set(PROTEUS_WAKE_FIELD_CASES
  boat_wake_owned_field wake_field_refinement wake_runtime_cadence wake_packet_contracts
  wake_candidate_backend_queries wake_candidate_moving_source wake_gpu_runtime
  wake_candidate_boat wake_candidate_crossing wake_candidate_gpu
  wake_candidate_course_rejected wake_candidate_map_rejected wake_candidate_current_rejected
  wake_front_prototype wake_heightfield_prototype)
foreach(test IN LISTS PROTEUS_WAKE_FIELD_CASES)
  if(TEST ${test})
    set_property(TEST ${test} APPEND PROPERTY LABELS wake_field)
    if(NOT PROTEUS_WAKE_FIELD_TESTS)
      set_property(TEST ${test} PROPERTY DISABLED TRUE)
    endif()
  endif()
endforeach()

# Lanes: quick tests take under about 100 ms on their own (ctest --preset quick); everything
# else, including GPU tests, is slow and runs in the full suite. Case tests declare their lane
# in C (tools/boat_test_cases.h); the remaining tests heavier than that are listed here.
set(PROTEUS_SLOW_TESTS
  release_command
  acid_xm_is_valid boat_audio_is_valid boating_xm_is_valid starter_xm_is_valid
  boat_banking_presentation boat_benchmark_contract boat_clock_soak_smoke boat_combat
  boat_core_boundary boat_drive_extraction boat_flag_cloth boat_flag_cpu boat_fleet_render
  boat_host boat_map_scaling boat_net_boundary boat_net_process boat_outer_map boat_race_trial boat_replay
  boat_scenery
  boat_river_benchmark_options boat_river_route boat_river_wave_route boat_voyage
  boat_server_scripted_deathmatch boat_turn_entry city_dressing harbor_geometry harbor_traffic
  waterfront_actors
  boat_crossing_wake_screenshot boat_default_player_wake_screenshot boat_environment_screenshot
  boat_environment_river_screenshot boat_object_wake_screenshot boat_player_wake_screenshot
  boat_river_screenshot)

if(TEST boat_plugin_author)
  list(APPEND PROTEUS_SLOW_TESTS boat_plugin_author boat_plugin_capture boat_plugin_stage)
endif()

# Labels: core (display-free core and net library gates, fleet, server and bot), render (CPU captures), gpu (graphics context)
# and cpu (everything else that runs without a display).
get_property(PROTEUS_TESTS DIRECTORY PROPERTY TESTS)
foreach(test IN LISTS PROTEUS_TESTS)
  get_test_property(${test} LABELS labels)
  if(test MATCHES "^boat_combat$|^boat_core|^boat_outer_map$|^boat_fleet|^boat_match|^boat_server|^boat_net|^boat_bot")
    set_property(TEST ${test} APPEND PROPERTY LABELS core cpu)
  elseif(labels MATCHES "gpu")
  elseif(test MATCHES "screenshot|checksum|_cpu$")
    set_property(TEST ${test} APPEND PROPERTY LABELS render)
  else()
    set_property(TEST ${test} APPEND PROPERTY LABELS cpu)
  endif()
  get_test_property(${test} LABELS labels)
  if(labels MATCHES "gpu" OR test IN_LIST PROTEUS_SLOW_TESTS)
    set_property(TEST ${test} APPEND PROPERTY LABELS slow)
  else()
    set_property(TEST ${test} APPEND PROPERTY LABELS quick)
  endif()
endforeach()
foreach(test IN LISTS PROTEUS_SLOW_TESTS)
  if(NOT TEST ${test})
    message(AUTHOR_WARNING "PROTEUS_SLOW_TESTS names no test: ${test}")
  endif()
endforeach()

# Long deterministic physics runs need optimization even in the development build.
# This does not define NDEBUG or enable fast-math; all assertions/scenarios remain.
# The shared boating libraries are compiled once for validators and applications alike, so
# Debug wake_velocity also runs optimized boat code. Turn this off for instruction-by-instruction
# debugging of these executables and libraries.
if(PROTEUS_OPTIMIZE_TESTS)
  foreach(target proteus_boat_core proteus_render proteus_boat_session proteus_boat_drive
      validate_boat validate_boat_banking validate_boat_turn_entry
      validate_boat_environment validate_boat_replay boat_water_review wake_objects_review
      validate_boat_clock validate_boat_world validate_boat_core validate_boat_map
      validate_boat_map_index validate_boat_outer_map validate_boat_voyage validate_boat_fleet
      boat_server
      proteus_boat_net validate_boat_net validate_boat_net_enet)
    target_compile_options(${target} PRIVATE
      $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:AppleClang,Clang,GNU>>:-O2>)
  endforeach()
endif()
