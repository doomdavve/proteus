# The unmodified upstream archive is shipped with the checkout so cold builds need no
# Lua download. Keep verifying the same published hash before extraction.
FetchContent_Declare(
  lua
  URL "${CMAKE_CURRENT_LIST_DIR}/../third_party/lua/lua-5.4.9.tar.gz"
  URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
