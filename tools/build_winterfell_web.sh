#!/usr/bin/env bash
# Build the directory UI against the live room's released core, without changing its physics.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"
release_commit=2b55994a326350f8addc0431c36e0bb086c94b6b
source_dir="$repo_root/build/winterfell-web-source"
build_dir="$repo_root/build/winterfell-web"
mkdir -p "$source_dir"
git archive "$release_commit" | tar -x -C "$source_dir"
for path in tools/boat_title.c web/index.html web/runtime.js web/server_directory.js \
    web/servers.json cmake/Web.cmake tools/package_web.py; do
    cp "$repo_root/$path" "$source_dir/$path"
done
raylib_source="$build_dir/_deps/raylib-src"
raylib_options=()
if [[ -f "$repo_root/build/_deps/raylib-src/CMakeLists.txt" ]]; then
    raylib_source="$repo_root/build/_deps/raylib-src"
    raylib_options+=("-DFETCHCONTENT_SOURCE_DIR_RAYLIB=$raylib_source")
fi
emcmake cmake -S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=MinSizeRel \
    "${raylib_options[@]}"
cmake --build "$build_dir" --target web --parallel 4
python3 "$source_dir/tools/package_web.py" --build-dir "$build_dir" \
    --raylib-license "$raylib_source/LICENSE" \
    --version v0.0.17-server-directory
printf 'Browser UI with released protocol/physics: %s/site\n' "$build_dir"
