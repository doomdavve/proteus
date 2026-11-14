# tests/lint: registered from tests/tests.cmake in the project root's directory scope.

if(NOT EMSCRIPTEN)
  # PD5: compiled selection is gone from production sources and executables; fixtures are test-only.
  add_test(NAME compiled_selection_guard COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_compiled_selection.py
    --source-dir ${CMAKE_CURRENT_SOURCE_DIR} --nm ${CMAKE_NM}
    --binary $<TARGET_FILE:wake_velocity> --binary $<TARGET_FILE:boat_server>)
  set_tests_properties(compiled_selection_guard PROPERTIES LABELS "cpu;quick" TIMEOUT 60)
  if(CMAKE_NM AND NOT MSVC)
    # Negative control: a validator that links the fixtures must trip the executable check.
    add_test(NAME compiled_selection_guard_detects COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_compiled_selection.py
      --source-dir ${CMAKE_CURRENT_SOURCE_DIR} --nm ${CMAKE_NM}
      --binary $<TARGET_FILE:validate_boat_voyage>)
    set_tests_properties(compiled_selection_guard_detects PROPERTIES LABELS "cpu;quick" TIMEOUT 60
      PASS_REGULAR_EXPRESSION "contains boat_fixture_voyage_outer")
  endif()
endif()
set(BOAT_CORE_BOUNDARY_NM)
if(CMAKE_NM AND NOT MSVC)
  set(BOAT_CORE_BOUNDARY_NM --nm ${CMAKE_NM} --archive $<TARGET_FILE:proteus_boat_core>)
endif()
# No test may wait for wall-clock time to pass (docs/TESTING.md, "No wall time").
add_test(NAME test_time COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_test_time.py --root ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(test_time PROPERTIES PASS_REGULAR_EXPRESSION "test_time=ok")
# AR3: browser bridges live in CMake-selected *_web.c backends; migrated files use their services.
add_test(NAME platform_bridges COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_platform_bridges.py --root ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(platform_bridges PROPERTIES LABELS "cpu;quick"
  PASS_REGULAR_EXPRESSION "platform_bridges=ok")
add_test(NAME platform_bridges_detects COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_platform_bridges.py --self-test)
set_tests_properties(platform_bridges_detects PROPERTIES LABELS "cpu;quick"
  PASS_REGULAR_EXPRESSION "platform_bridges_self_test=ok")
# AR4: common plugin staging holds no OS handles, process launch or JavaScript bridges.
add_test(NAME plugin_stage_backend COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_plugin_stage_backend.py
  --root ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(plugin_stage_backend PROPERTIES PASS_REGULAR_EXPRESSION
  "plugin_stage_backend=ok")
add_test(NAME plugin_stage_backend_self_test COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_plugin_stage_backend.py
  --root ${CMAKE_CURRENT_SOURCE_DIR} --self-test)
set_tests_properties(plugin_stage_backend_self_test PROPERTIES PASS_REGULAR_EXPRESSION
  "plugin_stage_backend self-test=ok")
# AR5: plugin data and its data-only consumers need no Lua, files, processes or bridges.
set(PLUGIN_DATA_BOUNDARY_NM)
if(CMAKE_NM AND NOT MSVC)
  set(PLUGIN_DATA_BOUNDARY_NM --nm ${CMAKE_NM} --archive $<TARGET_FILE:proteus_plugin_data>
    --archive $<TARGET_FILE:proteus_boat_core> --archive $<TARGET_FILE:proteus_json>
    --consumer-archive $<TARGET_FILE:proteus_sandbox_save>
    --consumer-archive $<TARGET_FILE:proteus_plugin_catalog>)
endif()
add_test(NAME plugin_data_boundary COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_plugin_data_boundary.py
  --root ${CMAKE_CURRENT_SOURCE_DIR} ${PLUGIN_DATA_BOUNDARY_NM})
set_tests_properties(plugin_data_boundary PROPERTIES PASS_REGULAR_EXPRESSION
  "plugin_data_boundary=ok")
add_test(NAME plugin_data_boundary_self_test COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_plugin_data_boundary.py
  --root ${CMAKE_CURRENT_SOURCE_DIR} --self-test)
set_tests_properties(plugin_data_boundary_self_test PROPERTIES PASS_REGULAR_EXPRESSION
  "plugin_data_boundary self-test=ok")
add_test(NAME boat_core_boundary COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_core_boundary.py --root ${CMAKE_CURRENT_SOURCE_DIR}
  ${BOAT_CORE_BOUNDARY_NM} --module core)
set_tests_properties(boat_core_boundary PROPERTIES PASS_REGULAR_EXPRESSION "core_boundary=ok")
# The net library (codec, sessions, memory transport) obeys the same rules, with no sockets.
set(BOAT_NET_BOUNDARY_NM)
if(CMAKE_NM AND NOT MSVC)
  set(BOAT_NET_BOUNDARY_NM --nm ${CMAKE_NM} --archive $<TARGET_FILE:proteus_boat_core>
    --archive $<TARGET_FILE:proteus_boat_net>)
endif()
add_test(NAME boat_net_boundary COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_core_boundary.py --root ${CMAKE_CURRENT_SOURCE_DIR}
  ${BOAT_NET_BOUNDARY_NM} --module core --module net)
set_tests_properties(boat_net_boundary PROPERTIES PASS_REGULAR_EXPRESSION "core_boundary=ok")
# Neither boundary library may link Lua or the plugin host (docs/PLUGIN_PLAN.md).
foreach(target proteus_boat_core proteus_boat_net)
  get_target_property(links ${target} LINK_LIBRARIES)
  if(links MATCHES "proteus_lua|proteus_plugins")
    message(FATAL_ERROR "${target} must not link Lua or the plugin host")
  endif()
endforeach()

add_test(NAME module_includes COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_module_includes.py --root ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME module_includes_self_test COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_module_includes.py --self-test)
set_tests_properties(module_includes module_includes_self_test PROPERTIES LABELS "cpu;quick")

# H2: the checker's own logic runs everywhere on synthetic ELF files.
add_test(NAME binary_hardening_selftest COMMAND ${Python3_EXECUTABLE} -I
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_binary_hardening.py --selftest)
set_tests_properties(binary_hardening_selftest PROPERTIES LABELS "cpu;quick" TIMEOUT 60
  PASS_REGULAR_EXPRESSION "selftest=ok")
if(PROTEUS_HARDENED)
  # The server binary built here must carry the flags; fortification needs optimization.
  set(BOAT_HARDEN_FORTIFY)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(BOAT_HARDEN_FORTIFY --fortify)
  endif()
  add_test(NAME boat_server_hardening COMMAND ${Python3_EXECUTABLE} -I
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_binary_hardening.py ${BOAT_HARDEN_FORTIFY}
    $<TARGET_FILE:boat_server>)
  set_tests_properties(boat_server_hardening PROPERTIES LABELS "cpu;quick" TIMEOUT 60
    PASS_REGULAR_EXPRESSION "boat_server: hardened")
endif()
