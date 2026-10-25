#!/usr/bin/env bash
# Run from the repository root; requires Weston, Mesa/EGL and GNU timeout on Linux.
set -euo pipefail

build_dir=${1:-build-linux}
export XDG_RUNTIME_DIR
XDG_RUNTIME_DIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"
export WAYLAND_DISPLAY=proteus-smoke
export XDG_SESSION_TYPE=wayland
export LIBGL_ALWAYS_SOFTWARE=1
unset DISPLAY
weston_pid=

cleanup() {
    if [[ -n "$weston_pid" ]]; then
        kill "$weston_pid" 2>/dev/null || true
        wait "$weston_pid" 2>/dev/null || true
    fi
    rm -rf "$XDG_RUNTIME_DIR"
}
trap cleanup EXIT

weston --backend=headless-backend.so --use-gl --width=960 --height=600 \
    --idle-time=0 --no-config --socket="$WAYLAND_DISPLAY" \
    >"$build_dir/weston-smoke.log" 2>&1 &
weston_pid=$!

for ((attempt = 0; attempt < 100; attempt++)); do
    if [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]]; then
        break
    fi
    if ! kill -0 "$weston_pid" 2>/dev/null; then
        cat "$build_dir/weston-smoke.log"
        exit 1
    fi
    sleep 0.1
done
if [[ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]]; then
    cat "$build_dir/weston-smoke.log"
    echo "Weston did not create its Wayland socket" >&2
    exit 1
fi

timeout 60s "$build_dir/wake_velocity" --verbose --location basin --mouse-steering --frames 120 \
    2>&1 | tee "$build_dir/wayland-smoke.log"
grep -F 'PLATFORM: DESKTOP (GLFW - Wayland): Initialized successfully' \
    "$build_dir/wayland-smoke.log"
