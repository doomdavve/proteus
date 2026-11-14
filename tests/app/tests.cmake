# tests/app: registered from tests/tests.cmake in the project root's directory scope.

if(NOT EMSCRIPTEN)
  add_executable(validate_boat_directory tests/app/validate_boat_directory.c apps/wake_velocity/boat_directory.c)
  target_link_libraries(validate_boat_directory PRIVATE proteus_boat_net_enet CURL::libcurl
    Threads::Threads proteus_json)
  target_compile_options(validate_boat_directory PRIVATE ${PROTEUS_WARNINGS})
  add_test(NAME boat_directory COMMAND validate_boat_directory)
  set_tests_properties(boat_directory PROPERTIES LABELS "cpu;quick")
endif()

# T3: title settings persistence and the title's lighting hand-off into each launch mode. The
# build-tree data directory keeps the player's settings out of the test.
set(BOAT_TITLE_SETTINGS_DIR "${CMAKE_CURRENT_BINARY_DIR}/boat-title-settings-tests")
file(MAKE_DIRECTORY "${BOAT_TITLE_SETTINGS_DIR}")
add_executable(validate_boat_title tests/app/validate_boat_title.c apps/wake_velocity/boat_title.c
  apps/wake_velocity/wake_velocity_input.c apps/wake_velocity/wake_velocity_controls.c)
target_include_directories(validate_boat_title PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# The title reaches discovery only through wake_velocity_servers.h (stubbed here), so it links
# the pure protocol for LAN room lists and no transport.
target_link_libraries(validate_boat_title PRIVATE proteus_render proteus_work_save
  proteus_frame_pacer proteus_boat_net)
target_compile_options(validate_boat_title PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_title_settings COMMAND validate_boat_title "${BOAT_TITLE_SETTINGS_DIR}")
set_tests_properties(boat_title_settings PROPERTIES LABELS "cpu")
if(NOT EMSCRIPTEN)
  # The room command line wake_velocity --host passes to its --serve child.
  add_executable(validate_wake_velocity_host tests/app/validate_wake_velocity_host.c
    apps/wake_velocity/wake_velocity_host.c)
  target_include_directories(validate_wake_velocity_host PRIVATE tools)
  target_compile_options(validate_wake_velocity_host PRIVATE ${PROTEUS_WARNINGS})
  proteus_add_case_tests(wake_velocity_host validate_wake_velocity_host LABELS cpu quick)
endif()
# The pause menu's input handling, driven by synthetic BoatAppInput values (no window or raylib
# input reads).
add_executable(validate_app_input tests/app/validate_app_input.c apps/wake_velocity/wake_velocity_menu.c)
target_link_libraries(validate_app_input PRIVATE proteus_render)
target_compile_options(validate_app_input PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME app_input COMMAND validate_app_input)
set_tests_properties(app_input PROPERTIES LABELS "cpu;quick")

if(NOT EMSCRIPTEN)
  foreach(side left right)
    add_test(NAME cargo_sandbox_${side}_screenshot COMMAND cargo_sandbox --location basin --renderer cpu
      --scenario loaded-${side}
      --screenshot ${CMAKE_CURRENT_BINARY_DIR}/cargo-${side}-test.png)
    add_test(NAME cargo_sandbox_outer_${side}_screenshot COMMAND cargo_sandbox --renderer cpu
      --scenario loaded-${side}
      --screenshot ${CMAKE_CURRENT_BINARY_DIR}/cargo-outer-${side}-test.png)
  endforeach()
  add_test(NAME cargo_sandbox_site COMMAND cargo_sandbox --survey)
  add_test(NAME cargo_sandbox_scene_survives_reset COMMAND cargo_sandbox --scene-check)
  add_test(NAME cargo_shadow_wave_cpu COMMAND cargo_sandbox --location basin --renderer cpu --shadow-review)
  if(PROTEUS_GPU_TESTS)
    add_test(NAME cargo_shadow_wave_gpu COMMAND cargo_sandbox --renderer gpu --shadow-review)
    set_tests_properties(cargo_shadow_wave_gpu PROPERTIES LABELS "gpu" TIMEOUT 30)
  endif()
  foreach(scenario shadow-high shadow-low remoor crossing receiver-approach delivered
    landing-built redelivered job-reset build-blocked quay-pickup jetty-approach jetty-delivered)
    add_test(NAME cargo_sandbox_${scenario}_screenshot COMMAND cargo_sandbox --renderer cpu
      --scenario ${scenario}
      --screenshot ${CMAKE_CURRENT_BINARY_DIR}/cargo-${scenario}-test.png)
  endforeach()
  # SV1d: a staged session is saved to a build-tree file, resumed by a later run and captured
  # (the same landing-built frame as its screenshot test), and a save written by a package with
  # a different work digest is set aside and the run starts fresh. Nothing touches the user's data
  # directory because every run names its file.
  set(SANDBOX_SAVE_DIR ${CMAKE_CURRENT_BINARY_DIR}/sandbox-save-tests)
  file(MAKE_DIRECTORY ${SANDBOX_SAVE_DIR})
  add_test(NAME cargo_sandbox_save_write COMMAND cargo_sandbox --scenario landing-built
    --save ${SANDBOX_SAVE_DIR}/resume.sav --save-now)
  add_test(NAME cargo_sandbox_save_resume COMMAND cargo_sandbox --renderer cpu
    --save ${SANDBOX_SAVE_DIR}/resume.sav --expect resumed
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/cargo-save-resume-test.png)
  add_test(NAME cargo_sandbox_save_stale COMMAND cargo_sandbox --scenario landing-built
    --save ${SANDBOX_SAVE_DIR}/stale.sav --save-now --stale-digest)
  add_test(NAME cargo_sandbox_save_rejected COMMAND cargo_sandbox --renderer cpu
    --save ${SANDBOX_SAVE_DIR}/stale.sav --expect rejected
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/cargo-save-rejected-test.png)
  set_tests_properties(cargo_sandbox_save_write PROPERTIES FIXTURES_SETUP sandbox_save_resume)
  set_tests_properties(cargo_sandbox_save_resume PROPERTIES FIXTURES_REQUIRED sandbox_save_resume)
  set_tests_properties(cargo_sandbox_save_stale PROPERTIES FIXTURES_SETUP sandbox_save_rejected)
  set_tests_properties(cargo_sandbox_save_rejected PROPERTIES FIXTURES_REQUIRED
    sandbox_save_rejected)
  # The window loop's automatic saving on a virtual clock (2 ticks per frame): a periodic
  # write, a milestone, pausing and quitting, then a later run resumes the last write.
  add_test(NAME cargo_sandbox_autosave COMMAND cargo_sandbox --autosave-check --scenario departure
    --save ${SANDBOX_SAVE_DIR}/autosave.sav)
  add_test(NAME cargo_sandbox_autosave_resume COMMAND cargo_sandbox --renderer cpu
    --save ${SANDBOX_SAVE_DIR}/autosave.sav --expect resumed
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/cargo-autosave-resume-test.png)
  set_tests_properties(cargo_sandbox_autosave PROPERTIES FIXTURES_SETUP sandbox_autosave)
  set_tests_properties(cargo_sandbox_autosave_resume PROPERTIES FIXTURES_REQUIRED sandbox_autosave)
  list(APPEND PROTEUS_RESUME_TESTS cargo_sandbox_autosave cargo_sandbox_autosave_resume)
  list(APPEND PROTEUS_RESUME_TESTS cargo_sandbox_save_write cargo_sandbox_save_resume
    cargo_sandbox_save_stale cargo_sandbox_save_rejected)
  # SV1c: save the staged session, resume it from the record and from its encoded bytes, and
  # require equal behaviour over 1200 scripted ticks. No window and no files.
  foreach(scenario swing departure crossing delivered landing-built build-blocked quay-pickup
    jetty-delivered delivery-pending capsized)
    add_test(NAME cargo_sandbox_resume_${scenario} COMMAND cargo_sandbox --resume-check
      --scenario ${scenario})
    list(APPEND PROTEUS_RESUME_TESTS cargo_sandbox_resume_${scenario})
  endforeach()
  list(APPEND PROTEUS_RESUME_TESTS cargo_sandbox_scene_survives_reset)
endif()

add_executable(validate_boat_drive tests/app/validate_boat_drive.c)
target_link_libraries(validate_boat_drive PRIVATE proteus_boat_drive)
target_compile_options(validate_boat_drive PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>)
add_test(NAME boat_drive_extraction COMMAND validate_boat_drive)
# Time trials ride the wake pool; only the scripted crossing boat stays free-boating only.
add_test(NAME wake_objects_course_rejected COMMAND wake_study --wake-objects-crossing --course)
add_test(NAME wake_objects_lifetime_rejected COMMAND wake_velocity --wake-objects --wake-life-ticks 1921)
set_tests_properties(wake_objects_course_rejected
  wake_objects_lifetime_rejected PROPERTIES WILL_FAIL TRUE)

if(NOT EMSCRIPTEN)
  # P4.6 live tool bridge over request files; --serve backs the client's end-to-end checks.
  add_executable(validate_wake_bridge tests/app/validate_wake_bridge.c apps/wake_velocity/wake_velocity_bridge.c
    apps/wake_velocity/wake_velocity_plugin.c apps/wake_velocity/wake_command.c)
  target_link_libraries(validate_wake_bridge PRIVATE proteus_plugins proteus_wake_objective proteus_plugin_catalog proteus_boat_session)
  target_compile_options(validate_wake_bridge PRIVATE ${PROTEUS_WARNINGS})
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wake-bridge-test")
  target_compile_definitions(validate_wake_bridge PRIVATE
    BOAT_BRIDGE_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/wake-bridge-test")
  add_test(NAME wake_velocity_bridge COMMAND validate_wake_bridge)
  set_tests_properties(wake_velocity_bridge PROPERTIES LABELS "cpu"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 30)
  # PC: the command layer, console input/scrollback and live scene application, headless under a
  # virtual clock and through the real isolated worker.
  add_executable(validate_wake_command tests/app/validate_wake_command.c
    apps/wake_velocity/wake_velocity_plugin.c apps/wake_velocity/wake_command.c apps/wake_velocity/wake_console.c
    apps/wake_velocity/wake_console_bridge.c)
  target_link_libraries(validate_wake_command PRIVATE proteus_plugins proteus_wake_objective proteus_plugin_catalog
    proteus_boat_session)
  target_compile_options(validate_wake_command PRIVATE ${PROTEUS_WARNINGS})
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wake-command-test")
  target_compile_definitions(validate_wake_command PRIVATE
    WAKE_COMMAND_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/wake-command-test")
  add_test(NAME wake_command COMMAND validate_wake_command)
  set_tests_properties(wake_command PROPERTIES LABELS "cpu"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 60)
  add_test(NAME boat_plugin_author COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_plugin_author.py
    --worker $<TARGET_FILE:boat_plugin_worker>
    --bridge-server $<TARGET_FILE:validate_wake_bridge>)
  # Four packages are previewed twice; Debug CI can exceed the old 10 s child deadline.
  set_tests_properties(boat_plugin_author PROPERTIES LABELS "cpu" TIMEOUT 180)
  # P4.5 pinned frame captures through the game's CPU screenshot path, also from a P4.7
  # release layout of the authoring tools.
  add_test(NAME boat_plugin_capture COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/test_plugin_author.py
    --worker $<TARGET_FILE:boat_plugin_worker> --game $<TARGET_FILE:wake_velocity> Capture
    Packaged)
  set_tests_properties(boat_plugin_capture PROPERTIES LABELS "render" TIMEOUT 120)
endif()
# AR6 (L0): the private local session against the game's direct free-boating path.
add_executable(validate_boat_local tests/app/validate_boat_local.c apps/wake_velocity/wake_velocity_plugin.c)
target_link_libraries(validate_boat_local PRIVATE proteus_local_drive proteus_boat_fixtures
  proteus_plugins proteus_wake_objective proteus_plugin_catalog proteus_boat_session)
target_compile_options(validate_boat_local PRIVATE ${PROTEUS_WARNINGS})
# authoring_trace opens plugins/shelter-tour; the saves case writes in the build directory.
target_compile_definitions(validate_boat_local PRIVATE
  BOAT_LOCAL_PLUGIN="${CMAKE_CURRENT_SOURCE_DIR}/plugins/shelter-tour")
proteus_add_case_tests(boat_local validate_boat_local LABELS core cpu)
# Every PLACE the title offers opens its package through the title's own arguments.
foreach(place_package
    "basin=proteus.basin-free" "outer-standard=proteus.langedrag"
    "outer-harbor-life=proteus.harbor-life" "outer-grodan=proteus.grodan"
    "outer-canal-tour=proteus.grodan"
    "basin-trial=proteus.time-trial")
  string(REPLACE "=" ";" place_package "${place_package}")
  list(GET place_package 0 place)
  list(GET place_package 1 package)
  add_test(NAME wake_velocity_title_start.${place}
    COMMAND wake_velocity --title-start ${place} --renderer cpu
      --screenshot ${CMAKE_CURRENT_BINARY_DIR}/title_start_${place}.png --time 0.1
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(wake_velocity_title_start.${place} PROPERTIES TIMEOUT 60
    PASS_REGULAR_EXPRESSION "Plugin ${package}:")
endforeach()
# WORKING HARBOUR starts through --work instead of --package. Unlike --package, --work prints no
# package banner, so the exit status and the screenshot are the check.
add_test(NAME wake_velocity_title_start.outer-working-harbour
  COMMAND wake_velocity --title-start outer-working-harbour --renderer cpu
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/title_start_outer-working-harbour.png --time 0.1
    --data-dir ${CMAKE_CURRENT_BINARY_DIR}/work-save-tests-title
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(wake_velocity_title_start.outer-working-harbour PROPERTIES TIMEOUT 60)
add_test(NAME wake_velocity_title_start_rejected COMMAND wake_velocity --title-start nowhere)
set_tests_properties(wake_velocity_title_start_rejected PROPERTIES WILL_FAIL TRUE)
add_test(NAME boat_net_study_mode_rejected COMMAND wake_velocity --connect 127.0.0.1:24680 --waves)
add_test(NAME boat_net_study_name_rejected COMMAND wake_velocity --name lonely)
add_test(NAME boat_net_study_skin_rejected
  COMMAND wake_velocity --connect 127.0.0.1:24680 --skin norway)
# Hosting from the game: wake_velocity --serve is the room, --host starts it as a child and joins.
add_test(NAME wake_velocity_serve_scripted COMMAND wake_velocity --serve --scripted --ticks 240)
set_tests_properties(wake_velocity_serve_scripted PROPERTIES
  PASS_REGULAR_EXPRESSION "boat_server scripted: boats=4 ticks=240")
add_test(NAME wake_velocity_host_race_location_rejected
  COMMAND wake_velocity --host race --location basin)
add_test(NAME wake_velocity_host_hunt_location_rejected
  COMMAND wake_velocity --host hunt --location basin)
add_test(NAME wake_velocity_host_connect_rejected
  COMMAND wake_velocity --host free --connect 127.0.0.1:24680)
add_test(NAME wake_velocity_port_without_host_rejected COMMAND wake_velocity --port 24680)
add_test(NAME boat_host COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/app/boat_host_check.py --study $<TARGET_FILE:wake_velocity>
  --bot $<TARGET_FILE:boat_bot>)
set_tests_properties(boat_host PROPERTIES PASS_REGULAR_EXPRESSION "boat_host=ok" TIMEOUT 30)
if(PROTEUS_GPU_TESTS)
  # A windowed host session on a private port: start the room, join it, leave, close it.
  add_test(NAME wake_velocity_host_gpu
    COMMAND wake_velocity --host deathmatch --location outer --port 24779 --frames 120)
  set_tests_properties(wake_velocity_host_gpu PROPERTIES LABELS "gpu" RESOURCE_LOCK gpu TIMEOUT 60
    PASS_REGULAR_EXPRESSION "standard input closed; closing the room")
endif()
# Lua plugin P1: the reference plugin's outing at the outer location, and the modes it refuses.
add_test(NAME wake_velocity_plugin_screenshot COMMAND wake_velocity --renderer cpu
  --plugin plugins/langedrag --time 3 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-plugin.png)
add_test(NAME wake_velocity_plugin_location_rejected COMMAND wake_velocity
  --plugin plugins/langedrag --location basin)
add_test(NAME wake_velocity_plugin_course_rejected COMMAND wake_velocity
  --plugin plugins/langedrag --course)
add_test(NAME wake_velocity_plugin_room_rejected COMMAND wake_velocity
  --plugin plugins/langedrag --connect 127.0.0.1:24680)
add_test(NAME wake_velocity_plugin_missing_rejected COMMAND wake_velocity
  --plugin plugins/missing --renderer cpu --time 1 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-plugin-missing.png)
set_tests_properties(wake_velocity_plugin_location_rejected wake_velocity_plugin_course_rejected
  wake_velocity_plugin_room_rejected wake_velocity_plugin_missing_rejected PROPERTIES WILL_FAIL TRUE)
set_tests_properties(wake_velocity_plugin_screenshot wake_velocity_plugin_location_rejected
  wake_velocity_plugin_course_rejected wake_velocity_plugin_room_rejected
  wake_velocity_plugin_missing_rejected PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME boat_player_wake_screenshot COMMAND wake_study --renderer cpu --player-wake --time 9
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-player-wake.png)
add_test(NAME boat_player_wake_course_rejected COMMAND wake_study --player-wake --course)
set_tests_properties(boat_player_wake_course_rejected PROPERTIES WILL_FAIL TRUE)
# D6 (L8): the game names wake_study for each developer study instead of running it.
foreach(study player-wake no-player-wake wake-objects-crossing)
  add_test(NAME wake_velocity_study_rejected.${study} COMMAND wake_velocity --${study})
  set_tests_properties(wake_velocity_study_rejected.${study} PROPERTIES
    PASS_REGULAR_EXPRESSION "--${study} is a developer study: run it with wake_study" LABELS quick)
endforeach()
add_test(NAME boat_current_screenshot COMMAND wake_velocity --renderer cpu --current --time 5
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-current.png)
add_test(NAME boat_crossing_wake_screenshot COMMAND wake_velocity --renderer cpu --crossing-wake --time 9
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-crossing-wake.png)
add_test(NAME boat_environment_screenshot COMMAND wake_velocity --renderer cpu --current --crossing-wake
  --waves --time 9 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-environment.png)
add_test(NAME boat_environment_course_rejected COMMAND wake_velocity --current --course)
add_test(NAME boat_environment_outer_screenshot COMMAND wake_velocity --renderer cpu
  --location outer --current --crossing-wake --time 3
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-outer-environment.png)
set_tests_properties(boat_environment_course_rejected PROPERTIES WILL_FAIL TRUE)
add_test(NAME boat_default_player_wake_screenshot COMMAND wake_velocity --renderer cpu --time 9
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-default-player-wake.png)
add_test(NAME boat_object_wake_screenshot COMMAND wake_velocity --renderer cpu --wake-objects --time 9
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-object-wake.png)
set_tests_properties(boat_object_wake_screenshot PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(boat_default_player_wake_screenshot boat_object_wake_screenshot
  PROPERTIES FIXTURES_SETUP player_wake_defaults)
add_test(NAME boat_default_wake_matches_explicit COMMAND ${CMAKE_COMMAND} -E compare_files
  ${CMAKE_CURRENT_BINARY_DIR}/boat-default-player-wake.png
  ${CMAKE_CURRENT_BINARY_DIR}/boat-object-wake.png)
set_tests_properties(boat_default_wake_matches_explicit PROPERTIES FIXTURES_REQUIRED player_wake_defaults)
add_test(NAME boat_replay_screenshot COMMAND wake_velocity --renderer cpu
  --replay ${BOAT_REPLAY_TEST_DIR}/replay-fixture.run --time 5
  --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-replay.png)
add_test(NAME boat_ghost_screenshot COMMAND wake_velocity --renderer cpu
  --ghost ${BOAT_REPLAY_TEST_DIR}/replay-fixture.run --time 0
  --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-ghost.png)
add_test(NAME boat_saved_best_screenshot COMMAND wake_velocity --renderer cpu --course --time 0
  --data-dir ${BOAT_REPLAY_TEST_DIR}
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-saved-best.png)
add_test(NAME boat_ghost_wrong_water COMMAND wake_velocity --renderer cpu --waves
  --ghost ${BOAT_REPLAY_TEST_DIR}/replay-fixture.run --time 0
  --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-ghost-invalid.png)
set_tests_properties(boat_ghost_wrong_water PROPERTIES WILL_FAIL TRUE)
set_tests_properties(boat_replay_screenshot boat_ghost_screenshot boat_saved_best_screenshot boat_ghost_wrong_water
  PROPERTIES FIXTURES_REQUIRED boat_recording)
add_test(NAME boat_course_screenshot COMMAND wake_velocity --renderer cpu --course --time 0
  --data-dir ${BOAT_REPLAY_TEST_DIR}/profile
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-course.png)
add_test(NAME boat_wave_screenshot COMMAND wake_velocity --renderer cpu --waves --time 1.8
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-wave.png)
add_test(NAME boat_screenshot COMMAND wake_velocity --renderer cpu --time 3
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-screenshot.png)
add_test(NAME boat_cpu_aa_option COMMAND wake_velocity --renderer cpu --gpu-aa --time 0
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-cpu-aa-option.png)
add_test(NAME boat_cpu_aa_option_reversed COMMAND wake_velocity --gpu-aa --renderer cpu --time 0
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-cpu-aa-option-reversed.png)
add_test(NAME boat_outer_screenshot COMMAND wake_velocity --renderer cpu --location outer --time 0
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-outer.png)
add_test(NAME boat_lighting_hour_screenshot COMMAND wake_velocity --renderer cpu --location outer
  --lighting-hour 12 --time 0 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-lighting-hour.png)
add_test(NAME boat_lighting_hour_precedence_a COMMAND wake_velocity --renderer cpu --location outer
  --daylight sunset --lighting-hour 12 --time 0
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-lighting-hour-precedence-a.png)
add_test(NAME boat_lighting_hour_precedence_b COMMAND wake_velocity --renderer cpu --location outer
  --lighting-hour 12 --daylight sunset --time 0
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-lighting-hour-precedence-b.png)
add_test(NAME boat_lighting_hour_precedence_matches COMMAND ${CMAKE_COMMAND} -E compare_files
  ${CMAKE_CURRENT_BINARY_DIR}/boat-lighting-hour-precedence-a.png
  ${CMAKE_CURRENT_BINARY_DIR}/boat-lighting-hour-precedence-b.png)
add_test(NAME boat_lighting_hour_precedence_reference_matches COMMAND ${CMAKE_COMMAND} -E
  compare_files ${CMAKE_CURRENT_BINARY_DIR}/boat-lighting-hour.png
  ${CMAKE_CURRENT_BINARY_DIR}/boat-lighting-hour-precedence-a.png)
add_test(NAME boat_lighting_hour_rejected COMMAND wake_velocity --renderer cpu --location outer
  --lighting-hour nan --time 0 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/boat-lighting-hour-bad.png)
set_tests_properties(boat_lighting_hour_rejected PROPERTIES WILL_FAIL TRUE)
set_tests_properties(boat_lighting_hour_screenshot boat_lighting_hour_precedence_a
  boat_lighting_hour_precedence_b PROPERTIES FIXTURES_SETUP lighting_hour_precedence)
set_tests_properties(boat_lighting_hour_precedence_matches
  boat_lighting_hour_precedence_reference_matches
  PROPERTIES FIXTURES_REQUIRED lighting_hour_precedence)
set_tests_properties(boat_lighting_hour_screenshot boat_lighting_hour_precedence_a
  boat_lighting_hour_precedence_b boat_lighting_hour_precedence_matches
  boat_lighting_hour_precedence_reference_matches boat_lighting_hour_rejected
  PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# SV1f: the working-harbour session in the game, from the scripted drive: the berth, and the
# landing delivery (idle for 10 s, then the delivery script). --work refuses incompatible options.
add_test(NAME wake_velocity_work_cpu_screenshot COMMAND wake_velocity --renderer cpu
  --location outer --work --time 3 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/wake-work-berth.png)
add_test(NAME wake_velocity_work_delivered_cpu_screenshot COMMAND wake_velocity --renderer cpu
  --location outer --work --time 14
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/wake-work-delivered.png)
# SV1g: a scripted delivery is saved to a build-tree file by one run and resumed by the next,
# which must say so (and exit 0); nothing touches the user's data directory.
set(WORK_SAVE_DIR ${CMAKE_CURRENT_BINARY_DIR}/work-save-tests)
file(MAKE_DIRECTORY ${WORK_SAVE_DIR})
add_test(NAME wake_velocity_work_save_write COMMAND ${CMAKE_COMMAND} "-DREGEX=Wrote save .*work.sav"
  -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_expect.cmake -- $<TARGET_FILE:wake_velocity>
  --renderer cpu --location outer --work --time 14 --save ${WORK_SAVE_DIR}/work.sav --save-now)
add_test(NAME wake_velocity_work_save_resume COMMAND ${CMAKE_COMMAND}
  "-DREGEX=Resumed save .*work.sav at tick" -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_expect.cmake
  -- $<TARGET_FILE:wake_velocity> --renderer cpu --location outer --work
  --save ${WORK_SAVE_DIR}/work.sav --camera-distance 30 --camera-height 20
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/wake-work-resumed.png)
set_tests_properties(wake_velocity_work_save_write PROPERTIES FIXTURES_SETUP work_save)
set_tests_properties(wake_velocity_work_save_resume PROPERTIES FIXTURES_REQUIRED work_save)
add_test(NAME wake_velocity_work_refuses_package COMMAND wake_velocity --renderer cpu
  --location outer --work --package outer-work --time 0
  --screenshot ${CMAKE_CURRENT_BINARY_DIR}/wake-work-refused.png)
set_tests_properties(wake_velocity_work_refuses_package PROPERTIES WILL_FAIL TRUE)
# Fast developer CLI validation: each case rejects before a window or capture starts.
add_test(NAME wake_velocity_time_scale_nan_rejected COMMAND wake_velocity --time-scale nan)
add_test(NAME wake_velocity_time_scale_unsupported_rejected COMMAND wake_velocity --time-scale 3)
add_test(NAME wake_velocity_time_scale_missing_rejected COMMAND wake_velocity --time-scale)
add_test(NAME wake_velocity_time_scale_multiplayer_rejected
  COMMAND wake_velocity --host free --time-scale 2)
add_test(NAME wake_velocity_work_review_pose_name_rejected
  COMMAND wake_velocity --work --work-review-pose pirouette)
add_test(NAME wake_velocity_work_review_pose_without_work_rejected
  COMMAND wake_velocity --renderer cpu --work-review-pose swing
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/work-review-unused.png --time 0)
add_test(NAME wake_velocity_work_review_pose_unbounded_rejected
  COMMAND wake_velocity --renderer cpu --location outer --work --work-review-pose swing)
add_test(NAME wake_velocity_work_clock_report_missing_arg_rejected
  COMMAND wake_velocity --work-clock-report)
add_test(NAME wake_velocity_work_clock_report_empty_rejected
  COMMAND wake_velocity --work-clock-report "")
add_test(NAME wake_velocity_work_clock_report_without_frames_rejected
  COMMAND wake_velocity --work --work-clock-report ${CMAKE_CURRENT_BINARY_DIR}/work-clock.csv)
set_tests_properties(
  wake_velocity_time_scale_nan_rejected
  wake_velocity_time_scale_unsupported_rejected
  wake_velocity_time_scale_missing_rejected
  wake_velocity_time_scale_multiplayer_rejected
  wake_velocity_work_review_pose_name_rejected
  wake_velocity_work_review_pose_without_work_rejected
  wake_velocity_work_review_pose_unbounded_rejected
  wake_velocity_work_clock_report_missing_arg_rejected
  wake_velocity_work_clock_report_empty_rejected
  wake_velocity_work_clock_report_without_frames_rejected
  PROPERTIES WILL_FAIL TRUE LABELS "quick" TIMEOUT 10)
# The retired river location names its replacement instead of printing the usage.
add_test(NAME boat_river_location_retired COMMAND wake_velocity --location river)

add_executable(validate_dance tests/app/validate_dance.c apps/dance_study/dance.c apps/dance_study/choreography.c)
target_link_libraries(validate_dance PRIVATE proteus_render)
target_compile_options(validate_dance PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME dance_constraints COMMAND validate_dance)
add_test(NAME house_dance_constraints COMMAND validate_dance ${CMAKE_CURRENT_SOURCE_DIR}/assets/dance/house.dance)
add_test(NAME house_groove_constraints COMMAND validate_dance ${CMAKE_CURRENT_SOURCE_DIR}/assets/dance/house-groove.dance)
add_executable(validate_choreography tests/app/validate_choreography.c apps/dance_study/choreography.c
  apps/dance_study/dance.c)
target_link_libraries(validate_choreography PRIVATE proteus_render)
target_compile_options(validate_choreography PRIVATE
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
add_test(NAME choreography_language COMMAND validate_choreography
  ${CMAKE_CURRENT_BINARY_DIR}/choreography-validation.dance)
add_test(NAME dance_screenshot
  COMMAND dance_study --time 8.8 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/dance-screenshot.png
)
