# Lua plugins P0 (docs/PLUGIN_PLAN.md, docs/PLUGIN_API.md). Lua 5.4.9 (MIT; notice at the end of
# lua.h), pinned by lua.org's published SHA-256. Only the VM and the base, string, table and math
# libraries are compiled: io, os, debug, package/require, coroutine and utf8 do not exist. A fixed
# string-hash seed keeps table traversal, and so diagnostics, identical from run to run.
include(${PROJECT_SOURCE_DIR}/cmake/LuaSource.cmake)
FetchContent_MakeAvailable(lua)
# Lua's MIT notice, from the end of lua.h, for the native packages (lua-LICENSE.txt).
file(READ ${lua_SOURCE_DIR}/src/lua.h PROTEUS_LUA_HEADER)
string(REGEX MATCH "\\* Copyright \\(C\\)[^\n]*\n(\\*[^\n]*\n)*" PROTEUS_LUA_NOTICE
  "${PROTEUS_LUA_HEADER}")
string(REGEX REPLACE "(^|\n)\\*+ ?" "\\1" PROTEUS_LUA_NOTICE "${PROTEUS_LUA_NOTICE}")
string(STRIP "${PROTEUS_LUA_NOTICE}" PROTEUS_LUA_NOTICE)
if(NOT PROTEUS_LUA_NOTICE MATCHES "^Copyright \\(C\\) 1994-[0-9]+ Lua.org, PUC-Rio\\..*NONINFRINGEMENT")
  message(FATAL_ERROR "Cannot find Lua's license notice in lua.h")
endif()
file(WRITE ${PROJECT_BINARY_DIR}/lua-LICENSE.txt "${PROTEUS_LUA_NOTICE}\n")
set(PROTEUS_LUA_SOURCES lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes
  lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lstrlib ltablib lmathlib)
list(TRANSFORM PROTEUS_LUA_SOURCES PREPEND ${lua_SOURCE_DIR}/src/)
list(TRANSFORM PROTEUS_LUA_SOURCES APPEND .c)
add_library(proteus_lua STATIC ${PROTEUS_LUA_SOURCES})
target_include_directories(proteus_lua SYSTEM PUBLIC ${lua_SOURCE_DIR}/src)
set_target_properties(proteus_lua PROPERTIES C_STANDARD 99)
target_compile_options(proteus_lua PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-w;-include;${PROJECT_SOURCE_DIR}/cmake/proteus_lua.h>
  $<$<C_COMPILER_ID:MSVC>:/FI${PROJECT_SOURCE_DIR}/cmake/proteus_lua.h>)
if(NOT MSVC)
  target_link_libraries(proteus_lua PRIVATE m)
endif()

# AR5: plugin data and codecs. Manifest parsing, descriptor decoding and checks, digests, the
# stage wire codec and the objective checkpoint codec over caller-owned bytes; no Lua, files,
# processes or browser bridges (plugin_data_boundary). Data-only consumers link only this.
add_library(proteus_plugin_data STATIC boat_plugin.c boat_plugin.h
  boat_plugin_decode.h boat_plugin_revision.h boat_plugin_race.c
  boat_plugin_objective.h boat_plugin_objective_codec.c boat_plugin_wire.c
  boat_plugin_wire.h)
target_link_libraries(proteus_plugin_data PUBLIC proteus_boat_core PRIVATE proteus_json)
target_compile_options(proteus_plugin_data PRIVATE ${PROTEUS_WARNINGS})

# The plugin host: restricted Lua evaluation, revisions, objective transitions, isolated
# staging and boat_plugin_io for reading a package directory. Core and net never link it.
add_library(proteus_plugins STATIC boat_plugin_load.c boat_plugin_load.h
  boat_plugin_lua.c boat_plugin_lua.h boat_plugin_io.c boat_plugin_io.h
  boat_plugin_host.c boat_plugin_host.h boat_plugin_state.c boat_plugin_state.h)
target_link_libraries(proteus_plugins PUBLIC proteus_plugin_data PRIVATE proteus_json proteus_lua)
target_compile_options(proteus_plugins PRIVATE ${PROTEUS_WARNINGS})
target_sources(proteus_plugins PRIVATE boat_plugin_stage.c boat_plugin_stage.h
  boat_plugin_stage_backend.h boat_plugin_eval.c boat_plugin_eval.h)
# AR4: one isolated-worker backend per platform behind the private boat_plugin_stage_backend.h.
if(EMSCRIPTEN)
  target_sources(proteus_plugins PRIVATE boat_plugin_stage_web.c)
elseif(WIN32)
  target_sources(proteus_plugins PRIVATE boat_plugin_stage_windows.c)
else()
  target_sources(proteus_plugins PRIVATE boat_plugin_stage_posix.c)
endif()

# PD1: bundled packages are evaluated and fully validated by a build-time generator. The
# generated table and the lookup/decode helpers form a library that depends on proteus_plugins;
# the generator itself never links the catalog, so there is no cycle. PD4 builds the same
# generator as a Node program for the browser and lets CMake run it through the cross-compiling
# emulator; only solo entries are linked into the game.
set(BOAT_PLUGIN_PACKAGES
  plugins/basin-free plugins/outer-free plugins/time-trial
  plugins/langedrag plugins/river-sprint plugins/harbor-life plugins/deathmatch
  plugins/harbor-room plugins/harbor-race plugins/grodan plugins/outer-work
  plugins/basin-work plugins/outer-hunt)
if(EMSCRIPTEN)
  set(BOAT_PLUGIN_CATALOG_PACKAGES
    plugins/basin-free plugins/time-trial plugins/langedrag
    plugins/harbor-life plugins/grodan)
else()
  set(BOAT_PLUGIN_CATALOG_PACKAGES ${BOAT_PLUGIN_PACKAGES})
endif()
set(BOAT_PLUGIN_PACKAGE_FILES)
foreach(package IN LISTS BOAT_PLUGIN_CATALOG_PACKAGES)
  list(APPEND BOAT_PLUGIN_PACKAGE_FILES
    ${PROJECT_SOURCE_DIR}/${package}/plugin.json
    ${PROJECT_SOURCE_DIR}/${package}/main.lua)
endforeach()
add_executable(boat_plugin_catalog_gen boat_plugin_catalog_gen.c)
target_link_libraries(boat_plugin_catalog_gen PRIVATE proteus_plugins)
target_compile_options(boat_plugin_catalog_gen PRIVATE ${PROTEUS_WARNINGS})
if(EMSCRIPTEN)
  # Build-time only: the game never links this Node program or its Lua.
  set_target_properties(boat_plugin_catalog_gen PROPERTIES SUFFIX ".js")
  target_link_options(boat_plugin_catalog_gen PRIVATE
    -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB)
endif()
set(BOAT_PLUGIN_CATALOG_GEN $<TARGET_FILE:boat_plugin_catalog_gen>)
if(CMAKE_CROSSCOMPILING_EMULATOR)
  set(BOAT_PLUGIN_CATALOG_GEN ${CMAKE_CROSSCOMPILING_EMULATOR} ${BOAT_PLUGIN_CATALOG_GEN})
endif()
set(BOAT_PLUGIN_CATALOG_C "${PROJECT_BINARY_DIR}/generated/boat_plugin_catalog_data.c")
add_custom_command(OUTPUT ${BOAT_PLUGIN_CATALOG_C}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/generated"
  COMMAND ${BOAT_PLUGIN_CATALOG_GEN} --output ${BOAT_PLUGIN_CATALOG_C}
    ${BOAT_PLUGIN_CATALOG_PACKAGES}
  DEPENDS boat_plugin_catalog_gen ${BOAT_PLUGIN_PACKAGE_FILES}
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Generating the bundled plugin catalog"
  VERBATIM)

add_library(proteus_plugin_catalog STATIC
  boat_plugin_catalog.c boat_plugin_catalog.h ${BOAT_PLUGIN_CATALOG_C})
# Lookup and decode of pre-evaluated wire bytes: plugin data only, no Lua.
target_link_libraries(proteus_plugin_catalog PUBLIC proteus_plugin_data)
target_compile_options(proteus_plugin_catalog PRIVATE ${PROTEUS_WARNINGS})
