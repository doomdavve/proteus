# tests/web: registered from tests/tests.cmake in the project root's directory scope.

add_test(NAME web_plugin_wire COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/check_web_wire.py)
set_tests_properties(web_plugin_wire PROPERTIES LABELS "cpu;quick"
  PASS_REGULAR_EXPRESSION "web_plugin_wire=ok")
if(PROTEUS_NODE_EXECUTABLE)
  add_test(NAME web_plugin_author COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/test_web_plugin_author.cjs)
  set_tests_properties(web_plugin_author PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_plugin_author=ok")
  add_test(NAME web_server_directory COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/test_web_server_directory.cjs)
  set_tests_properties(web_server_directory PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_server_directory=ok")
  add_test(NAME web_rtc_transport COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/test_web_rtc_transport.cjs)
  set_tests_properties(web_rtc_transport PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_rtc_transport=ok")
  add_test(NAME web_mobile_controls COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/test_web_mobile_controls.cjs)
  set_tests_properties(web_mobile_controls PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_mobile_controls=ok")
  add_test(NAME web_console_controls COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/test_web_console_controls.cjs)
  set_tests_properties(web_console_controls PROPERTIES LABELS "cpu;quick"
    PASS_REGULAR_EXPRESSION "web_console_controls=ok")
  add_test(NAME web_mouse_controls COMMAND ${PROTEUS_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/test_web_mouse_controls.cjs)
  set_tests_properties(web_mouse_controls PROPERTIES LABELS "cpu"
    PASS_REGULAR_EXPRESSION "web_mouse_controls=ok")
endif()
