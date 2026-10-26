#!/usr/bin/env bash
# Build the directory UI against the live room's released core, without changing its physics.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"
release_ref=${1:-v0.0.17}
release_commit=$(git rev-parse --verify "${release_ref}^{commit}")
source_dir=${WAKE_WEB_SOURCE_DIR:-"$repo_root/build/winterfell-web-source"}
build_dir=${WAKE_WEB_BUILD_DIR:-"$repo_root/build/winterfell-web"}
mkdir -p "$source_dir"
git archive "$release_commit" | tar -x -C "$source_dir"
for path in tools/boat_title.c web/index.html web/runtime.js web/server_directory.js \
    web/servers.json cmake/Web.cmake tools/package_web.py; do
    cp "$repo_root/$path" "$source_dir/$path"
done
# v0.0.17 predates the outer location; keep this UI backport on its two-location core.
if [[ "$release_commit" == 2b55994a326350f8addc0431c36e0bb086c94b6b ]]; then
    python3 - "$source_dir/tools/boat_title.c" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
path.write_text(''.join(line for line in path.read_text().splitlines(keepends=True)
                        if 's->location == BOAT_LOCATION_OUTER' not in line))
PY
fi
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
    --version "${release_ref}-server-directory"
python3 - "$source_dir" "$build_dir" "$release_commit" <<'PY'
from pathlib import Path
import json, re, sys
source, output, commit = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
def constant(file, name):
    return int(re.search(r'^#define ' + name + r' (\d+)$', (source / file).read_text(), re.M)[1])
metadata = {'commit': commit,
            'protocol': constant('src/boat_net_protocol.h', 'BOAT_NET_VERSION'),
            'physics': constant('src/boat.h', 'BOAT_PHYSICS_REVISION')}
(output / 'deployment.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY
printf 'Browser UI with released protocol/physics: %s/site\n' "$build_dir"
