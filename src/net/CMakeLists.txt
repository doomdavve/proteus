# Shared multiplayer codec, authority/client sessions, prediction and memory/multiplexed
# transports obey the core boundary. Native ENet, optional native WebRTC and the browser
# bridge own platform networking separately.
set(PROTEUS_BOAT_NET_SOURCES
  boat_net_mux.c boat_net_mux.h
  boat_net_client.c boat_net_client.h boat_net_memory.c boat_net_protocol.c
  boat_net_protocol.h boat_net_server.c boat_net_server.h boat_net_transport.h
  boat_net_private.c boat_net_private.h boat_local.c boat_local.h
  boat_prediction.c boat_prediction.h boat_remote_view.c boat_remote_view.h)
add_library(proteus_boat_net STATIC ${PROTEUS_BOAT_NET_SOURCES})
target_link_libraries(proteus_boat_net PUBLIC proteus_boat_core)
target_compile_options(proteus_boat_net PRIVATE ${PROTEUS_WARNINGS})

if(NOT EMSCRIPTEN)
  # ENet v1.3.18 (MIT, LICENSE in its source tree), pinned to the tag's commit. Its sources are
  # compiled here as a static library with its own configure checks and no project warnings.
  FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG 2662c0de09e36f2a2030ccc2c528a3e4c9e8138a # v1.3.18
    SOURCE_SUBDIR no-cmake-build # Fetch only; the target is defined below.
  )
  FetchContent_MakeAvailable(enet)
  add_library(proteus_enet STATIC
    ${enet_SOURCE_DIR}/callbacks.c ${enet_SOURCE_DIR}/compress.c ${enet_SOURCE_DIR}/host.c
    ${enet_SOURCE_DIR}/list.c ${enet_SOURCE_DIR}/packet.c ${enet_SOURCE_DIR}/peer.c
    ${enet_SOURCE_DIR}/protocol.c ${enet_SOURCE_DIR}/unix.c ${enet_SOURCE_DIR}/win32.c)
  target_include_directories(proteus_enet SYSTEM PUBLIC ${enet_SOURCE_DIR}/include)
  set_target_properties(proteus_enet PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)
  if(PROTEUS_SANITIZED)
    # ENet 1.3.18 shifts 1 into the sign bit when it marks an unsequenced group (protocol.c).
    # It has no memory effect, and aborting on it would stop every sanitized network test.
    target_compile_options(proteus_enet PRIVATE -fno-sanitize=shift-base)
  endif()
  if(WIN32)
    target_link_libraries(proteus_enet PUBLIC ws2_32 winmm)
  else()
    include(CheckFunctionExists)
    include(CheckStructHasMember)
    include(CheckTypeSize)
    foreach(function fcntl poll getaddrinfo getnameinfo gethostbyname_r gethostbyaddr_r
        inet_pton inet_ntop)
      string(TOUPPER "HAS_${function}" variable)
      check_function_exists(${function} ${variable})
      if(${variable})
        target_compile_definitions(proteus_enet PRIVATE ${variable}=1)
      endif()
    endforeach()
    check_struct_has_member("struct msghdr" "msg_flags" "sys/types.h;sys/socket.h"
      HAS_MSGHDR_FLAGS)
    set(CMAKE_EXTRA_INCLUDE_FILES "sys/types.h" "sys/socket.h")
    check_type_size("socklen_t" HAS_SOCKLEN_T BUILTIN_TYPES_ONLY)
    unset(CMAKE_EXTRA_INCLUDE_FILES)
    foreach(variable HAS_MSGHDR_FLAGS HAS_SOCKLEN_T)
      if(${variable})
        target_compile_definitions(proteus_enet PRIVATE ${variable}=1)
      endif()
    endforeach()
  endif()
  target_compile_options(proteus_enet PRIVATE $<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-w>)

  add_library(proteus_boat_net_enet STATIC boat_net_enet.c boat_net_enet.h)
  target_link_libraries(proteus_boat_net_enet PUBLIC proteus_boat_net PRIVATE proteus_enet)
  target_compile_options(proteus_boat_net_enet PRIVATE ${PROTEUS_WARNINGS})
endif()
