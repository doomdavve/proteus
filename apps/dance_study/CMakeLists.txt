# The dance study.

add_executable(dance_study dance_study.c dance.c choreography.c ${PROJECT_SOURCE_DIR}/src/audio/xm_audio.c)
target_include_directories(dance_study PRIVATE ${PROJECT_SOURCE_DIR}/apps)
target_link_libraries(dance_study PRIVATE proteus_render)
target_compile_definitions(dance_study PRIVATE
  PROTEUS_ASSET_DIR="$<TARGET_FILE_DIR:dance_study>/assets"
)
target_compile_options(dance_study PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_dependencies(dance_study proteus_assets)
