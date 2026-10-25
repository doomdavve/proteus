cmake_minimum_required(VERSION 3.24)
project(proteus LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
  src/boat_fleet.c src/boat_fleet.h src/boat_match.c src/boat_match.h src/boat_race.c src/boat_race.h src/boat_location.c src/boat_location.h src/boat_map.c
  src/boat_map.h
  src/boat_model.c src/boat_model.h src/boat_pose.c src/boat_pose.h
  src/boat_input.h src/boat_throttle.h src/boat_steering.h src/boat_view_state.h src/boat_wake.c src/boat_wake.h src/boat_wake_objects.c
  src/boat_wake_objects.h src/boat_wake_player.c src/boat_wake_player.h src/boat_water.c
  src/boat_water.h src/boat_world.c src/boat_world.h src/geometry3d.h
  src/waterfront_city.c src/waterfront_city.h src/waterfront_harbor.c src/waterfront_harbor.h
  src/waterfront_traffic.c src/waterfront_traffic.h)
add_library(proteus_boat_core STATIC ${PROTEUS_BOAT_CORE_SOURCES})
target_include_directories(proteus_boat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Types and raymath only; Debug builds resolve raymath's out-of-line definitions from raylib.
target_link_libraries(proteus_boat_core PUBLIC raylib)
target_compile_options(proteus_boat_core PRIVATE ${PROTEUS_WARNINGS})

# Presentation: cosmetic simulation, CPU renderer, boat/course views, water meshes and scenes.
add_library(proteus_render STATIC
  src/boat_hud.c src/boat_hud.h src/boat_skin.c src/boat_skin.h src/boat_course_view.c
  src/boat_flag.c src/boat_presentation.c src/boat_view.c
  src/boat_water_mesh.c src/renderer3d.c src/waterfront.c src/waterfront_corner.c
  src/waterfront_scene.c)
target_link_libraries(proteus_render PUBLIC proteus_boat_core)
target_compile_options(proteus_render PRIVATE ${PROTEUS_WARNINGS})

# GPU renderers, linked only by graphics executables.
add_library(proteus_gpu STATIC src/boat_gpu.c src/renderer_gpu.c)
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

add_executable(wake_velocity tools/wake_velocity.c tools/wake_velocity_menu.c tools/boat_title.c src/profiler.c
  src/boat_audio.c src/xm_audio.c)
target_link_libraries(wake_velocity PRIVATE proteus_boat_drive proteus_gpu proteus_frame_pacer)
target_compile_options(wake_velocity PRIVATE ${PROTEUS_WARNINGS})

# Race R0: measured shortened river route, source contracts, captures and playable trial.
add_executable(boat_race_trial tools/boat_race_trial.c tools/boat_vessel_wake_gpu.c)
target_include_directories(boat_race_trial PRIVATE ${raylib_SOURCE_DIR}/src)
target_link_libraries(boat_race_trial PRIVATE proteus_boat_drive proteus_gpu)
target_compile_options(boat_race_trial PRIVATE ${PROTEUS_WARNINGS})

# Multiplayer M3, native only: the protocol codec, the authority and client sessions and the
# in-memory impairment transport obey the core boundary (no devices, files or sockets);
# boat_net_enet.c is the only file that sees ENet or the operating system's sockets.
if(NOT EMSCRIPTEN)
  set(PROTEUS_BOAT_NET_SOURCES
    src/boat_net_client.c src/boat_net_client.h src/boat_net_memory.c src/boat_net_protocol.c
    src/boat_net_protocol.h src/boat_net_server.c src/boat_net_server.h src/boat_net_transport.h
    src/boat_prediction.c src/boat_prediction.h src/boat_remote_view.c src/boat_remote_view.h)
  add_library(proteus_boat_net STATIC ${PROTEUS_BOAT_NET_SOURCES})
  target_link_libraries(proteus_boat_net PUBLIC proteus_boat_core)
  target_compile_options(proteus_boat_net PRIVATE ${PROTEUS_WARNINGS})

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
  # The game hosts a room by running itself as `--serve`, boat_server's command line.
  target_sources(wake_velocity PRIVATE tools/wake_velocity_net.c tools/wake_velocity_net.h
    tools/wake_velocity_host.c tools/wake_velocity_host.h tools/boat_server.c tools/boat_server.h)
endif()

if(EMSCRIPTEN)
  include(cmake/Web.cmake)
  return() # Desktop studies and CTest tools remain native targets.
endif()

add_custom_target(proteus_assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:wake_velocity>/assets
)

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
  set_tests_properties(wake_gpu_runtime PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
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
add_dependencies(boat_race_trial proteus_assets)
add_test(NAME boat_race_trial COMMAND boat_race_trial --check)
set_tests_properties(boat_race_trial PROPERTIES
  PASS_REGULAR_EXPRESSION "boat_race_trial=ok" LABELS "core;cpu")
set_tests_properties(boat_race_trial PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
if(PROTEUS_GPU_TESTS)
  add_test(NAME boat_race_trial_gpu COMMAND boat_race_trial --gpu-check)
  set_tests_properties(boat_race_trial_gpu PROPERTIES LABELS "gpu;render" RUN_SERIAL TRUE
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
add_test(NAME boat_clock COMMAND validate_boat_clock ${BOAT_CLOCK_TEST_DIR})
add_test(NAME boat_clock_soak_smoke
  COMMAND validate_boat_clock --soak 60 ${BOAT_CLOCK_TEST_DIR} --waves)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME boat_benchmark_contract COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_boat_benchmark.py $<TARGET_FILE:boat_benchmark>)

add_executable(wake_objects_review tools/wake_objects_review.c)
target_include_directories(wake_objects_review PRIVATE ${raylib_SOURCE_DIR}/src)
target_link_libraries(wake_objects_review PRIVATE proteus_boat_session proteus_gpu)
target_compile_options(wake_objects_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_dependencies(wake_objects_review proteus_assets)
add_test(NAME wake_objects_contracts_jumps COMMAND wake_objects_review)
set_tests_properties(wake_objects_contracts_jumps PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 120)
if(PROTEUS_GPU_TESTS)
  add_test(NAME wake_objects_gpu COMMAND wake_objects_review --gpu)
  set_tests_properties(wake_objects_gpu PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    LABELS "gpu" RUN_SERIAL TRUE TIMEOUT 120)
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
    LABELS "gpu" RUN_SERIAL TRUE TIMEOUT 180)
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
target_link_libraries(validate_boat_map PRIVATE proteus_boat_core)
target_compile_options(validate_boat_map PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_map COMMAND validate_boat_map)
set_tests_properties(boat_map PROPERTIES PASS_REGULAR_EXPRESSION "boat_map=ok")
# Multiplayer M2: the local four-boat authority and its scripted server, also core-only.
add_executable(validate_boat_fleet tools/validate_boat_fleet.c)
target_link_libraries(validate_boat_fleet PRIVATE proteus_boat_core)
# Capsize deathmatch rules: credit, respawns, bailouts and rounds over a core-only fleet.
add_executable(validate_boat_match tools/validate_boat_match.c)
target_link_libraries(validate_boat_match PRIVATE proteus_boat_core)
# River sprint rules: gates, grid hold, places, recovery, deadlines and a four-bot ferry race.
add_executable(validate_boat_race tools/validate_boat_race.c)
target_link_libraries(validate_boat_race PRIVATE proteus_boat_core)
# M3: the authority process and a scripted network client link the ENet transport; the
# protocol/session validator links only the display- and socket-free net library.
add_executable(boat_server tools/boat_server_main.c tools/boat_server.c)
add_executable(boat_bot tools/boat_bot.c)
foreach(target boat_server boat_bot)
  target_link_libraries(${target} PRIVATE proteus_boat_net_enet)
endforeach()
add_executable(validate_boat_net tools/validate_boat_net.c)
target_link_libraries(validate_boat_net PRIVATE proteus_boat_net)
foreach(target validate_boat_fleet validate_boat_match validate_boat_race boat_server boat_bot validate_boat_net)
  target_compile_options(${target} PRIVATE ${PROTEUS_WARNINGS})
endforeach()
add_test(NAME boat_fleet COMMAND validate_boat_fleet)
set_tests_properties(boat_fleet PROPERTIES PASS_REGULAR_EXPRESSION "boat_fleet=ok")
add_test(NAME boat_match COMMAND validate_boat_match)
set_tests_properties(boat_match PROPERTIES PASS_REGULAR_EXPRESSION "boat_match=ok")
add_test(NAME boat_race COMMAND validate_boat_race)
set_tests_properties(boat_race PROPERTIES PASS_REGULAR_EXPRESSION "boat_race=ok")
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
  set_tests_properties(wake_velocity_host_gpu PROPERTIES LABELS "gpu" RUN_SERIAL TRUE TIMEOUT 60
    PASS_REGULAR_EXPRESSION "standard input closed; closing the room")
endif()
set_tests_properties(boat_server_scripted_room_option_rejected boat_server_port_rejected
  boat_server_bind_rejected boat_server_round_without_deathmatch_rejected boat_bot_endpoint_rejected
  boat_bot_discover_join_rejected boat_net_study_mode_rejected
  boat_net_study_name_rejected boat_net_study_skin_rejected wake_velocity_host_race_location_rejected
  wake_velocity_host_connect_rejected wake_velocity_port_without_host_rejected PROPERTIES WILL_FAIL TRUE)
add_test(NAME boat_net COMMAND validate_boat_net)
set_tests_properties(boat_net PROPERTIES PASS_REGULAR_EXPRESSION "boat_net=ok")
add_test(NAME boat_net_loopback COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/boat_net_smoke.py --server $<TARGET_FILE:boat_server>
  --bot $<TARGET_FILE:boat_bot>)
set_tests_properties(boat_net_loopback PROPERTIES PASS_REGULAR_EXPRESSION "boat_net_loopback=ok"
  TIMEOUT 60)
add_test(NAME boat_net_deathmatch COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/boat_net_smoke.py --deathmatch
  --server $<TARGET_FILE:boat_server> --bot $<TARGET_FILE:boat_bot>)
set_tests_properties(boat_net_deathmatch PROPERTIES
  PASS_REGULAR_EXPRESSION "boat_net_deathmatch=ok" TIMEOUT 60)
add_test(NAME boat_net_race COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/boat_net_smoke.py --race
  --server $<TARGET_FILE:boat_server> --bot $<TARGET_FILE:boat_bot>)
set_tests_properties(boat_net_race PROPERTIES PASS_REGULAR_EXPRESSION "boat_net_race=ok"
  TIMEOUT 60)
add_test(NAME boat_net_session COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/boat_net_session.py
  --server $<TARGET_FILE:boat_server> --bot $<TARGET_FILE:boat_bot>)
set_tests_properties(boat_net_session PROPERTIES
  PASS_REGULAR_EXPRESSION "boat_net_session=ok" TIMEOUT 90)
add_test(NAME boat_net_river COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/boat_net_session.py --location river --mode deathmatch
  --server $<TARGET_FILE:boat_server> --bot $<TARGET_FILE:boat_bot>)
set_tests_properties(boat_net_river PROPERTIES
  PASS_REGULAR_EXPRESSION "boat_net_session=ok" TIMEOUT 90)
set(BOAT_CORE_BOUNDARY_NM)
if(CMAKE_NM AND NOT MSVC)
  set(BOAT_CORE_BOUNDARY_NM --nm ${CMAKE_NM} --archive $<TARGET_FILE:proteus_boat_core>)
endif()
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
add_test(NAME boat_handling COMMAND validate_boat)
add_test(NAME boat_banking COMMAND validate_boat_banking)
add_test(NAME boat_turn_entry COMMAND validate_boat_turn_entry)
add_test(NAME boat_banking_presentation COMMAND boat_banking_review)
add_test(NAME boat_flag_cpu COMMAND boat_flag_review --scenario cruise --time 5
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-flag-cpu.png)
set_tests_properties(boat_flag_cpu PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME boat_profile_records COMMAND validate_boat_profile)
add_test(NAME boat_environment COMMAND validate_boat_environment)
add_test(NAME boat_world_isolation COMMAND validate_boat_world)
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
  set_tests_properties(ferry_gpu_screenshot PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  add_test(NAME harbor_gpu_screenshot COMMAND harbor_review --renderer gpu --view marina
    --waves --time 2 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/harbor-marina-gpu.png)
  set_tests_properties(harbor_gpu_screenshot PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  add_test(NAME boat_player_wake_gpu_screenshot COMMAND wake_velocity --player-wake --time 9
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-player-wake-gpu.png)
  add_test(NAME boat_player_wake_gpu_aa_screenshot COMMAND wake_velocity --player-wake --current --waves
    --gpu-aa --time 9 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-player-wake-gpu-aa.png)
  set_tests_properties(boat_player_wake_gpu_screenshot boat_player_wake_gpu_aa_screenshot
    PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  add_test(NAME boat_environment_gpu_screenshot COMMAND wake_velocity --current --crossing-wake
    --waves --gpu-aa --time 9 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-environment-gpu.png)
  set_tests_properties(boat_environment_gpu_screenshot PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  add_test(NAME boat_environment_gpu COMMAND validate_boat_environment --gpu)
  set_tests_properties(boat_environment_gpu PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  add_test(NAME boat_flag_gpu COMMAND boat_flag_review --renderer gpu --scenario turn --time 5
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-flag-gpu.png)
  set_tests_properties(boat_flag_gpu PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    LABELS "gpu" RUN_SERIAL TRUE)
  add_test(NAME boat_banking_gpu COMMAND boat_banking_review --gpu
    --output ${CMAKE_CURRENT_BINARY_DIR}/banking-gpu)
  set_tests_properties(boat_banking_gpu PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  add_test(NAME gpu_water_surface COMMAND boat_water_review --check-gpu)
  set_tests_properties(gpu_water_surface PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  add_test(NAME boat_river_benchmark_contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_boat_water_benchmark.py
    $<TARGET_FILE:boat_water_review>)
  set_tests_properties(boat_river_benchmark_contract PROPERTIES LABELS "gpu" RUN_SERIAL TRUE
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
    PROPERTIES FIXTURES_REQUIRED boat_recording LABELS "gpu" RUN_SERIAL TRUE)
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
  set_tests_properties(boat_basin_wave_gpu_screenshot PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  set_tests_properties(boat_basin_gpu_screenshot boat_basin_gpu_aa_screenshot boat_course_gpu_screenshot
    PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  set_tests_properties(boat_gpu_screenshot boat_gpu_aa_screenshot boat_wave_gpu_screenshot
    PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
  add_test(NAME gpu_surface_materials COMMAND gpu_fixture --city --check)
  set_tests_properties(gpu_surface_materials PROPERTIES LABELS "gpu" RUN_SERIAL TRUE)
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

# Labels: core (display-free core and net library gates, fleet, server and bot), render (CPU captures), gpu (graphics context)
# and cpu (everything else that runs without a display).
get_property(PROTEUS_TESTS DIRECTORY PROPERTY TESTS)
foreach(test IN LISTS PROTEUS_TESTS)
  get_test_property(${test} LABELS labels)
  if(test MATCHES "^boat_core|^boat_map$|^boat_fleet|^boat_match|^boat_race$|^boat_server|^boat_net|^boat_bot")
    set_property(TEST ${test} APPEND PROPERTY LABELS core cpu)
  elseif(labels MATCHES "gpu")
  elseif(test MATCHES "screenshot|checksum|_cpu$")
    set_property(TEST ${test} APPEND PROPERTY LABELS render)
  else()
    set_property(TEST ${test} APPEND PROPERTY LABELS cpu)
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
      validate_boat_clock validate_boat_world validate_boat_core validate_boat_map validate_boat_fleet
      boat_server
      proteus_boat_net validate_boat_net)
    target_compile_options(${target} PRIVATE
      $<$<AND:$<CONFIG:Debug>,$<C_COMPILER_ID:AppleClang,Clang,GNU>>:-O2>)
  endforeach()
endif()
