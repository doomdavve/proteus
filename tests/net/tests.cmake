# tests/net: registered from tests/tests.cmake in the project root's directory scope.

add_executable(validate_boat_net_mux tests/net/validate_boat_net_mux.c)
target_link_libraries(validate_boat_net_mux PRIVATE proteus_boat_net)
target_compile_options(validate_boat_net_mux PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_net_mux COMMAND validate_boat_net_mux)
set_tests_properties(boat_net_mux PROPERTIES LABELS "core;cpu" PASS_REGULAR_EXPRESSION "boat_net_mux=ok")

add_executable(validate_boat_net_sdp tests/net/validate_boat_net_sdp.c)
target_link_libraries(validate_boat_net_sdp PRIVATE proteus_boat_net)
target_compile_options(validate_boat_net_sdp PRIVATE ${PROTEUS_WARNINGS})
add_test(NAME boat_net_sdp COMMAND validate_boat_net_sdp)
set_tests_properties(boat_net_sdp PROPERTIES LABELS "core;cpu;quick" PASS_REGULAR_EXPRESSION "boat_net_sdp=ok")

add_executable(validate_boat_net tests/net/validate_boat_net.c)
target_link_libraries(validate_boat_net PRIVATE proteus_boat_net)
# The same room harness over real ENet sockets on 127.0.0.1, still on a virtual clock. Cases
# that need the memory network's impairment are left out, hence -Wno-unused-function.
add_executable(validate_boat_net_enet tests/net/validate_boat_net.c)
target_link_libraries(validate_boat_net_enet PRIVATE proteus_boat_net_enet)
target_compile_definitions(validate_boat_net_enet PRIVATE BOAT_NET_TEST_ENET)
target_compile_options(validate_boat_net_enet PRIVATE ${PROTEUS_WARNINGS}
  $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wno-unused-function>)
proteus_add_case_tests(boat_net validate_boat_net LABELS core cpu)
proteus_add_case_tests(boat_net_enet validate_boat_net_enet LABELS cpu)
