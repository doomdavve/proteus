# tests/platform: registered from tests/tests.cmake in the project root's directory scope.

add_executable(validate_frame_pacer tests/platform/validate_frame_pacer.c)
target_link_libraries(validate_frame_pacer PRIVATE proteus_frame_pacer)
target_compile_options(validate_frame_pacer PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME frame_pacer COMMAND validate_frame_pacer)

add_executable(validate_boat_capture tests/platform/validate_boat_capture.c
  apps/wake_velocity/wake_velocity_performance.c src/platform/boat_capture.c)
target_include_directories(validate_boat_capture PRIVATE ${raylib_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(validate_boat_capture PRIVATE raylib)
target_compile_options(validate_boat_capture PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_capture COMMAND validate_boat_capture ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(boat_capture PROPERTIES LABELS "cpu")
add_executable(validate_boat_file tests/platform/validate_boat_file.c)
target_link_libraries(validate_boat_file PRIVATE proteus_file)
target_compile_options(validate_boat_file PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_file COMMAND validate_boat_file ${CMAKE_CURRENT_BINARY_DIR}/file-tests)
set_tests_properties(boat_file PROPERTIES LABELS "cpu;quick")
