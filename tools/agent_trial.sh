#!/bin/sh
# Set up (and optionally start) the external-agent authoring trial in docs/AGENT_AUTHORING.md.
#   tools/agent_trial.sh setup [DRAFT]   copy plugins/langedrag to DRAFT (watched by the game) and
#                                        DRAFT-work (the agent edits and stages this one)
#   tools/agent_trial.sh play  [DRAFT]   start the game on DRAFT with the live bridge
# Run from the repository root with a built ./build/wake_velocity. The bridge directory is private
# to the user and its token must not be pasted into prompts.
set -eu

cmd=${1:-}
draft=${2:-$HOME/wake-agent-trial/langedrag}
bridge=${WAKE_BRIDGE:-$HOME/.wake-velocity-bridge}
author="python3 tools/plugin_author.py"

case "$cmd" in
setup)
    [ -x build/wake_velocity ] || { echo "build first: cmake --build build -j" >&2; exit 1; }
    [ ! -e "$draft" ] || { echo "$draft exists; remove it or pass another DRAFT" >&2; exit 1; }
    mkdir -p "$(dirname "$draft")"
    [ ! -e "$draft-work" ] || { echo "$draft-work exists" >&2; exit 1; }
    cp -R plugins/langedrag "$draft"
    cp -R plugins/langedrag "$draft-work"
    $author validate "$draft"
    $author preview "$draft" --ticks 360 >/dev/null
    $author capture "$draft" --anchor outer.dock --output "$draft/../baseline.png"
    echo "draft (game watches, do not edit): $draft"
    echo "work copy (agent edits and stages):    $draft-work"
    echo "bridge: $bridge"
    echo "baseline frame: $(dirname "$draft")/baseline.png"
    echo "next: tools/agent_trial.sh play $draft   (then start the agent in this directory)"
    ;;
play)
    [ -d "$draft" ] || { echo "run setup first" >&2; exit 1; }
    exec ./build/wake_velocity --plugin "$draft" --plugin-bridge "$bridge"
    ;;
*)
    echo "usage: $0 setup|play [DRAFT]" >&2
    exit 2
    ;;
esac
