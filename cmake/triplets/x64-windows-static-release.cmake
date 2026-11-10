# Routine CI only builds Release. Avoid building unused Debug curl/zlib packages.
# Release/manual packaging keeps the standard x64-windows-static triplet.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
