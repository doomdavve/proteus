# Optional native endpoint for the browser transport. Normal native builds stay ENet-only.
enable_language(CXX)
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(datachannel
  GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
  GIT_TAG a02b751917ac8afc8c58dc6f4461d25ff9465d48 # v0.24.1, MPL-2.0
  GIT_SHALLOW FALSE
  GIT_SUBMODULES deps/plog deps/libjuice deps/usrsctp
  GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(datachannel)
add_library(proteus_boat_net_rtc STATIC src/boat_net_rtc.c)
target_link_libraries(proteus_boat_net_rtc PUBLIC proteus_boat_net PRIVATE datachannel-static)
target_compile_options(proteus_boat_net_rtc PRIVATE ${PROTEUS_WARNINGS})
add_executable(boat_rtc_echo tools/boat_rtc_echo.c)
target_link_libraries(boat_rtc_echo PRIVATE proteus_boat_net_rtc proteus_boat_net_enet)

# Both the dedicated authority and the game's --serve path can enable browser clients.
foreach(target wake_velocity boat_server)
  target_link_libraries(${target} PRIVATE proteus_boat_net_rtc)
  target_compile_definitions(${target} PRIVATE PROTEUS_WEBRTC=1)
endforeach()
