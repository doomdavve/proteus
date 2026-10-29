# The same wake_velocity libraries and raylib target are used by native and web builds.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# raylib's EM_ASM calls require GNU extensions; the game itself remains strict C17.
set_target_properties(raylib PROPERTIES C_EXTENSIONS ON)
set(WEB_ASSETS "${CMAKE_CURRENT_BINARY_DIR}/web-assets")
file(GLOB WEB_SHADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/waterfront_*.vs"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/waterfront_*.fs"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gpu_downsample.fs")
foreach(shader IN LISTS WEB_SHADERS)
  get_filename_component(name "${shader}" NAME)
  file(READ "${shader}" source)
  # The native review field is explicitly unavailable in the browser. Keep its
  # wide sampler out of WebGL programs, including drivers that compile dead branches.
  string(FIND "${source}" "// BEGIN NATIVE WAKE FIELD" wake_begin)
  if(NOT wake_begin EQUAL -1)
    string(FIND "${source}" "// END NATIVE WAKE FIELD" wake_end)
    if(wake_end LESS wake_begin)
      message(FATAL_ERROR "Missing native wake shader end marker: ${name}")
    endif()
    string(SUBSTRING "${source}" 0 ${wake_begin} prefix)
    string(SUBSTRING "${source}" ${wake_end} -1 suffix)
    set(source "${prefix}const int wakeFieldEnabled = 0;\nvec3 wakeFieldSample(vec2 p) { return vec3(0.0); }\nfloat wakeVertexHeight(vec2 p) { return 0.0; }\n${suffix}")
  endif()
  # These two fragments use raylib's default vertex shader (mediump varyings).
  if(name STREQUAL "waterfront_sky.fs" OR name STREQUAL "gpu_downsample.fs")
    set(precision mediump)
  else()
    set(precision highp)
  endif()
  string(REPLACE "#version 330" "#version 300 es\nprecision ${precision} float;\nprecision highp int;"
    source "${source}")
  file(GENERATE OUTPUT "${WEB_ASSETS}/shaders/${name}" CONTENT "${source}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WEB_SHADERS})
file(GLOB WEB_TEXTURES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/*.png")
foreach(texture IN LISTS WEB_TEXTURES)
  get_filename_component(name "${texture}" NAME)
  configure_file("${texture}" "${WEB_ASSETS}/textures/${name}" COPYONLY)
endforeach()
set(WEB_FONTS m5x7.ttf m3x6.ttf m5x7.fnt m3x6.fnt m5x7.png m3x6.png README.md)
foreach(font IN LISTS WEB_FONTS)
  configure_file("assets/fonts/${font}" "${WEB_ASSETS}/fonts/${font}" COPYONLY)
  set_property(TARGET wake_velocity APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/${font}")
endforeach()
configure_file(assets/title/wake-velocity.png "${WEB_ASSETS}/title/wake-velocity.png" COPYONLY)
configure_file(assets/title/river-backdrop.png "${WEB_ASSETS}/title/river-backdrop.png" COPYONLY)
configure_file(assets/music/a-little-boat.xm "${WEB_ASSETS}/music/a-little-boat.xm" COPYONLY)
configure_file(assets/music/acid-wake.xm "${WEB_ASSETS}/music/acid-wake.xm" COPYONLY)
# The river's scenery pack is preloaded with the page. The outer pack is served next to it and
# fetched only when a session needs it (web/scenery_loader.js), so river and basin players never
# download it.
configure_file(assets/geometry/river/scenery.bin "${WEB_ASSETS}/geometry/river/scenery.bin"
  COPYONLY)
configure_file(assets/geometry/outer/scenery.bin
  "${CMAKE_CURRENT_BINARY_DIR}/web-lazy/geometry/outer/scenery.bin" COPYONLY)

set_target_properties(wake_velocity PROPERTIES SUFFIX ".js")
# Asyncify is raylib's supported way to yield from its existing synchronous loop.
# Keep optimization enabled for Asyncify even in a development build.
foreach(target wake_velocity proteus_boat_core proteus_render proteus_gpu proteus_boat_session
    proteus_boat_drive proteus_boat_net proteus_plugins proteus_lua)
  target_compile_options(${target} PRIVATE -Os)
  target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
endforeach()
target_compile_options(raylib PRIVATE -Os)
target_link_options(wake_velocity PRIVATE
  -Os -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576
  -sASYNCIFY_STACK_SIZE=65536 -sENVIRONMENT=web -sEXIT_RUNTIME=1
  -lidbfs.js
  "SHELL:--preload-file \"${WEB_ASSETS}@/assets\""
  "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/web/rtc_transport.js\""
  "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/web/server_directory.js\""
  "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/web/mouse_controls.js\""
  "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/web/mobile_controls.js\""
  "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/web/scenery_loader.js\""
  "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/web/plugin_author.js\""
  "SHELL:--pre-js \"${CMAKE_CURRENT_SOURCE_DIR}/web/runtime.js\"")
set_property(TARGET wake_velocity APPEND PROPERTY LINK_DEPENDS
  ${WEB_SHADERS} ${WEB_TEXTURES}
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/title/river-backdrop.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/title/wake-velocity.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/a-little-boat.xm"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/acid-wake.xm"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/geometry/river/scenery.bin"
  "${CMAKE_CURRENT_SOURCE_DIR}/web/scenery_loader.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/web/rtc_transport.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/web/server_directory.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/web/mouse_controls.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/web/mobile_controls.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/web/runtime.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/web/plugin_author.js")

# Imported descriptors compile only inside a disposable browser Web Worker.
add_executable(plugin_stage tools/boat_plugin_stage_worker.c)
target_link_libraries(plugin_stage PRIVATE proteus_plugins)
target_compile_options(plugin_stage PRIVATE -Os ${PROTEUS_WARNINGS})
set_target_properties(plugin_stage PROPERTIES SUFFIX ".js")
target_link_options(plugin_stage PRIVATE -Os -sMODULARIZE=1 -sEXPORT_ES6=1
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sENVIRONMENT=worker -sALLOW_MEMORY_GROWTH=1
  -sSTACK_SIZE=1048576 "-sEXPORTED_FUNCTIONS=['_main']")

# Only this directory is served/deployed. Content hashes keep JS, Wasm and data in sync.
add_custom_target(web ALL
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/package_web.py
    --build-dir ${CMAKE_CURRENT_BINARY_DIR}
    --raylib-license ${raylib_SOURCE_DIR}/LICENSE --lua-license ${CMAKE_BINARY_DIR}/lua-LICENSE.txt
  DEPENDS wake_velocity plugin_stage
  VERBATIM)
add_custom_target(web-serve
  COMMAND ${Python3_EXECUTABLE} -m http.server 8000 --bind 127.0.0.1
    --directory ${CMAKE_CURRENT_BINARY_DIR}/site
  DEPENDS web
  USES_TERMINAL
  VERBATIM)

# Lua plugins P0 preflight (docs/PLUGIN_API.md): the same validator under Node, reading the
# source tree through NODERAWFS. Not part of `web` or the site.
add_executable(validate_boat_plugin tools/validate_boat_plugin.c)
target_link_libraries(validate_boat_plugin PRIVATE proteus_plugins)
target_compile_options(validate_boat_plugin PRIVATE ${PROTEUS_WARNINGS})
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-test")
target_compile_definitions(validate_boat_plugin PRIVATE
  BOAT_PLUGIN_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-test")
target_link_options(validate_boat_plugin PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
set_target_properties(validate_boat_plugin PROPERTIES SUFFIX ".js" EXCLUDE_FROM_ALL TRUE)
