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
# Enable C++ before creating targets in module/application subdirectories so their link
# rules inherit the runtime required by the static libdatachannel dependency.
if(PROTEUS_WEBRTC)
  enable_language(CXX)
endif()

option(PROTEUS_GPU_TESTS "Enable GPU-context CTest cases" OFF)
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

# Imported targets are directory-scoped: find them here so the game and its tests see them.
if(NOT EMSCRIPTEN)
  find_package(CURL REQUIRED)
  find_package(Threads REQUIRED)
endif()

# Executables and libraries keep landing in the build directory's root, as tests,
# scripts and packaging expect (multi-config generators add the configuration).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# Modules, in dependency order. Each owns a CMakeLists.txt beside its sources.
add_subdirectory(src/core)
add_subdirectory(src/scenery)
add_subdirectory(src/json)
add_subdirectory(src/plugins)
add_subdirectory(src/session)
add_subdirectory(src/render)
add_subdirectory(src/platform)
add_subdirectory(src/net)
add_subdirectory(apps/wake_velocity)
add_subdirectory(tools)

if(EMSCRIPTEN)
  include(cmake/Web.cmake)
  return() # Desktop studies and CTest tools remain native targets.
endif()

add_subdirectory(apps/boat_server)
add_subdirectory(apps/cargo_sandbox)
add_subdirectory(apps/dance_study)

add_custom_target(proteus_assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:wake_velocity>/assets
)

enable_testing()

include(cmake/CaseTests.cmake)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

find_program(PROTEUS_NODE_EXECUTABLE node)

if(PROTEUS_WEBRTC)
  include(cmake/WebRTC.cmake)
endif()

# W1 stays isolated from player water and shaders; Python uses only the standard library.
find_package(Python3 COMPONENTS Interpreter)

include(tests/tests.cmake)

# Lanes: quick tests take under about 100 ms on their own (ctest --preset quick); everything
# else, including GPU tests, is slow and runs in the full suite. Case tests declare their lane
# in C (tests/support/boat_test_cases.h); the remaining tests heavier than that are listed here.
set(PROTEUS_SLOW_TESTS
  cargo_shadow_wave_cpu ${PROTEUS_RESUME_TESTS}
  cargo_sandbox_shadow-high_screenshot cargo_sandbox_shadow-low_screenshot cargo_sandbox_remoor_screenshot
  cargo_sandbox_crossing_screenshot cargo_sandbox_receiver-approach_screenshot
  cargo_sandbox_delivered_screenshot cargo_sandbox_landing-built_screenshot
  cargo_sandbox_redelivered_screenshot cargo_sandbox_job-reset_screenshot
  cargo_sandbox_build-blocked_screenshot cargo_sandbox_quay-pickup_screenshot
  cargo_sandbox_jetty-approach_screenshot cargo_sandbox_jetty-delivered_screenshot
  cargo_sandbox_site cargo_sandbox_outer_left_screenshot cargo_sandbox_outer_right_screenshot
  release_command
  acid_xm_is_valid boat_audio_is_valid boating_xm_is_valid starter_xm_is_valid
  boat_banking_presentation boat_benchmark_contract boat_clock_soak_smoke boat_combat
  boat_lighting_qualification_contract
  boat_room_record_objective boat_session_record_process
  boat_session_record.hunt boat_session_record.playback boat_session_record.diagnostic
  boat_session_record.seek
  boat_core_boundary boat_drive_extraction boat_flag_cloth boat_flag_cpu boat_fleet_render
  boat_host boat_map_scaling boat_net_boundary boat_net_process boat_outer_map boat_race_trial boat_replay
  boat_scenery ambient_river ambient_outer
  boat_river_benchmark_options boat_river_route boat_river_wave_route boat_outer_route
  boat_voyage
  boat_server_scripted_deathmatch boat_turn_entry city_dressing harbor_geometry harbor_traffic
  waterfront_actors waterfront_lighting_sky scene_site harbor_life_qualification
  boat_crossing_wake_screenshot boat_default_player_wake_screenshot boat_environment_screenshot
  boat_environment_outer_screenshot boat_object_wake_screenshot boat_player_wake_screenshot
  boat_outer_screenshot wake_velocity_work_cpu_screenshot wake_velocity_work_delivered_cpu_screenshot
  wake_velocity_work_refuses_package wake_velocity_work_save_write wake_velocity_work_save_resume
  wake_velocity_title_start.basin
  wake_velocity_title_start.outer-standard wake_velocity_title_start.outer-harbor-life
  wake_velocity_title_start.outer-grodan wake_velocity_title_start.outer-canal-tour
  wake_velocity_title_start.outer-working-harbour wake_velocity_title_start.basin-trial)

if(TEST boat_plugin_author)
  list(APPEND PROTEUS_SLOW_TESTS boat_plugin_author boat_plugin_capture boat_plugin_stage)
endif()
# About 0.2-0.4 s each: they copy and check a temporary tree once per planted violation.
list(APPEND PROTEUS_SLOW_TESTS plugin_stage_backend_self_test plugin_data_boundary_self_test)

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

include(cmake/CI.cmake)

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
      ambient_bake
      boat_server
      proteus_boat_net validate_boat_net validate_boat_net_enet validate_boat_local
      proteus_local_drive)
    target_compile_options(${target} PRIVATE
      $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:AppleClang,Clang,GNU>>:-O2>)
  endforeach()
endif()
