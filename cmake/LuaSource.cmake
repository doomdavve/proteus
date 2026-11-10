# Keep the pin and archive location together: CI hashes this file for its download cache key.
# Cache only the original archive; URL_HASH verifies it again before extraction.
FetchContent_Declare(
  lua
  URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
  URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
  DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads/lua"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
