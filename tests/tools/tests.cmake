# tests/tools: registered from tests/tests.cmake in the project root's directory scope.

if(NOT EMSCRIPTEN)
  add_test(NAME boat_combat_lighting_hour_invalid
    COMMAND boat_combat_review --lighting-hour nope)
  add_test(NAME boat_combat_lighting_hour_missing
    COMMAND boat_combat_review --lighting-hour)
  set_tests_properties(boat_combat_lighting_hour_invalid boat_combat_lighting_hour_missing
    PROPERTIES WILL_FAIL TRUE LABELS "cpu;quick")
endif()
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
add_test(NAME release_command COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_cut_release.py)
set_tests_properties(release_command PROPERTIES LABELS "cpu")
add_test(NAME web_endpoint_configuration COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_configure_web_endpoint.py)
add_test(NAME performance_analyzer COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_compare_performance_reports.py)
set_tests_properties(performance_analyzer PROPERTIES LABELS "cpu")
set_tests_properties(web_endpoint_configuration PROPERTIES LABELS "cpu")
add_test(NAME boat_benchmark_contract COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/validate_boat_benchmark.py $<TARGET_FILE:boat_benchmark>)

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
add_test(NAME boat_fleet_render COMMAND boat_fleet_review --check)
set_tests_properties(boat_fleet_render PROPERTIES LABELS "render;cpu"
  PASS_REGULAR_EXPRESSION "boat_fleet_render=ok")
add_test(NAME boat_plugin_catalog_invalid COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_plugin_catalog_gen.py
  --generator $<TARGET_FILE:boat_plugin_catalog_gen>)
set_tests_properties(boat_plugin_catalog_invalid PROPERTIES LABELS "cpu" TIMEOUT 120)
add_test(NAME map_pack COMMAND ${Python3_EXECUTABLE} -I
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/map_pack.py --self-test)
set_tests_properties(map_pack PROPERTIES PASS_REGULAR_EXPRESSION "map pack fixtures OK")
foreach(map river outer)
  add_test(NAME water_atlas_${map} COMMAND water_atlas_bake --map ${map}
    --check ${CMAKE_CURRENT_SOURCE_DIR}/assets/geometry/${map}/water_atlas.bin)
  set_tests_properties(water_atlas_${map} PROPERTIES PASS_REGULAR_EXPRESSION "water_atlas=ok")
endforeach()
foreach(map river outer)
  add_test(NAME ambient_${map} COMMAND ambient_bake --map ${map}
    --check ${CMAKE_CURRENT_SOURCE_DIR}/assets/geometry/${map}/ambient.bin)
  set_tests_properties(ambient_${map} PROPERTIES PASS_REGULAR_EXPRESSION "ambient=ok")
endforeach()
add_test(NAME boat_banking_presentation COMMAND boat_banking_review)
add_test(NAME boat_flag_cpu COMMAND boat_flag_review --scenario cruise --time 5
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-flag-cpu.png)
set_tests_properties(boat_flag_cpu PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME boat_river_route COMMAND boat_water_review --check)
add_test(NAME boat_river_wave_route COMMAND boat_water_review --check --waves)
add_test(NAME boat_outer_route COMMAND boat_water_review --check --scenario outer --waves)
add_test(NAME ferry_cpu_screenshot COMMAND harbor_review --renderer cpu --view ferry
  --time 40 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/ferry-cpu.png)
add_test(NAME harbor_cpu_screenshot COMMAND harbor_review --renderer cpu --view marina
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/harbor-marina-cpu.png)
