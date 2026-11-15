# M4 O4 example evidence — 2026-11-15

Source: branch `feat/marina-m4` at O3 (`a0db73f`) plus the O4 example, classification change and
tests. Platform: Apple Silicon, macOS, native Debug. Local only; nothing pushed.

- `ctest --test-dir build -j 8`: 574/574 passed (localhost sockets available, GPU CTests as
  configured). New: `boat_plugin_session.marina_berth_apply_undo` checks both vessels' owner at
  handoff ticks (599/600, 2999/3000, 239/240, 1679/1680, 1919/1920, 2519/2520), refuses the
  visit shifted to 3 s as a crossing, and exercises held Apply (restart) and Undo.
- `plugin_author.py validate plugins/marina-berth-demo`: ok. The example is also in the worker
  examples test.
- CPU and GPU stills (`--renderer cpu|gpu`, 320×200): [CPU 15 s](cpu-15.png),
  [GPU 15 s](gpu-15.png) (visitor moored, tender on its racetrack), [CPU 13.9 s](cpu-13.9.png)/
  [14.1 s](cpu-14.1.png) around the visitor's mooring, [CPU 24.9 s](cpu-24.9.png)/
  [25.1 s](cpu-25.1.png) and [GPU 24.9 s](gpu-24.9.png) around the tender's return. Inspected by
  the agent: hulls take their authored colours and no hull is doubled or missing at the
  handoffs. A still cannot show a wave pop.
- Windowed `--demo --demo-idle --frames 3300 --no-vsync` ran 5081 ticks to exit 0. The agent did
  not watch it, so motion smoothness and handoff pops are unjudged.
- A finding: a service edit that kept the routes unchanged was classified live; service
  timetables now classify as a session restart like route edits.

Not run: browser/Wasm (no Emscripten here), room end to end, independent-session comparison,
user acceptance.
