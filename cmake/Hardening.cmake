# H2: exploit-mitigation flags for native Linux builds (docs/SERVER_HARDENING_PLAN.md).
#
# Included before any dependency is fetched, so ENet, libjuice, libdatachannel, Lua and raylib
# inherit the compile and link options. Flags the compiler rejects are skipped, so older or
# unusual toolchains still configure. None of them changes floating-point results.
option(PROTEUS_HARDEN "Build Linux binaries with exploit-mitigation flags" ON)

if(NOT (PROTEUS_HARDEN AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN
        AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()

include(CheckCCompilerFlag)
foreach(flag -fstack-protector-strong -fstack-clash-protection -fcf-protection
        -ftrivial-auto-var-init=zero)
  string(MAKE_C_IDENTIFIER "PROTEUS_HAS_${flag}" has_flag)
  check_c_compiler_flag(${flag} ${has_flag})
  if(${has_flag})
    add_compile_options(${flag})
  endif()
endforeach()

# Checked libc calls need optimization, so Debug builds keep the unchecked headers. Level 3
# needs glibc 2.34 and GCC 12 or Clang 9; older glibc treats it as level 2.
add_compile_options($<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
                    $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)

# Position-independent executables (so ASLR moves the code), full RELRO and a
# non-executable stack.
if(POLICY CMP0083)
  cmake_policy(SET CMP0083 NEW)
endif()
include(CheckPIESupported)
check_pie_supported(LANGUAGES C)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_link_options(-Wl,-z,relro,-z,now,-z,noexecstack)
set(PROTEUS_HARDENED TRUE)
