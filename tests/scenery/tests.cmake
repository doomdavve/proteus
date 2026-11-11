# tests/scenery: registered from tests/tests.cmake in the project root's directory scope.

# Adventure A1.1: explicit map descriptions and independently owned collision caches.
add_executable(validate_boat_map tests/scenery/validate_boat_map.c)
target_link_libraries(validate_boat_map PRIVATE proteus_boat_scenery)
target_compile_options(validate_boat_map PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_map validate_boat_map LABELS core cpu)
# Adventure A3.1: the baked first outer-river slice answers the A0.3 route and shelter queries.
add_executable(validate_boat_outer_map tests/scenery/validate_boat_outer_map.c)
target_link_libraries(validate_boat_outer_map PRIVATE proteus_boat_scenery)
target_compile_options(validate_boat_outer_map PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_outer_map COMMAND validate_boat_outer_map)
set_tests_properties(boat_outer_map PROPERTIES PASS_REGULAR_EXPRESSION "boat_outer_map=ok")
# Solo minimap: baked chart, projection, markers and trim; --screenshot writes review captures.
# Adventure A3.5i: scenery packs load once, match the compiled water and reject bad input.
add_executable(validate_boat_scenery tests/scenery/validate_boat_scenery.c)
target_link_libraries(validate_boat_scenery PRIVATE proteus_render)
target_compile_options(validate_boat_scenery PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_scenery COMMAND validate_boat_scenery)
set_tests_properties(boat_scenery PROPERTIES PASS_REGULAR_EXPRESSION "boat_scenery=ok")
# Adventure A2.2: in-memory tile catalogue and indexed collision, exact against the full scan.
add_executable(validate_boat_map_index tests/scenery/validate_boat_map_index.c)
target_link_libraries(validate_boat_map_index PRIVATE proteus_boat_scenery)
target_compile_options(validate_boat_map_index PRIVATE ${PROTEUS_WARNINGS})
proteus_add_case_tests(boat_map_index validate_boat_map_index LABELS core cpu)
