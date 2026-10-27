# Pedal controls integration handoff

Status, 2026-10-26: implementation and automated verification complete on the isolated
`controls-pedals-stability` branch, based on **2b55994**. Worktree:
`/private/tmp/proteus-controls-pedals-stability`. No merge, push or deployment is part of this
change. The final commit is reported in the agent handoff; use this branch's tip for integration.
The [HUD/control owner](BOAT_HUD.md) describes current behavior; the
[stability owner](ARCADE_STABILITY.md) distinguishes it from the superseded roll study.

## Completed behavior

- Hold W/Up for full forward or S/Down for 20% reverse, including reverse-thrust braking.
  Neither/both pedals select neutral; Shift selects 5% in either direction. Scrolling is
  unassigned. Command travel is 4 units/s; engine spool remains separately smoothed.
- Keyboard steering still springs back, mouse steering still holds its target, and air yaw
  remains. The wheel no longer commands an aerial roll or suppresses upright recovery.
  Existing wet assistance, inversion fade and physical capsize rules remain.
- R is inactive; Space operates the deathmatch water cannon; E drops a stern wave grenade; Q launches a torpedo; V toggles the rearview mirror (hidden by default). Free/race play leaves Space unused. Reset is explicit in the menu or
  requires a one-second Backspace hold with progress. Release/help/menu/focus loss/replay
  cancels progress; a fired hold stays latched until release. Replay keeps Space pause,
  with Escape > RESTART RUN to leave playback. Room reset restrictions remain authoritative.
- The interactive race trial shares the pedal and hold helpers. HUD precision/reset indicators
  retain the scene pixel grid. Normal game instruments show actual command travel, not targets.
- Physics compatibility is **9**; that controls change preserved protocol v5 bytes; the current deathmatch arsenal uses v7. Old best files stay on
  disk; revision-9 bests use new names and old peers/recordings are rejected normally.

## Verification evidence

2026-10-26, Apple Silicon M1 Max / macOS, Debug, default retired wake tests disabled and
GPU CTests off: build warning-clean and **94 active CTests passed** in 74.71 s. Numerical
coverage includes pedal combinations, 0.25 s response, precision/reversal/release, coast versus
reverse braking, reset cancellation/latching, automatic recovery under a held turn, yaw,
inverted capsizing and bit-identical checkpoint replay. Dedicated multiplayer target checks
settle within one quantized command increment. The final required build/test rerun after help/notice updates also passed all 94 cases.
The final build/test result is retained with
local artifacts under `build/verification/`.

Emscripten 6.0.12 MinSizeRel builds. Chrome headless/SwiftShader pedal integration passed using
pixels from the actual thrust instrument: forward, reverse, neutral release, opposing keys,
both precision directions, and ignored scroll. Held mouse steering stayed unchanged around R,
Space and a cancelled short reset. Long hold and menu reset completed, with progress/menu/HUD
captures inspected. Browser multiplayer beside a native bot passed held and menu reset requests
individually (two completions, none dropped), then leave/rejoin; the authority reported zero
malformed, output and emission faults. The existing browser mouse integration also passed capture, movement accumulation, normalized
scroll draining, help/unlock, Escape/resume recapture and denied-capture fallback.
The reset test waits for actual outgoing requests rather than
assuming a fixed wall-time wait represents enough rendered frames under software rendering.

Native GPU HUD precision/progress captures and CPU basin/river frames were inspected. Native
windowed river/waves demos (including `--waves --demo --frames 600`) and interactive race-trial smoke (`--play --frames
120`) completed. These are agent-run checks, not human handling or listening acceptance.
The first sandboxed suite could not bind its existing local sockets; reruns with socket access
passed. Superseded failures were corrected rather than excluded: shared target-rate constants,
quantized-settling bounds, and deliberate capsize replay coverage.

Reproduce the browser checks after serving the worktree build:

```sh
python3 tools/check_web_pedals.py --url http://127.0.0.1:8026/ --output build-web/pedals
python3 tools/check_web_multiplayer.py --url http://127.0.0.1:8026/ --build-dir build-rtc --output build-web/pedals-network
```

The optional WebRTC authority build is described in [browser setup](WEB_BUILD.md#webrtc-rooms).
The automation scripts are source; installed browser packages, build output, logs and captures
remain uncommitted artifacts.

## Integration notes and next task

The integration agent should merge/cherry-pick this branch, preserve concurrent adventure/map
work, then rerun the native build/CTest and browser controls against the combined result.
Likely conflicts are the game input loop/menu, HUD/control docs and index, protocol handling
revision, CMake's core-header list, and scripted driver/validator target-rate expressions.

Keep **BOAT_THROTTLE_COMMAND_RATE** consistent in every physical target-to-rate controller,
including inactive network input and review/reference drivers. Leaving an old `.5f * BOAT_STEP`
denominator causes overshoot with the faster integrator. Bots observing delayed network mirrors
use a quarter-second feedback horizon; player pedals retain the fast predicted controller.
The historical calm-water fixture intentionally scales its authored rate input to retain its
original physical travel, isolating hydrodynamic regressions. The prediction fixture deliberately
seeds an inverted boat: safer normal steering no longer guarantees an incidental capsize there.

The explicit room-menu reset must be allowed in the frame that closes the menu, even though
normal driving input is inactive that frame. Do not restore the old `reset && active` gate;
keep grid/parked/respawn restrictions and neutral inactive controls. The extra reset menu row
also changes Leave navigation in browser integration scripts.

Next: human playtest docking, forward/reverse changes, held mouse turns across wakes, difficult
landings, and short/long reset holds after integration. Windows/Linux compilation, physical
mouse/browser feel, Firefox/Safari, human audio review and public WebRTC deployment are not
qualified by this change. Do not infer acceptance from automated tests or earlier handling
approval. No general capsize immunity, gamepad support or finer keyboard steering is added.
