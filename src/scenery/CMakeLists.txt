# Scenery packs: each mapped location's whole bake (assets/geometry/<location>/scenery.bin), loaded
# at run time for presentation. The core keeps only the compiled water, so it never reads them.
add_library(proteus_boat_scenery STATIC boat_scenery.c boat_scenery.h
  boat_water_atlas.c boat_water_atlas.h boat_ambient.c boat_ambient.h)
target_link_libraries(proteus_boat_scenery PUBLIC proteus_boat_core)
# Validators and review tools load the packs from the source tree (tests/support/boat_test_scenery.h).
target_compile_definitions(proteus_boat_scenery INTERFACE PROTEUS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_options(proteus_boat_scenery PRIVATE ${PROTEUS_WARNINGS})
