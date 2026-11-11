# tests/render: registered from tests/tests.cmake in the project root's directory scope.

add_executable(validate_boat_flag tests/render/validate_boat_flag.c)
target_link_libraries(validate_boat_flag PRIVATE proteus_render)
target_compile_options(validate_boat_flag PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME boat_flag_cloth COMMAND validate_boat_flag)
add_executable(validate_cargo_decal tests/render/validate_cargo_decal.c)
target_link_libraries(validate_cargo_decal PRIVATE proteus_render)
target_compile_options(validate_cargo_decal PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME cargo_shadow_geometry COMMAND validate_cargo_decal)
add_test(NAME boat_lighting_qualification_contract COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/validate_boat_lighting_qualification.py
  $<TARGET_FILE:boat_water_review>)
set_tests_properties(boat_lighting_qualification_contract PROPERTIES LABELS "cpu;slow"
  TIMEOUT 240)
add_executable(validate_boat_skin tests/render/validate_boat_skin.c)
target_link_libraries(validate_boat_skin PRIVATE proteus_render)
target_compile_options(validate_boat_skin PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_skin COMMAND validate_boat_skin)
set_tests_properties(boat_skin PROPERTIES LABELS "render;cpu")

add_executable(validate_boat_minimap tests/render/validate_boat_minimap.c)
target_link_libraries(validate_boat_minimap PRIVATE proteus_render)
target_compile_options(validate_boat_minimap PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_minimap COMMAND validate_boat_minimap)
set_tests_properties(boat_minimap PROPERTIES PASS_REGULAR_EXPRESSION "boat_minimap=ok")
# Adventure A2.5: the local view's queries, tiles and water do not grow with distant map.
add_executable(validate_boat_map_scaling tests/render/validate_boat_map_scaling.c)
target_link_libraries(validate_boat_map_scaling PRIVATE proteus_render)
target_compile_options(validate_boat_map_scaling PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_map_scaling COMMAND validate_boat_map_scaling)
set_tests_properties(boat_map_scaling PROPERTIES PASS_REGULAR_EXPRESSION "boat_map_scaling=ok")
add_test(NAME boat_river_benchmark_options COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/validate_boat_water_benchmark.py
  $<TARGET_FILE:boat_water_review> --options)

add_executable(validate_waterfront_actors tests/render/validate_waterfront_actors.c)
target_link_libraries(validate_waterfront_actors PRIVATE proteus_render)
target_compile_options(validate_waterfront_actors PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME waterfront_actors COMMAND validate_waterfront_actors)

add_executable(validate_city_dressing tests/render/validate_city_dressing.c)
target_link_libraries(validate_city_dressing PRIVATE proteus_render)
target_compile_options(validate_city_dressing PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME city_dressing COMMAND validate_city_dressing)
add_executable(validate_harbor_view tests/render/validate_harbor_view.c)
target_link_libraries(validate_harbor_view PRIVATE proteus_render)
target_compile_options(validate_harbor_view PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME harbor_view COMMAND validate_harbor_view)
set_tests_properties(harbor_view PROPERTIES LABELS "cpu;quick")

# M1 marina site: shipped layout, dimension/attachment rejection and maximum-capacity checks.
add_executable(validate_scene_site tests/render/validate_scene_site.c)
target_link_libraries(validate_scene_site PRIVATE proteus_plugins proteus_boat_session)
target_compile_options(validate_scene_site PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME scene_site COMMAND validate_scene_site)
set_tests_properties(scene_site PROPERTIES LABELS "cpu;slow" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(validate_renderer3d tests/render/validate_renderer3d.c)
target_link_libraries(validate_renderer3d PRIVATE proteus_render)
target_compile_options(validate_renderer3d PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME renderer3d_is_valid COMMAND validate_renderer3d)
add_executable(validate_waterfront_lighting tests/render/validate_waterfront_lighting.c)
target_link_libraries(validate_waterfront_lighting PRIVATE proteus_render)
target_compile_options(validate_waterfront_lighting PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME waterfront_lighting COMMAND validate_waterfront_lighting)
add_test(NAME waterfront_lighting_sky COMMAND validate_waterfront_lighting --sky-check)
add_executable(validate_boat_screen tests/render/validate_boat_screen.c)
target_link_libraries(validate_boat_screen PRIVATE proteus_render)
target_compile_options(validate_boat_screen PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_screen COMMAND validate_boat_screen)
add_executable(validate_waterfront_grade tests/render/validate_waterfront_grade.c)
target_link_libraries(validate_waterfront_grade PRIVATE proteus_render)
target_compile_options(validate_waterfront_grade PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME waterfront_grade COMMAND validate_waterfront_grade)
add_executable(validate_waterfront_lights tests/render/validate_waterfront_lights.c)
target_link_libraries(validate_waterfront_lights PRIVATE proteus_render)
target_compile_options(validate_waterfront_lights PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME waterfront_lights COMMAND validate_waterfront_lights)
if(PROTEUS_GPU_TESTS)
  add_test(NAME ferry_gpu_screenshot COMMAND harbor_review --renderer gpu --view ferry
    --waves --time 40 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/ferry-gpu.png)
  set_tests_properties(ferry_gpu_screenshot PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME harbor_gpu_screenshot COMMAND harbor_review --renderer gpu --view marina
    --waves --time 2 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/harbor-marina-gpu.png)
  set_tests_properties(harbor_gpu_screenshot PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME boat_player_wake_gpu_screenshot COMMAND wake_study --player-wake --time 9
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-player-wake-gpu.png)
  add_test(NAME boat_player_wake_gpu_aa_screenshot COMMAND wake_study --player-wake --current --waves
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
  # Graphics G2b: turning changes no detail level, and crossing the band out and back ends even.
  add_test(NAME detail_transitions COMMAND boat_water_review --check-detail)
  set_tests_properties(detail_transitions PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
    PASS_REGULAR_EXPRESSION "detail_check=ok")
  # Graphics G5a: high resolution, HDR bloom and fallback, preset changes and the HUD overlay.
  add_test(NAME presentation COMMAND boat_water_review --check-presentation)
  set_tests_properties(presentation PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
    PASS_REGULAR_EXPRESSION "presentation=ok")
  # Graphics G5b: clustered lights match every light tested everywhere, and the fallback.
  add_test(NAME light_clusters COMMAND boat_water_review --check-light-clusters)
  set_tests_properties(light_clusters PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
    PASS_REGULAR_EXPRESSION "light_clusters=ok")
  # Graphics G1: scalar and instanced objects draw the same bytes in every pass.
  add_test(NAME instancing_parity COMMAND boat_water_review --check-instancing)
  set_tests_properties(instancing_parity PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
    PASS_REGULAR_EXPRESSION "instancing_parity=ok")
  # Graphics G3a: the stamped wake field against the analytic pieces, lifecycle and switch.
  add_test(NAME wake_stamp COMMAND boat_water_review --check-wake-stamp)
  set_tests_properties(wake_stamp PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
    PASS_REGULAR_EXPRESSION "wake_stamp=ok")
  # Graphics G3b: foam history and ripples: replay, pause, catch-up, resets, scroll, stability.
  add_test(NAME water_history COMMAND boat_water_review --check-water-history)
  set_tests_properties(water_history PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
    PASS_REGULAR_EXPRESSION "water_history=ok")
  # Graphics G4: the opaque depth copy shades quay water, only lightens, and falls back; the
  # outer map binds its water atlas.
  add_test(NAME scene_depth COMMAND boat_water_review --check-scene-depth)
  set_tests_properties(scene_depth PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu
    PASS_REGULAR_EXPRESSION "scene_depth=ok")
  add_test(NAME gpu_view_lifecycle COMMAND boat_water_review --check-gpu-lifecycle)
  set_tests_properties(gpu_view_lifecycle PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
  add_test(NAME boat_river_benchmark_contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/render/validate_boat_water_benchmark.py
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
  add_test(NAME boat_gpu_screenshot COMMAND wake_velocity --location outer --gpu --time 3
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-gpu.png)
  add_test(NAME boat_gpu_aa_screenshot COMMAND wake_velocity --location outer --gpu --gpu-aa --time 3
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-gpu-aa.png)
  add_test(NAME boat_wave_gpu_screenshot COMMAND wake_velocity --location outer --gpu-aa --waves
    --camera-distance 12 --camera-height 6 --time 2.9
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-wave-gpu.png)
  add_test(NAME wake_velocity_work_gpu_screenshot COMMAND wake_velocity --location outer --work
    --gpu --time 3 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/wake-work-berth-gpu.png)
  add_test(NAME wake_velocity_work_delivered_gpu_screenshot COMMAND wake_velocity --location outer
    --work --gpu --time 14 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/wake-work-delivered-gpu.png)
  set_tests_properties(wake_velocity_work_gpu_screenshot wake_velocity_work_delivered_gpu_screenshot
    PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu)
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
