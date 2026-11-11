# Presentation: cosmetic simulation, CPU renderer, boat/course views, water meshes and scenes.
option(PROTEUS_WARM_MENU "Warm daylight menus (OFF selects the cyan dusk menus)" ON)
add_library(proteus_render STATIC
  boat_cargo_view.c boat_cargo_view.h
  boat_ui.c boat_ui.h
  boat_hud.c boat_hud.h boat_cabin.c boat_cabin.h boat_grodan.c
  boat_grodan.h boat_skin.c boat_skin.h boat_course_view.c
  boat_voyage_view.c boat_voyage_view.h boat_minimap.c boat_minimap.h
  boat_flag.c boat_presentation.c boat_view.c
  boat_water_mesh.c renderer3d.c waterfront.c waterfront_corner.c
  waterfront_harbor_view.c waterfront_harbor_view.h
  waterfront_lighting.c waterfront_lighting.h waterfront_scene.c
  boat_screen.c boat_screen.h waterfront_grade.c waterfront_grade.h
  waterfront_lights.c waterfront_lights.h)
target_link_libraries(proteus_render PUBLIC proteus_boat_core proteus_boat_scenery)
target_compile_definitions(proteus_render PUBLIC PROTEUS_WARM_MENU=$<BOOL:${PROTEUS_WARM_MENU}>)
target_compile_options(proteus_render PRIVATE ${PROTEUS_WARNINGS})

# GPU renderers, linked only by graphics executables.
add_library(proteus_gpu STATIC boat_gpu.c renderer_gpu.c waterfront_wake_stamp.c
  waterfront_water_history.c
  boat_rearview.c boat_rearview.h)
target_link_libraries(proteus_gpu PUBLIC proteus_render)
target_compile_options(proteus_gpu PRIVATE ${PROTEUS_WARNINGS})
