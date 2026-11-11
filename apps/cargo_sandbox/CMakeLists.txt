# The working-harbour sandbox.

if(NOT EMSCRIPTEN)
  add_executable(cargo_sandbox cargo_sandbox.c)
  target_include_directories(cargo_sandbox PRIVATE ${raylib_SOURCE_DIR}/src)
  target_link_libraries(cargo_sandbox PRIVATE proteus_gpu proteus_plugin_catalog proteus_work_session
    proteus_work_save proteus_sandbox_save proteus_file)
  add_dependencies(cargo_sandbox proteus_assets)
  target_compile_options(cargo_sandbox PRIVATE ${PROTEUS_WARNINGS})
endif()
