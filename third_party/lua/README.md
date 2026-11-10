# Lua source archive

`lua-5.4.9.tar.gz` is the unmodified upstream source release from
<https://www.lua.org/ftp/lua-5.4.9.tar.gz> (373,429 bytes).

SHA-256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`.

The original MIT notice from `src/lua.h` is reproduced in [LICENSE](LICENSE).
CMake extracts the archive into the build directory after checking the pinned hash in
`cmake/LuaSource.cmake`. Lua requires no download or CI cache, including in a fresh build.

To upgrade, replace the original archive, update its filename/hash in
`cmake/LuaSource.cmake` and this README, and refresh LICENSE from upstream. Review host
compatibility and update the interpreter contract in `docs/PLUGIN_API.md` in the same change.
Do not edit or regenerate the archive contents.
