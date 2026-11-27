#!/bin/sh
# Leak run: the display-free CTest suite under AddressSanitizer with LeakSanitizer on (Linux
# only; macOS ASan has no leak detection). No window or GPU is needed because the GPU tests are
# not registered unless -DPROTEUS_GPU_TESTS=ON. See docs/TESTING.md#leak-detection.
#
#   tools/lsan_run.sh [build-dir] [extra ctest arguments...]
#
# Writes <build-dir>/lsan.log (every test's output) and prints leaks grouped by allocation site.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-build-lsan}
[ $# -gt 0 ] && shift
if [ "$(uname -s)" != Linux ]; then
    echo "lsan_run: LeakSanitizer needs Linux (use 'leaks --atExit' on macOS)." >&2
    exit 2
fi

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_SANITIZE=address,undefined \
    -DPROTEUS_GPU_TESTS=OFF -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=ON
cmake --build "$build" -j"$(nproc)"

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:exitcode=23"
export LSAN_OPTIONS="suppressions=$root/tests/lint/lsan.supp:print_suppressions=0:max_leaks=0"
export UBSAN_OPTIONS="print_stacktrace=1"

status=0
ctest --test-dir "$build" -j"$(nproc)" --timeout 600 --output-on-failure "$@" \
    >"$build/lsan.log" 2>&1 || status=$?
tail -n 12 "$build/lsan.log"

echo
echo "== leak reports by allocation site (count, bytes, first project frame)"
python3 - "$build/lsan.log" <<'PY'
import collections, re, sys
text = open(sys.argv[1], errors="replace").read()
sites = collections.Counter()
sizes = collections.Counter()
for block in re.split(r"\n(?=(?:Direct|Indirect) leak of )", text):
    head = re.match(r"(?:Direct|Indirect) leak of (\d+) byte", block)
    if not head:
        continue
    frame = next((m.group(1) for m in re.finditer(r"#\d+ \S+ in (\S+ \S*(?:/src/|/apps/|/tools/)\S+)", block)),
                 "(no project frame)")
    sites[frame] += 1
    sizes[frame] += int(head.group(1))
for frame, count in sites.most_common(40):
    print(f"{count:5d} {sizes[frame]:9d}  {frame}")
if not sites:
    print("none")
PY
exit "$status"
