# tests/audio: registered from tests/tests.cmake in the project root's directory scope.

add_executable(validate_xm
  tests/audio/validate_xm.c
  src/audio/xm_audio.c
)
target_include_directories(validate_xm PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${raylib_SOURCE_DIR}/src
  ${raylib_SOURCE_DIR}/src/external
)
if(NOT MSVC)
  target_link_libraries(validate_xm PRIVATE m)
endif()
add_executable(validate_boat_audio tests/audio/validate_boat_audio.c src/audio/boat_audio.c)
target_include_directories(validate_boat_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(validate_boat_audio PRIVATE raylib)
target_compile_options(validate_boat_audio PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME boat_audio_is_valid COMMAND validate_boat_audio)
add_test(NAME starter_xm_is_valid
  COMMAND validate_xm ${CMAKE_CURRENT_SOURCE_DIR}/assets/music/starter.xm
)

add_test(NAME boating_xm_is_valid
  COMMAND validate_xm ${CMAKE_CURRENT_SOURCE_DIR}/assets/music/a-little-boat.xm --background)
add_test(NAME acid_xm_is_valid
  COMMAND validate_xm ${CMAKE_CURRENT_SOURCE_DIR}/assets/music/acid-wake.xm --acid)
