# Boating code links from static libraries, once per executable, without cycles:
# core <- render <- gpu, and render <- session <- drive.
# Core: clock, physics, pose, locations/world/shoreline, water queries, wakes, course crossing,
# harbor/traffic contacts and boat/city geometry data. It must not draw, read input, open
# windows or audio, or touch files; boat_core_boundary checks its sources and symbols.
set(PROTEUS_BOAT_CORE_SOURCES
  boat_cargo.c boat_cargo.h
  boat_vessel_wake.c boat_vessel_wake.h
  boat.c boat.h boat_clock.h boat_course.c boat_course.h
  boat_combat.c boat_combat.h boat_fleet.c boat_fleet.h boat_match.c boat_match.h boat_race.c boat_race.h boat_location.c boat_location.h boat_map.c
  boat_map.h boat_map_tiles.c boat_map_tiles.h
  boat_model.c boat_model.h boat_pose.c boat_pose.h
  boat_room_definition.c boat_room_definition.h
  boat_scene_definition.c boat_scene_definition.h
  boat_marks.c boat_marks.h
  waterfront_flock.c waterfront_flock.h
  boat_input.h boat_helm.h boat_reset_hold.h boat_throttle.h boat_steering.h boat_view_state.h boat_wake.c boat_wake.h boat_wake_objects.c
  boat_wake_objects.h boat_wake_player.c boat_wake_player.h boat_water.c
  boat_voyage.c boat_voyage.h boat_water.h boat_world.c boat_world.h
  geometry3d.h
  waterfront_city.c waterfront_city.h waterfront_harbor.c waterfront_harbor.h
  waterfront_outer.c waterfront_outer.h
  waterfront_route.c waterfront_route.h
  waterfront_traffic.c waterfront_traffic.h
  waterfront_upstream.c waterfront_upstream.h)
add_library(proteus_boat_core STATIC ${PROTEUS_BOAT_CORE_SOURCES})
target_include_directories(proteus_boat_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
# Types and raymath only; Debug builds resolve raymath's out-of-line definitions from raylib.
target_link_libraries(proteus_boat_core PUBLIC raylib)
target_compile_options(proteus_boat_core PRIVATE ${PROTEUS_WARNINGS})
