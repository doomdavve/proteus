# Marine boat instruments

Status, 2026-10-24: implemented in branch `boat-marine-hud`, then cherry-picked onto `main` after
`29d1afb` (title screen, M4 reset feedback, packaged `boat_server`). In the merge the HUD draws into
the 320x200 frame and the reset fade, notices and room roster stay in screen space. (At the
merge Esc released the mouse and then left; since 2026-10-25 it opens the in-game menu instead.) Native macOS checks after the merge: Debug
build and all 105 active CTests; the merged controls were not hand-played. The bottom-anchored translucent instrument panel now accompanies
stepped keyboard/scroll thrust and captured, stateful mouse steering. The initial absolute
pointer prototype is superseded following user feedback. The user accepted captured/stateful
steering with scroll thrust on 2026-10-24. Native macOS and browser qualification is recorded
below; native Windows/Linux, shipping Safari and multiplayer integration remain pending.
The [boating plan](BOATING_PLAN.md) owns gameplay direction.

Completed task: a bottom-centred marine case with pale markings, muted metal trim,
flush screws and amber/sea-green needles. All three readings are visual ranges, with
no numeric readouts or units:

- **Thrust:** a vertical telegraph. Forward is above neutral; reverse is below.
  The handle shows `Boat.throttle`, not delayed engine response. Thrust still holds when
  released. Tap Up/W or Down/S once per step; a held key steps again every 0.5 s, and each new
  press steps at once, so quick taps are faster than holding. Scroll down/up adds/removes
  thrust (down opens the throttle since 2026-10-24), in either steering mode: one scroll unit per step, with fractional trackpad
  motion accumulated and larger scrolls allowed to cross several steps. No scrolling holds
  the selected thrust. Overscrolling at either limit does not delay the next reverse scroll.
  Forward steps are
  ultra-slow (5%), slow (20%), medium (50%) and max (100%). Reverse steps are ultra-slow
  (5%) and slow (20%), with neutral between the directions. Seven marks give each setting
  distinct visual travel, including docking. Command travel and engine spool remain smooth;
  the handle tracks actual command travel. R selects neutral. Solo focus/help pauses retain
  the selection; multiplayer focus/help and new lives clear it to neutral.
- **Speed:** a needle shows planar ground-speed magnitude, including lateral/reverse motion.
  Its internal display range remains 0..60 knots, saturating above that range, without
  displaying units or numbers. This is not signed forward speed or speed through water.
- **Steering:** the spring-centred wheel setting, with port/starboard endpoints and a needle.
  This shows `Boat.wheel`, not heading, turn rate or engine deflection. Press M to toggle
  keyboard/mouse steering, or use `--mouse-steering`; the title's SETTINGS > STEERING row
  remembers which mode sessions start in. Mouse mode captures/hides the cursor
  and accumulates horizontal movement into a held wheel target (300 mouse pixels from centre
  to lock, independent of window size/frame time). Stopping movement holds the target;
  opposite movement unwinds it. Clamp the stored target at lock so excess movement does not
  cause windup. The existing turn-in/countersteering limits still apply. M switches
  mouse/keyboard steering. Esc opens the in-game menu (owned by the
  [title plan](TITLE_SCREEN_PLAN.md#in-game-escape-menu-2026-10-25)) and releases capture
  without changing the mode; its STEERING row also switches it. Resuming recaptures.
  Help/focus loss releases capture; returning resumes it with capture-frame motion discarded.
  Solo pause retains the target; multiplayer inactivity neutralizes it. R/new life resets
  the target to centre. Keyboard mode retains spring return. Demo/replay release capture and
  retain their existing controls. The scene-pixel label shows `MOUSE / ESC: MENU`
  (hidden while the menu is open). A browser unlock the game did not request (Escape,
  focus loss) opens the menu instead of switching to keyboard; denied capture still
  falls back to keyboard with `MOUSE UNAVAILABLE / USE A/D`.
  In object-wake flight the same wheel also commands roll, progressively up to a barrel
  roll at full steering. Centre it to brake and assist an upright landing; stopping the
  mouse holds the roll command. The [stability study](ARCADE_STABILITY.md) owns the first tuning.


The 166x44 panel has one layout at every window size and sits flush against the scene's
bottom edge (including its shallow shadow). The case is slightly translucent; markings
and needles remain opaque. A three-by-five bitmap font, rasterized circles and integer
line drawing keep every stroke on the scene's pixel grid.

The scene and HUD are composited into one 320x200 render texture **before** presentation,
then scaled together with nearest filtering and the same integer-scaled destination.
There is no independent HUD scale or window-resolution text. Blending onto the opaque
scene also avoids double-multiplying translucent colors. Letterboxing follows the scene.
Existing course/help/profiler/status overlays retain their previous screen-resolution
rendering; this change owns the three-instrument HUD. Wake/connection status moves above
the case so it does not cover the instruments. Help covers the HUD.

## Integration handoff

`src/boat_hud.[ch]` is presentation-only and linked in `proteus_render`. The draw function
allocates no resources and retains no state. It needs raylib and three telemetry floats.
The thrust helper (`src/boat_throttle.h`) shares detents and their visual positions. Core
physics and wire layouts retain their signed rate semantics. The multiplayer client adds an
optional target service, generating ordinary rates from its predicted boat each tick; callers
using the original rate service retain their behavior.

`tools/wake_velocity.c` owns a 320x200 `frame_target`, including allocation validation,
nearest filtering and cleanup. Each frame, it draws the scene texture into this target
with the existing CPU/GPU orientation normalization, then calls `boat_hud_draw` with the
displayed local player's velocity magnitude, throttle and wheel. It presents the combined
texture once to the existing integer-scaled scene destination. Preserve these orientation
and lifetime details when resolving integration conflicts. Do not use remote-boat or raw
keyboard state. Scene-only CLI screenshots retain their existing behavior.

`CMakeLists.txt` adds the module to `proteus_render` and a native-only `boat_hud_review`
executable. README/index/plan edits describe the current presentation.

Next task: qualify Windows/Linux and shipping Safari, then integrate the accepted HUD and
controls with multiplayer.
Inspect a connected session after integration, especially own spawn/reset and new UI.
Preserve the main agent's uncommitted work; this branch does not include those changes.
The current control behavior holds the selected thrust until another tap or reset.

## Stepped-thrust verification, 2026-10-24

Completed task: four forward and two reverse keyboard settings, neutral, matching HUD marks,
and target-to-rate conversion for solo and predicted multiplayer ticks. Automated coverage
exercises all 49 integrator transitions and forward/reverse settling over the virtual network.
The scripted `--demo` and existing replays retain their authored continuous inputs.

Verification, macOS / Apple M1 Max: Debug configure/build passed without project warnings.
All 102 active CTests passed (15 retired cases disabled) with GPU tests enabled in 81.30 s;
this includes the new transition/network checks, core/net boundaries and ENet loopback.
Log: `/private/tmp/proteus-throttle-ctest-final.log`. Forward and reverse docking captures
were inspected at 3x. A windowed GPU+AA river demo completed for 120 frames. Formatting,
diff whitespace and changed documentation file links passed. The initial sandboxed run could
not access sockets/graphics; the final suite ran with that access.

Manual keyboard play, connected native sessions and user acceptance remain pending; the
5%/20% docking power needs user review. Next: play-test the steps, then integrate this branch.

## Held thrust, scroll direction and steering setting, 2026-10-24

Completed task (user request): holding Up/W or Down/S repeats one thrust detent every 0.5 s
(`boat_throttle_hold` in `src/boat_throttle.h`), with each new press stepping at once and
restarting the wait. Scroll direction is flipped in `tools/wake_velocity.c` for native and browser
input alike: scrolling down opens the throttle. The title's new SETTINGS panel (NAME, BOAT,
STEERING, BACK) holds a persistent `mouse_steering=on|off` in `title.txt`; title sessions pass
`--mouse-steering` when it is on. NAME and BOAT moved from the main menu into that panel.

Verification, macOS, Debug: warning-free build and all 110 active CTests passed; `boat_handling`
checks press, hold, re-press, release and limits of the repeat. Title captures of the main menu
and (through a temporary, reverted capture tweak) the settings panel were inspected. Not yet
hand-played: held-key feel, physical scroll direction, the settings row with keyboard/mouse, and
mouse steering at session start from the title. User acceptance pending.

## Superseded absolute mouse prototype, 2026-10-24

Completed task: M mode toggle, `--mouse-steering`, horizontal target/dead zone, cursor/focus
handling, scene-pixel mode label and per-tick steering target conversion for solo and predicted
multiplayer. The core steering rules and wire input format retain their behavior. The shared
unwind constant is now in `src/boat_steering.h` beside the pure mouse mapping/controller.

Automated coverage adds all 49 wheel transitions in both float and wire-quantized form, checks
settling and sustained holding within 0.0002 wheel units, and verifies mouse targets on the
authority and predicted client over the virtual network.

Verification on macOS / Apple M1 Max: Debug configure/build passed without project warnings;
all 102 active CTests passed (15 retired cases disabled, GPU checks enabled) in 88.50 s.
Log: `/private/tmp/proteus-mouse-ctest.log`. After the reset-recentring refinement, the final
build and a 600-frame GPU+AA river mouse-mode run passed. An app-only capture of the live
river window was inspected (`build/mouse-steering-window.png`), including mode text and the
centred steering needle. Formatting, diff whitespace and changed local file links passed.
The river view was left running in mouse mode for user review. The user then reported the
visible pointer and absolute mapping felt strange, requesting captured/stateful steering.
That implementation is superseded by the revision below; native connected and browser
checks were not performed for this historical pass.

## Captured mouse and scroll thrust, 2026-10-24

Completed task: hidden/locked pointer via raylib cursor capture, relative movement integrated
into a held wheel target, M/Esc release, capture release around help/focus/replay and on exit,
reset handling, and scroll-wheel thrust using the existing seven detents. The absolute mapping
and centre dead zone are removed. The controller/prediction/wire rate conversion is retained.

Automated checks cover stationary wheel holding, relative direction, equal total motion split
across 60/144 updates, lock saturation and immediate reversal, invalid movement, fractional
scroll accumulation, multiple steps through neutral, both scroll limits and reversal. The
existing integrator/network target checks remain active.

Verification on macOS / Apple M1 Max: Debug configure/build passed without project warnings;
all 102 active CTests passed in 90.09 s (GPU checks enabled; 15 retired cases disabled).
Log: `/private/tmp/proteus-mouse-relative-ctest.log`. A 120-frame GPU+AA river run in captured
mouse mode completed and exited cleanly. After selecting vertical-only scroll input, the final
build passed and the live river view was opened for review. Its app-only capture was inspected
(`build/mouse-relative-window.png`), including the release hint and instruments. Formatting,
diff whitespace and changed documentation file links passed.

At this stage, actual hardware movement/scroll feel, help/focus/Esc interaction, native
connected play and browser pointer lock were unverified by the agent. User acceptance, 2026-10-24: after
trying this revision, the user said, “This is the way! Really nice.” Captured/stateful mouse
steering and scroll thrust are the accepted control scheme. This does not establish exhaustive
help/focus/Esc or platform coverage. The following qualification adds browser evidence.

## Platform qualification, 2026-10-24

Completed task: add a browser-specific capture adapter with gesture-based requests, actual
asynchronous lock confirmation, external Escape/focus loss handling, a click-to-recapture
prompt and visible keyboard fallback. Browser movement and scroll events accumulate through
the C frame; scroll modes normalize without losing fractional trackpad input. A regression
check covers Escape before acquisition confirmation. The native raylib/GLFW path is unchanged.
See [browser details and repeatable checks](WEB_BUILD.md#mouse-and-scroll-qualification-2026-10-24).

Verification on Apple M1 Max / macOS:

- Emscripten 6.0.12 MinSizeRel build passed without application warnings. Node event-contract
  tests passed; CTest includes `web_mouse_controls` when Node is installed.
- Chrome (headless/SwiftShader) and Firefox (headed): actual pointer-lock acquisition,
  accumulated movement consumed by C, held steering capture, fractional/line scroll, help
  release/click reacquisition, external unlock without a key event, Escape and denied-capture
  fallback passed. Deterministic DOM movement/scroll events test delivery; physical device
  feel is not inferred. No JavaScript errors. Captures were inspected.
- WebKit (headed): this test engine refused/lacked pointer capture. Keyboard/scroll fallback
  and the visible `MOUSE UNAVAILABLE / USE A/D` message passed; no JavaScript errors.
  This is **not** a captured-mouse pass or a shipping Safari check.
- Native Debug configure/build passed without project warnings; all 103 active CTests passed
  in 86.97 s (GPU enabled, Node available; 15 retired cases disabled). Log:
  `/private/tmp/proteus-controls-platform-ctest-final.log`. A final 120-frame macOS GPU+AA
  river run in mouse mode completed and exited cleanly. Formatting, changed local links,
  Python/JS syntax, workflow YAML syntax and diff whitespace checks passed.

Browser artifacts: `build-web/controls-{chromium,firefox,webkit}/results.json` and PNGs;
logs: `/private/tmp/proteus-controls-*.log`. Initial browser timing failures exposed and led
to fixes for short M gestures, capture confirmation and fast cancellation before confirmation.
Headless OS cursor moves did not provide dependable deltas; the repeatable test uses DOM events.

The Windows workflow already runs the extended `boat_handling` checks. New
[Linux CI](LINUX_BUILD.md) configures Ubuntu/X11, runs the CPU suite and opens/exits a captured
mouse window under Xvfb. Its YAML was checked; neither native workflow has run for this
uncommitted worktree. Windows/Linux toolchains and physical devices were unavailable here;
Wayland, shipping Safari, native connected controls and cross-device sensitivity remain
unverified. Browser Pages CI now runs the Node contract before its Wasm build. No workflow
was dispatched, no branch was pushed and no deployment was performed. Next: those platform
runs and human checks, then multiplayer integration.

2026-10-24 Wayland update: Linux builds now enable native Wayland by default alongside X11.
CI is configured to assert Wayland startup/exit under headless Weston and X11 under Xvfb;
the updated workflow has not yet run. Physical steering/capture, scroll, focus/Escape release
and display scaling on GNOME/KDE remain pending. See the [Linux owner](LINUX_BUILD.md) for
current commands and verification limits; the preceding platform-check record is historical.

## Review commands

From the worktree root, build normally, then run the boating view:

```sh
./build/wake_velocity --location river --gpu-aa
./build/wake_velocity --location river --gpu-aa --mouse-steering
./build/wake_velocity --location river --gpu-aa --demo --frames 180
```

`wake_velocity --screenshot` remains scene-only. The review tool composites deterministic
telemetry onto a background at 320x200, then nearest-scales the combined frame:

```sh
./build/wake_velocity --renderer cpu --location river --time 3 --screenshot build/hud-river-background.png
./build/boat_hud_review --background build/hud-river-background.png --case cruise --screenshot build/hud-cruise.png
./build/boat_hud_review --background build/hud-river-background.png --case neutral --compact --screenshot build/hud-neutral-compact.png
./build/boat_hud_review --background build/hud-river-background.png --case reverse --screenshot build/hud-reverse.png
./build/boat_hud_review --case docking --screenshot build/hud-docking.png
./build/boat_hud_review --case reverse-docking --screenshot build/hud-reverse-docking.png
./build/boat_hud_review --background build/hud-river-background.png --case full --large --screenshot build/hud-full-large.png
```

The review tool requires a graphics context. Default output is 960x600 (3x), compact
320x200 (1x), large 1280x800 (4x); all use the same layout and pixel grid. Omit
`--screenshot` and add `--frames 600` for a visible static review. Fixture telemetry
is illustrative, not sampled from the background scene.

## Verification and limitations

Historical first pass, 2026-10-24, macOS / Apple M1 Max: Debug build and 102 active
CTests (21 active GPU) passed; 15 retired wake-field cases stayed disabled. Four
captures were inspected and a scripted GPU river run completed. The user rejected
that pass's mixed pixel sizes and requested the visual-only revision above.

Historical bottom/transparency pass, 2026-10-24, macOS / Apple M1 Max: Debug build completed
without project warnings and all 102 active CTests passed (21 active GPU, 15 retired cases disabled),
including the final bottom/transparency revision. Full output:
`/private/tmp/proteus-hud-ctest-bottom.log`. Inspected neutral, cruise, reverse and
full-range captures at 1x/3x/4x. A pixel-by-pixel check using the existing PNG reader
verified exact uniform blocks throughout every capture, including the translucent case.
Scripted GPU+AA river (`--demo --frames 180`) and CPU basin (`--demo --frames 60`)
windowed runs completed successfully. Formatting, diff whitespace and local file links
passed. This is agent-run verification; no manual keyboard play is claimed.
Dependencies are read from the main checkout's pinned source caches; all output stays
in this worktree. Initial raylib compilation had its existing `stb_vorbis.c` warning.

Windows, Linux, browser compilation, manual keyboard play, interactive replay and connected
multiplayer HUD checks remain unverified. Pre-existing course/replay overlays can occupy
substantial screen space at the minimum window size; they are not redesigned here.
Artifacts are in ignored `build/` and `/private/tmp/proteus-hud-*.log`.
