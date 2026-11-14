add_test(NAME ci_lane_budget COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/test_ci_lane.py)
set_tests_properties(ci_lane_budget PROPERTIES LABELS "cpu;quick;ci")

# The routine CI lane is deliberately curated. Build only the game and the validators it
# actually runs; broad simulations, tool integration, repeated captures and benchmarks stay
# in the full suite. Keep this list small rather than silently promoting every new test.
add_custom_target(ci
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint/check_ci_lane.py
    --build-dir ${CMAKE_CURRENT_BINARY_DIR} --config $<CONFIG> --ctest ${CMAKE_CTEST_COMMAND}
  DEPENDS wake_velocity
  VERBATIM)
function(proteus_ci_cases target pattern)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "CI validator is missing: ${target}")
  endif()
  add_dependencies(ci ${target})
  set_property(TARGET ${target} PROPERTY PROTEUS_CI_CASES TRUE)
  foreach(test IN LISTS PROTEUS_TESTS)
    get_test_property(${test} LABELS labels)
    if(test MATCHES "${pattern}" AND "quick" IN_LIST labels)
      set_property(TEST ${test} APPEND PROPERTY LABELS ci)
    endif()
  endforeach()
endfunction()

proteus_ci_cases(validate_boat "^boat_handling[.]")
proteus_ci_cases(validate_boat_net "^boat_net[.]")
proteus_ci_cases(validate_boat_net_enet "^boat_net_enet[.]")
proteus_ci_cases(validate_boat_plugin "^boat_plugin[.]")
proteus_ci_cases(validate_boat_plugin_state "^boat_plugin_state[.]")
proteus_ci_cases(validate_boat_plugin_catalog "^boat_plugin_catalog[.]")
proteus_ci_cases(validate_boat_room_package "^boat_room_package[.]")
proteus_ci_cases(validate_boat_sandbox_save "^boat_sandbox_save$")
proteus_ci_cases(validate_boat_cargo "^boat_cargo$")
proteus_ci_cases(validate_boat_file "^boat_file$")
proteus_ci_cases(validate_app_input "^app_input$")
proteus_ci_cases(validate_waterfront_lighting "^waterfront_lighting$")

# Keep the source/library boundaries and two asset-loading paths even though some have
# historically been labelled slow. These have no multi-frame soak or preview process loop.
foreach(test test_time platform_bridges plugin_stage_backend plugin_data_boundary
    boat_core_boundary boat_net_boundary boat_screenshot boat_outer_screenshot)
  if(NOT TEST ${test})
    message(FATAL_ERROR "CI smoke check is missing: ${test}")
  endif()
  set_property(TEST ${test} APPEND PROPERTY LABELS ci)
endforeach()

# H2: the ELF checker's own logic everywhere, and on hardened Linux builds the real server
# binary (which the ci target therefore builds).
set_property(TEST binary_hardening_selftest APPEND PROPERTY LABELS ci)
if(TEST boat_server_hardening)
  add_dependencies(ci boat_server)
  set_property(TEST boat_server_hardening APPEND PROPERTY LABELS ci)
endif()

# H3: the fuzz harnesses as deterministic regressions (replay plus a fixed number of mutations).
foreach(harness fuzz_net_decode fuzz_net_server fuzz_session_record)
  if(TEST ${harness})
    add_dependencies(ci ${harness})
    set_property(TEST ${harness} APPEND PROPERTY LABELS ci)
  endif()
endforeach()

# H5 (Linux only): the lockdown child-process cases and a locked-down server soak, both well
# under a second of work.
if(TEST boat_lockdown)
  add_dependencies(ci validate_boat_lockdown boat_server boat_bot)
  set_property(TEST boat_lockdown boat_lockdown_process APPEND PROPERTY LABELS ci)
endif()

# The case tables are discovered after linking, so the ci target validates their final
# labels and test-count budget through check_ci_lane.py once all selected targets exist.
