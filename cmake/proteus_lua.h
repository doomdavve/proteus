/* Forced into every Lua source (CMakeLists.txt, proteus_lua). A fixed string-hash seed keeps
 * table traversal, and so plugin diagnostics, identical from run to run. */
#define luai_makeseed(L) 0u
