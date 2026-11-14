# Race R0: measured shortened river route, source contracts, captures and playable trial.
add_executable(boat_race_trial boat_race_trial.c boat_vessel_wake_gpu.c
  boat_race_plugin.c boat_race_plugin.h)
target_include_directories(boat_race_trial PRIVATE ${PROJECT_SOURCE_DIR} ${raylib_SOURCE_DIR}/src)
target_link_libraries(boat_race_trial PRIVATE proteus_boat_drive proteus_gpu proteus_plugins
  proteus_plugin_catalog)
target_compile_options(boat_race_trial PRIVATE ${PROTEUS_WARNINGS})

if(NOT EMSCRIPTEN)
  add_executable(boat_combat_review boat_combat_review.c ${PROJECT_SOURCE_DIR}/apps/wake_velocity/wake_velocity_net.c ${PROJECT_SOURCE_DIR}/src/audio/boat_audio.c)
  target_link_libraries(boat_combat_review PRIVATE proteus_boat_drive proteus_gpu
    proteus_boat_net_enet proteus_boat_fixtures)
  target_compile_options(boat_combat_review PRIVATE ${PROTEUS_WARNINGS})
  # P5d: a CPU capture of the hunt's marks, objective panel and minimap marks (build/hunt.png).
  add_executable(boat_hunt_review boat_hunt_review.c ${PROJECT_SOURCE_DIR}/apps/wake_velocity/wake_velocity_net.c)
  target_link_libraries(boat_hunt_review PRIVATE proteus_boat_drive proteus_gpu
    proteus_boat_net_enet proteus_boat_fixtures)
  target_compile_options(boat_hunt_review PRIVATE ${PROTEUS_WARNINGS})
  add_dependencies(boat_hunt_review proteus_assets)
endif()

# Native only: the browser build returns before these in the project root.

if(NOT EMSCRIPTEN)
# Reproducible, scenery-only title sample at the original bridge-lookback camera.
add_executable(title_backdrop title_backdrop.c)
target_link_libraries(title_backdrop PRIVATE proteus_gpu)
target_compile_options(title_backdrop PRIVATE ${PROTEUS_WARNINGS})

add_executable(gpu_fixture gpu_fixture.c)
target_include_directories(gpu_fixture PRIVATE ${raylib_SOURCE_DIR}/src)
target_link_libraries(gpu_fixture PRIVATE proteus_gpu)
target_compile_definitions(gpu_fixture PRIVATE
  PROTEUS_ASSET_DIR="$<TARGET_FILE_DIR:gpu_fixture>/assets"
)
target_compile_options(gpu_fixture PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_dependencies(gpu_fixture proteus_assets)
add_dependencies(title_backdrop proteus_assets)
add_dependencies(boat_race_trial proteus_assets)
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  OUTPUT_VARIABLE BOAT_BENCH_REV OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT BOAT_BENCH_REV)
  set(BOAT_BENCH_REV unknown)
endif()
execute_process(COMMAND git status --porcelain --untracked-files=no
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE BOAT_BENCH_DIRTY)
if(BOAT_BENCH_DIRTY)
  string(APPEND BOAT_BENCH_REV "-dirty")
endif()
add_executable(boat_benchmark boat_benchmark.c)
target_link_libraries(boat_benchmark PRIVATE proteus_boat_drive)
target_compile_definitions(boat_benchmark PRIVATE
  BOAT_BENCH_BUILD="${BOAT_BENCH_REV}/${CMAKE_BUILD_TYPE}/${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}"
  BOAT_BENCH_PLATFORM="${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
target_compile_options(boat_benchmark PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_executable(boat_water_review boat_water_review.c)
add_dependencies(boat_water_review proteus_assets)
target_compile_definitions(boat_water_review PRIVATE
  BOAT_BENCH_BUILD="${BOAT_BENCH_REV}/${CMAKE_BUILD_TYPE}/${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}"
  BOAT_BENCH_PLATFORM="${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
add_executable(boat_banking_review boat_banking_review.c)
add_dependencies(boat_banking_review proteus_assets)

add_executable(boat_hud_review boat_hud_review.c)
target_link_libraries(boat_hud_review PRIVATE proteus_render)
target_compile_options(boat_hud_review PRIVATE ${PROTEUS_WARNINGS})

add_executable(boat_flag_review boat_flag_review.c)
add_dependencies(boat_flag_review proteus_assets)
add_executable(boat_fleet_review boat_fleet_review.c)
add_dependencies(boat_fleet_review proteus_assets)
foreach(target boat_banking_review boat_flag_review boat_fleet_review)
  target_link_libraries(${target} PRIVATE proteus_boat_session proteus_gpu)
endforeach()
# The drive library adds boat_profile statistics to the session.
target_link_libraries(boat_water_review PRIVATE proteus_boat_drive proteus_gpu proteus_frame_pacer)
# P3.3b display-free objective inspection and native checkpoint/seek review.
add_executable(boat_plugin_review boat_plugin_review.c ${PROJECT_SOURCE_DIR}/apps/wake_velocity/wake_velocity_plugin.c)
target_link_libraries(boat_plugin_review PRIVATE proteus_plugins proteus_wake_objective proteus_plugin_catalog proteus_boat_session)
target_compile_options(boat_plugin_review PRIVATE ${PROTEUS_WARNINGS})

# P3 runtime qualification: explicit measurements, never timing-sensitive CTest assertions.
add_executable(boat_plugin_bench boat_plugin_bench.c)
target_link_libraries(boat_plugin_bench PRIVATE proteus_plugins proteus_boat_session)
target_compile_options(boat_plugin_bench PRIVATE ${PROTEUS_WARNINGS})
# Graphics G4: each map's water atlas sidecar rebakes byte for byte from its compiled water.
add_executable(water_atlas_bake water_atlas_bake.c)
target_link_libraries(water_atlas_bake PRIVATE proteus_render)
target_compile_options(water_atlas_bake PRIVATE ${PROTEUS_WARNINGS})

# Graphics G5c: each map's ambient occlusion sidecar rebakes byte for byte from its scenery pack.
add_executable(ambient_bake ambient_bake.c)
target_link_libraries(ambient_bake PRIVATE proteus_render)
target_compile_options(ambient_bake PRIVATE ${PROTEUS_WARNINGS})
add_executable(boat_bot boat_bot.c ${PROJECT_SOURCE_DIR}/src/platform/cli_usage.c)

add_executable(harbor_review harbor_review.c)
add_dependencies(harbor_review proteus_assets)
target_link_libraries(harbor_review PRIVATE proteus_gpu)
target_compile_options(harbor_review PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
endif()

# Offline utilities consume validator fixtures without exposing them to applications.
get_property(PROTEUS_TOOL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS PROTEUS_TOOL_TARGETS)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/apps)
endforeach()
