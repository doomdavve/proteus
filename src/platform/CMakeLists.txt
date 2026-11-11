# SV1d: user-data file helpers (atomic save with backup, bounded listing). No raylib.
add_library(proteus_file STATIC boat_file.c boat_file.h)
target_include_directories(proteus_file PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(proteus_file PRIVATE ${PROTEUS_WARNINGS})

# Absolute-deadline frame pacing with a vsync backstop for windowed loops (sleeps; not core).
add_library(proteus_frame_pacer STATIC frame_pacer.c frame_pacer.h)
target_include_directories(proteus_frame_pacer PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(proteus_frame_pacer PUBLIC raylib)
target_compile_options(proteus_frame_pacer PRIVATE ${PROTEUS_WARNINGS})
