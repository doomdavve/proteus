# Gothenburg boating playground

Status: updated 2026-10-25. `wake_velocity` implements free boating in the basin and river
locations ([Eriksberg location removed](#eriksberg-location-removed--2026-10-25)), reverse/docking, planing, buoyancy, optional gentle waves, opt-in
current/scripted-wake options in all locations, default bounded arcade wakes in free boating,
and a timed S course with recordings, persistent personal bests, ghosts, and replay controls. The study
has quiet engine/water audio and [selectable XM music](#selectable-soundtracks--2026-10-24):
the gentle A little boat loop (default), Acid Wake techno, or Off. The
[stern-wake motion pass](#stern-wake-motion--2026-10-18) and physics Phase D are implemented;
the user has provisionally accepted Phase D handling and revised player-wake motion,
with continued evaluation over time.
The [handling and speed review](#handling-and-speed-review--2026-10-19) adds quicker
countersteering and a modest speed view, accepted by the user on 2026-10-19.
A [Swedish ensign](#swedish-ensign--2026-10-20) now responds to a light breeze and boat-relative air flow.
The score-loop arcade mode remains proposed. The [free-riding adventure plan](FREE_RIDING_ADVENTURE_PLAN.md)
now owns the proposed staged world expansion toward Vinga, inspired by the Boelo storm report
on 2026-10-25. Its [execution handoff](FREE_RIDING_ADVENTURE_FIRST_STEPS.md) records
the completed A0.1 [current-map reference](adventure/baseline.md) and A0.2
[pinned coastal sources](adventure/README.md#coastal-sources-a02-2026-10-25); the A0.3 route
survey is next. No expanded world is implemented.

Controls update, 2026-10-26: the isolated `controls-pedals-stability` branch implements the
approved W/S pedals, Shift precision, safe reset and automatic aerial stabilization plan.
It is not integrated or user-played. Current controls live in [Marine HUD](BOAT_HUD.md), with
[verification and integration handoff](CONTROLS_PEDALS_HANDOFF.md); earlier dated control
claims below describe their historical implementation. Next: integrate, then evaluate feel.

The user reports current jumping, landing and capsizing are fun. The
[multiplayer plan](MULTIPLAYER_PLAN.md) owns the multiplayer track: preserve that feel
with an authoritative native-first world for four friends, beginning with a two-player basin
slice. M3 added a diagnostic network room (`boat_server`, `wake_velocity --connect`) without
prediction; the first playable is M4. Shared free boating preceded the
[deathmatch](DEATHMATCH_PLAN.md) implementation. The next requested mode is a
[45–60 second river race](RACE_PLAN.md) with vessel-made jump wakes, a shared grid and
finishing positions. Its [R0 ferry-wake fixture](RACE_PLAN.md#r0-prototype--2026-10-24) is
implemented on a shortened route, and its [R1 rules core](RACE_PLAN.md#r1-race-core--2026-10-24)
and the [R2 network race](RACE_PLAN.md#r2-network-race--2026-10-24) (`boat_server --mode race`,
HUD and results) are implemented and automatically verified; human race play is pending.

Current clock/replay compatibility is revision 6 after foundations C1; revision-5
personal best files remain on disk and are not loaded into current play.

This document owns gameplay direction. Detailed next tasks belong to the
[physics plan](BOAT_PHYSICS_PLAN.md), [living-city plan](LIVING_CITY_PLAN.md), and
[river expansion record](river/README.md). See the [plan index](README.md)
for status across tracks. Revise the design as we discover what is enjoyable to play.

The [2026-10-19 architecture review](ARCHITECTURE_REVIEW.md) records growth risks and a proposed
technical sequence for exploration/racing. The review is complete; P1 instrumentation is implemented.
The [foundations implementation plan](BOATING_FOUNDATIONS_PLAN.md) now owns the ordered tasks:
instrumentation, clock correction, explicit world ownership, and simulation/presentation
separation. P2a, the existing arcade-loop simulation benchmark, is next; the foundations plan
defines minimum multiplayer prerequisite slices while remaining full-phase tasks stay pending.
Verification evidence and limits belong to those documents; existing gameplay acceptance and
the feature tracks below remain separate.

Turn banking update, 2026-10-20: T2 adds lateral-load-driven roll with inertia and
planing dependence; speed and turn tightness both affect the bank. Waves share the same
physical hull and the camera remains upright. Banking introduced handling revision 3; the
H3 carving pass introduced revision 4. The subsequent [arcade gameplay pass](WAKE_ARCADE_IMPLEMENTATION.md)
used revision 5 and preserved earlier best files separately. The subsequent
[clock migration](BOATING_FOUNDATIONS_PLAN.md#c1--authoritative-ticks-precise-seconds-compatibility)
uses revision 6 and keeps revision-5 files on disk.
The [banking plan](BOAT_TURN_BANKING_PLAN.md) records T3 dynamics coverage
and large-angle recovery tuning, followed by T4 presentation/replay fixtures and the spray
attachment correction. T5 now tunes quicker turn-exit recovery while retaining roughly
12° in hard turns. Automated/scripted verification is recorded there. The user's subsequent
feedback requested more bow grip and a clearer stern-led turn; the
[H3 carving pass](BOAT_PHYSICS_PLAN.md#h3--stern-led-carving-feel-2026-10-20) increases planing
hull grip while keeping the outboard as the force source. The user accepted the combined
banking/carving feel on 2026-10-20: "This is way better. I think we're good here." No further
tuning is queued. The exhaustive hands-on location/controls/replay/focus checklist remains
unverified; acceptance does not establish that each check occurred.

## Eriksberg location removed — 2026-10-25

At the user's request the separate `eriksberg` location, a subset of the river crop, was removed
together with the four-minute `proteus` demo that used its smaller crop. The river (the Gothenburg
area, to be expanded) is the only mapped location; basin remains the default. Old material stays in
the git history.

- Code: `BOAT_LOCATION_ERIKSBERG`, the Eriksberg crop/assets/survey (`docs/eriksberg/`), the demo
  (`src/main.c`, `src/scenes.*`, `src/audio_spectrum.*`), `waterfront_storyboard`, the timeline
  and flock camera, `gpu_review.py`, `living_city_review.py`, `boat_water_review.py` and the
  “Below the surface” soundtrack. `waterfront_city` has a single crop and no selector;
  `waterfront_scene_evaluate(time, water, frame)` fills scenery and actors for every caller.
  The river survey moved to [`docs/river/`](river/README.md). `boat_water_review` drives only
  the river route. Protocol v5 renumbers river to location 1; v1–v4 peers are rejected.
- Tests: Eriksberg-specific CTests (`boat_net_eriksberg`, `boat_eriksberg_screenshot`,
  `boat_environment_eriksberg_screenshot`, `boat_water_route`, `boat_water_wave_route`) and the
  demo's `cube_*`, `gpu_review`, `audio_spectrum_is_valid` and `soundtrack_xm_is_valid` were
  removed; validators that used the Eriksberg definition now use the river.
- Verification, M1 Max/macOS 26.6.2, Debug, GPU tests on: warning-free build; all 110 active
  CTests passed (15 retired wake-field checks disabled). CPU and GPU+AA river captures and the
  fixed `gpu_fixture --city` view inspected; a 300-frame windowed river `--demo` with the
  profiler overlay exited 0 and its overlay was inspected. Not run: `prepare_waterfront.py` and
  `docs/river/survey.py` (pyproj is not installed), Windows/Linux/web builds and CI, and human play.

## Swedish ensign — 2026-10-20

Implemented: the player boat carries a two-sided Swedish flag on a starboard stern
staff raked about 11° aft and shortened from 1.40 m to 0.75 m. Following user feedback, the flag is one-third smaller
in each dimension (0.64 × 0.40 m); weaker air flow and softer fabric let it hang almost
vertically at rest. A fixed-tick cloth simulation responds to gravity, a gentle world-space breeze,
particle-relative air pressure/drag, and small wake eddies. Moving attachments carry
hull translation and attitude into the cloth; the fly can lag through turns and change
direction in reverse. CPU and GPU color/reflection/shadow passes share a vertex snapshot.
Playback and ghosts animate the flag without changing recording files or handling.
[The flag guide](BOAT_FLAG.md) records the model, playback approximation, and review commands.

Completed task: implement wind-driven cloth, renderer integration, playback history,
and close idle/cruise/turn/reverse inspection fixtures; revised idle drape, size, and
aft rake after user feedback, then shortened the staff by 46%. The user accepted the
flag on 2026-10-20: "I like it a lot!" The subsequent visual pass is recorded under
[speedboat skins](#speedboat-skins--2026-10-24);
no further handling tuning is queued.

Verification, 2026-10-20, macOS: Debug configure/build completed. Cloth dynamics and
replay checks passed; CPU idle/cruise/reverse and GPU turning captures were inspected,
including closely spaced cruise frames to inspect changing folds. The revised stopping
fixture with the shortened staff settles with 0.603 m fly drop and 0.183 m horizontal reach. A 300-frame desktop
GPU flag review and a 300-frame Eriksberg GPU+AA/waves demo smoke run exited successfully.
All 77 CTests passed in the full run, including 20 GPU checks. After shortening the
staff, all seven affected cloth/replay/presentation and CPU/GPU screenshot checks passed
again. The final short-staff GPU idle capture was inspected, and a 180-frame desktop
flag animation smoke run exited successfully. Formatting and changed local file links
were checked.
Flag appearance is user-accepted. Hands-on controls/audio assessment, Windows/browser
verification, and exhaustive wind-relative headings remain unverified. The small solver has no
self-collision or hull/cabin collision; seek cloth reconstructs bounded history.

## Speedboat skins — 2026-10-24

Current integration handoff — 2026-10-25: the user accepted the sleeker speed profile
("Much nicer!") and twin motors ("Great!") and requested preparation for main. The `boat-white-hulls` worktree now adds twin outboards to that
accepted profile, following the white hulls and deep cockpit (`4010f37`). The profile uses a
finer seven-station bow, graduated forefoot, lighter rub rail, gently crowned foredeck,
lower swept windscreen, tapered aft fairing and bevelled outboard cowl. All four accepted
liveries, the cockpit cutout and lowered seats remain; there is no helm. The earlier upper-bow
rake experiment was rejected and reverted. The twin-motor extension changes presentation only;
it does not establish improved hydrodynamic performance or alter the simulation.

Ready: `boat-white-hulls-integration` combines the accepted work with committed main
`642ca78`, retaining A1.4/A1.5. All 116 enabled combined CTests passed with GPU checks;
16 deterministic captures match the accepted source worktree byte-for-byte. The concurrent
renderer work landed as A2.1–A2.3; the three commits then applied cleanly onto main `76d3883`
and were folded in on 2026-10-25 ([handoff](BOAT_SKINS_HANDOFF.md#integrated-into-main--2026-10-25)).
[The integration handoff](BOAT_SKINS_HANDOFF.md) owns the exact commits, commands, logs and
verification scope. Main's working files were not edited during preparation.

The single/twin-outboard comparisons for all four boats are at
`build/boat-color-review/seat-depth/comparison.html` and
`build/boat-color-review/twin-outboards/comparison.html` in the source worktree. The earlier
accepted-profile comparison remains under `build/boat-color-review/speed-profile/`.
These are local review artifacts, not committed assets. Each render pass draws both motors
with fixed hull-relative mounts and the existing steering rotation. Thrust, mass, handling,
wakes and network data are unchanged. A pose check covers both mounts under pitch, roll,
heading and three steering angles. Source-worktree verification follows; combined-main
verification is recorded in the handoff.

Verification of the accepted speed profile on macOS, 2026-10-25: the full Debug build passed without
compiler warnings; the skin validator passed shared-geometry, finite/nondegenerate mesh,
palette and footprint checks. All 91 enabled CTests passed in 67.33 seconds with localhost
access; 13 retired experiments remained disabled. Representative CPU side/bow/raised-stern
captures across the four liveries and Sweden river/Norway basin follow captures were inspected.
The gallery contains 24 matched before/after captures; its local image links resolve.
Formatting and `git diff --check` passed. Logs and captures are under
`build/boat-color-review/speed-profile/`. No manual playback was performed for this geometry-only
proposal; GPU, Windows and browser visual review remain unverified.

Twin-outboard verification on macOS, 2026-10-25: warning-clean full Debug build; all 91
enabled CTests passed in 66.69 seconds with localhost access (13 retired experiments disabled).
The skin validator now checks fixed, separate mounts under heading, pitch, roll and steering.
CPU side/bow/raised-stern captures were produced for all four liveries, with the accepted
single-motor profile as the matched baseline; representative views and a CPU turn capture
were inspected. A 180-frame Sweden GPU turning demo completed, and a Norway GPU turning
capture plus Sweden river/Norway basin CPU follow captures were inspected. These are scripted
smoke checks, not manual driving or listening. The updated gallery's 56 local image links
resolve. Formatting of the changed skin/validator code and `git diff --check` passed. Evidence
is under `build/boat-color-review/twin-outboards/`. The user subsequently accepted the twin-motor look. Windows and browser review remain
pending.

Accepted baseline: the user accepted the speedboat look on 2026-10-24 ("I like the look!") and then
requested four Nordic countries. The isolated `boat-speedboat-skins` worktree now implements
Sweden, Finland, Denmark and Norway in that order, with matching liveries and animated flags.
The user accepted the Nordic versions ("Great!") and requested integration handoff.
The cosmetic work is complete; [the multiplayer handoff](BOAT_SKINS_HANDOFF.md) records the
branch, APIs, likely conflicts and remaining integration checks.

Completed: pointed foredeck, faceted V hull, low swept windscreen, two-seat open cockpit,
compact steering outboard, and country palettes: Swedish blue/yellow, Finnish white/blue,
Danish red/white and Norwegian red/white/navy. The same per-view skin ID selects the flag
for CPU/GPU color, reflection, shadow, replay and ghost rendering. Norway's white-bordered
blue cross uses render-only subdivisions; all skins share the accepted cloth size, simulation,
boat geometry and gameplay class. No physics, contact, wake, audio, protocol or recording
format changes. [The skin guide](BOAT_SKINS.md) owns commands and the integration boundary.

Nordic-pass verification on macOS, 2026-10-24: warning-clean Debug build; extended skin checks
pass for country colors, both flag faces, Norway's border, independent mesh storage and
unchanged input cloth. CPU cruise captures for all four countries were inspected at 960x600 and native 320x200,
as was the Norway follow-camera frame. GPU side captures confirm Finland's blue cross and
Norway's white border. A 180-frame Sweden GPU+AA/waves demo and a 180-frame Norway GPU
turning/flag review ran as scripted motion smoke checks. All 102 active CTests passed in
83.21 seconds with `PROTEUS_GPU_TESTS=ON`, `PROTEUS_WAKE_FIELD_TESTS=OFF`, desktop graphics
and localhost access; 15 retired experiments remain disabled. Formatting, changed local
file links and `git diff --check` passed. Captures/logs are under `build/nordic-*` and
`/private/tmp/proteus-nordic-ctest.log`. These checks do not establish hands-on driving,
listening or acceptance of the newly requested national themes.

Integrated into main on 2026-10-24 (`38d76c6`); multiplayer draws room slots 1–4 as Sweden,
Finland, Denmark and Norway on every client
([fleet rendering handoff](MULTIPLAYER_PLAN.md#m4-step-4a-fleet-rendering-handoff--2026-10-24)).
No persistent loadout, network selection, unlocks or arbitrary-color UI. Hands-on
driving/listening and Windows/browser checks remain pending.

Handoff completed 2026-10-24: the isolated branch is committed for cherry-pick. A merge-tree
preview against multiplayer `6f26862` auto-merged without conflicts; the combined program
has not been built/tested. The final branch-only Debug build and all 102 active CTests passed
again (80.70 seconds, same GPU/wake options). See the handoff for commands and evidence.

### First speedboat pass — verified 2026-10-24, look accepted

The initial Tide/Ember/Cobalt/Orchid palettes are superseded by the country choices above.
The following is verification history for that initial geometry pass, not the Nordic revision.

Verification on macOS, 2026-10-24: Debug build passed without compiler warnings. The skin
validator checks shared geometry, palette customization, valid triangles, bounds and fallback.
CPU front/rear/side and native 320x200 follow/close captures were inspected across all four
liveries; a GPU turning capture was inspected. A depth artifact on the windscreen was found
and corrected with thin solid panes. The post-fix GPU capture was inspected and the artifact is gone. All 102 active CTests
passed with `PROTEUS_GPU_TESTS=ON` and `PROTEUS_WAKE_FIELD_TESTS=OFF` (15 retired experiments
disabled), including core/network boundaries and ENet loopback, in 83.20 seconds with desktop
and localhost access. The earlier sandbox run could not bind the loopback socket; rerunning
with the needed access resolved that environmental failure. A 180-frame Orchid GPU/waves
boating demo and 180-frame Ember close turning/flag review exited successfully; the close
review was repeated after the pane fix. This is scripted motion smoke evidence. Formatting,
`git diff --check` and changed local documentation links passed. Local captures and smoke logs
are under `build/skin-*`; full test output is `/private/tmp/proteus-skins-ctest-gpu.log`.


### Historical cabin proposal — saved 2026-10-20, superseded

The earlier proposal refined the boxy cabin, roof, gunwale and cream/teal palette. It was
not implemented before the user chose the low speedboat direction on 2026-10-24. Its intent
to preserve the flag, simple geometry, handling and nearest-neighbour presentation remains.

## Marine HUD — 2026-10-24

Implemented in the isolated `boat-marine-hud` branch: vertical thrust, speed and steering
range indicators without units or numbers. A slightly translucent marine case sits at the bottom of the
scene, with all instrument pixels sharing the scene's 320x200 grid.
[The HUD handoff](BOAT_HUD.md) owns API, integration notes, review commands,
verification and limitations. Completed: presentation module and local view integration.
The user prefers this shared-pixel, visual-only direction. Next: review bottom placement
and transparency, then integrate with the main multiplayer branch. Spring-to-neutral
thrust remains undecided; held-thrust behavior is unchanged. Connected HUD review is pending.

## Window presentation — 2026-10-19 (historical instrument-free baseline)

Implemented: the 320x200 scene uses the entire window's integer-scaled presentation area,
with letterboxing when needed. The dashboard, wheel, speed/thrust instruments, and persistent
shortcut hints are removed. `?` opens a modal keyboard guide; `?` or Esc closes it. Help pauses
simulation, replay, and audio and blocks driving/camera/replay controls. Esc outside help exits.
Course timing, route map, and replay progress remain visible in course mode. Map attribution
moves to help. The optional F3 profiler remains available.

The marine HUD above supersedes the instrument-free layout; full-window scene scaling and
modal help remain. The following evidence records the original 2026-10-19 change.
Verification on 2026-10-19, macOS: Debug configure/build and all 69 CTests
(including 18 GPU checks) passed. A default GPU
`--demo --frames 120` desktop smoke run exited successfully; a 600-frame preview was captured
and inspected at the initial 960x600 client size, confirming the scene fills the window without
the dashboard or hints. Manual keyboard-modal, resize, and audible playback review remain
unverified; user acceptance is pending.

## Current prototype

`--location river` selects the bundled geographic waterfront, from the Eriksberg dock to
Älvsborgsbron; `--location basin` selects the physics test area (the default). The river spawn
is in the dock basin facing the crane and river. A shared CPU/GPU blockout includes the bridge
deck, open towers, main cables, and hangers. The four tower-leg footprints obstruct navigation
while the deck stays above the navigable water. Its default follow view is 12 m back / 6 m high;
C restores that view. See the [bridge expansion record](river/README.md) for assumptions and
route checks. Buildings, shore, Dockbron, crane and sky come from the bundled offline scene. The held controls, follow
camera, wake, and spray work in either location; R returns to the selected location's spawn.
Water-triangle boundary edges constrain an oriented bow, stern, and beam footprint at the shoreline
and map crop. Glancing impacts scrub speed, turn the craft away, and rebound gently. The river
supports `--gpu --gpu-aa`: the player hull, engine and airborne
spray participate in depth, reflection and shadow passes; surface foam is depth-tested and receives
shadows without casting solid shadows. Optional AA uses 4×4 supersampling before integer scaling.
GPU is the default for all locations, including the basin course. `--renderer cpu` selects the
software renderer for window-free captures; GPU captures need a desktop graphics context.
`--gpu-aa` only enables supersampling and does not select a renderer or location.
Buildings and quays remain static, with no overhead structure collisions or camera
occlusion handling near buildings. It does not include the
rest of Gothenburg or the archipelago. Source attribution is retained in the title Credits screen and README.

The [generated boat and berth foundation](HARBOR_BOATS.md) adds six leisure boats at a
west-bank pontoon and a working east-bank berth in the river location. Moored
hulls, pontoon fingers, and gangway have forgiving static contacts. The visual generator
covers skiff through speedboat and passenger-ferry recipes; it does not change the player craft or implement
speed tiers, unlocks, prices, or progression saves. Those remain future design work.

Three travelling leisure craft and a compact ferry now follow authored dock/river circuits
in both geographic locations. [Harbor traffic](HARBOR_TRAFFIC.md) documents continuous
routes, visual wakes, lights, and moving hull contacts. Traffic follows fixed schedules;
it does not yield or navigate dynamically. Its wakes do not affect handling. The basin
course and replay baseline are unchanged.

The boat-level [water review](WATER_REVIEW.md) records a repeatable dock/river/quay circuit
and the completed 04f surface and 04g selected waterline passes. Those visual changes
preserve handling; the separately implemented river expansion adds playable geography.

Run `./build/wake_velocity`. Left/right or A/D adjust the wheel, clamped from full
left to full right. Centre to either limit takes one second. Releasing keys lets an overdamped
spring return the wheel smoothly to centre without overshooting; opposite input now unwinds
full lock in about 0.33 seconds, then builds the new turn at the original rate. Return strength is set by `WHEEL_RETURN_RATE` in `src/boat.c` (initially 3/s,
about 1.5 seconds from full lock to within 1% of centre). Up/W raises signed thrust and down/S
lowers it; releasing the keys holds the setting.
The thrust range is -100% (reverse) through 0% (idle) to 100% (forward), with four seconds of
held input covering the full range. Lowering
thrust passes smoothly through idle and continues into reverse only while Down/S remains held.
R resets to neutral thrust, centres the wheel, and clears effects; Esc exits.
The handling sketch has a third-person follow camera, stern-applied engine thrust, rotational
inertia, water resistance at two hull stations, and forgiving basin-wall collisions. Steering
angles the outboard; its lateral thrust pushes the stern and creates torque. The outboard follows
the wheel as it springs back when keys are released. Returning the wheel to centre lets rotation settle
through water resistance. Countersteering can catch
a turn. At idle there is no powered steering, and a stationary boat cannot turn. Propeller wash
moves opposite the thrust direction, independently of the spreading wake. The model uses normalized
mass and tunable forces, not calibrated hydrodynamics. Forward drag combines linear and quadratic
terms. Lateral resistance at each hull station increases with forward speed and local sideways
velocity, including rotation. A passive force across the angled submerged outboard uses the local
stern velocity and acts at the stern, producing both drag and turning torque. It works at idle
while moving and fades to zero with water-relative motion. Calm water remains the baseline. A
rate-limited, hysteretic planing blend reduces forward hull resistance above cruising speed and
returns smoothly to displacement handling while slowing. Six damped buoyancy supports provide
physical heave, pitch, and roll; planing redistributes support to raise the bow during the
transition and settle at cruise. Hull, cabin, and outboard share the physical pose in both
renderers, including GPU reflections and shadows. The camera keeps a level horizon.
`--waves` enables gentle crossing waves with water-relative resistance and matching visual
surfaces. Free boating in every location now enables physical player wakes by default;
`--no-player-wake` restores the visual-only trail. Opt-in `--current` and `--crossing-wake`
profiles also work in all locations, with an authored river source/channel for mapped views.
They reject course/replay/ghost modes, which retain their existing water settings. See
[Phase D](BOAT_PHYSICS_PLAN.md#phase-d--currents-and-crossing-wakes).
Default arcade wake pieces live for sixteen seconds and add signed height at crossings.
`--player-wake` selects the legacy D3 model, whose crests travel at 3 units/s through a
slower packet and persist about twenty seconds. See [the wake plan](WAKE_REWRITE_PLAN.md).
Harbor traffic still leaves visual foam only.
A spreading, fading wake traces the travelled path, with varied foam patches and short-lived
stern wash that separates and erodes; planing broadens the trail and adds symmetric spray
even on straight runs. Lateral slip throws turning spray from the sliding side of the hull. Two orange markers provide turning
references but do not collide. Rendering is 320x200 with nearest-neighbour integer scaling. Losing
focus pauses movement.

The simulation uses fixed 120 Hz ticks. Handling constants live in `src/boat.c`. Screenshot mode
runs a deterministic example without a graphics context when using CPU rendering; GPU captures
require a desktop graphics context. See the README for commands.
The 240x160-world-unit basin is a handling test area, not yet a representation of Gothenburg.
It is four times wider and deeper than the original basin. The camera follows the boat from
20 units behind and 14 units above, looking 5 units ahead. Its heading eases through turns on fixed
simulation ticks, including angle wraparound. `-`/`+` adjust distance (8..48), PageUp/PageDown adjust
height (4..36), and `C` restores the default view. `R` resets the boat
without changing them. Settings are per-run; `--camera-distance` and `--camera-height` also configure
interactive runs and screenshots. River geographical scenery is implemented;
collision splashes belong to later stages. Player evaluation of handling is the
next design input: especially steering responsiveness, the amount of slide, and coasting into a turn.

## Handling and speed review — 2026-10-19

Completed and user-accepted on 2026-10-19: a focused feel pass requested by the user,
building on the provisionally accepted Phase D baseline. Implementation details, measurements and acceptance evidence belong to the
[physics plan](BOAT_PHYSICS_PLAN.md#handling-feel-pass--2026-10-19).

The code review found that opposite steering used the same one-second centre-to-lock ramp
as initial turn-in: a full-lock correction spent a second merely unwinding the existing turn.
Release centring initially unwound faster than explicitly countersteering. The first change
therefore improves correction response while keeping the existing hull forces and gradual turn-in.

Other findings: the 55° lens was identical at docking and cruise; the river follow view is distant
at 32/6 and the basin/Eriksberg view is high at 20/14. The historical full-window pass removed thrust
feedback (restored by the marine HUD),
so persistent throttle and the neutral crossing now depend on boat/wash response. Reverse is still
a four-second full-range throttle adjustment, which supports deliberate docking but deserves a
separate usability test. Accepted stern foam, spray, waves and quiet audio already provide useful
feedback. The existing S course, retry, personal best and ghost supply a short practice loop.

### Research and how it applies

Sources reviewed 2026-10-19. These support design hypotheses, not claims that a particular number
makes Proteus more fun. Older talks/papers are used for design principles, not current APIs.

| Primary source | Relevant finding | Application here |
| --- | --- | --- |
| [Matthew Harris / Criterion, Vehicle Feel Masterclass, GDC 2018](https://www.gdcvault.com/play/1025295/Vehicle-Feel-Masterclass-Balancing-Arcade) | The session frames vehicle feel as physical simulation plus camera behavior and handling assists. | Tune the input/camera layer around the existing hull model. The session overview was reviewed; the full presentation was not watched. |
| [Richard Goodenough, Clemson, 2010](https://open.clemson.edu/all_theses/978/) | Changing geometric FOV affected perceived speed; in that simulator larger geometric FOV generally reduced perceived speed. | Wider does not automatically mean faster. Test a small 55–62° lens change with a constant-lens comparison; evaluate the actual 320x200 view. This study used a driving simulator, not this boat camera. |
| [Matt Small / Vector Unit, Hydro Heritage, 2017](https://vector-unit.squarespace.com/blog-posts/2017/2/24/hydro-heritage-evolution-of-an-arcade-racer) | Hydro Thunder emphasized shortcuts/powerups; Riptide added interactive waves and risk timing for stunt rewards. | Give the player satisfying lines and well-signalled water encounters; use the existing route/ghost foundation before adding a boost economy. |

### Ordered experiments

These priorities are design judgments from the audit and research, not measured player preference.
Only the first row is implemented and user-accepted in this pass. The remaining rows are proposals.

| Priority | Experiment | Success criterion / guardrail |
| --- | --- | --- |
| 1 | Accepted quicker countersteering and small speed view; `--fixed-fov` for comparison | Catch the S turn with less correction delay; keep docking precision, readable boat and comfortable horizon. See physics H1/H2. |
| 2 | Compare a closer, lower follow view using existing `--camera-distance 18 --camera-height 6` | Stronger nearby water/scenery motion and clearer hull attitude without hiding upcoming buoys. Test camera positioning before more FOV or camera shake. River now defaults to 12 m back / 6 m high (2026-10-24); user-approved with the finer rain on 2026-10-24. |
| 3 | A short course with a broad sweeper, one S, an exit straight and an optional tighter line | Meaningful throttle/line choices and rapid R retries; measure clean completion and voluntary retries, not just best time. Retain forgiving contacts; new gate layouts require a course revision. |
| 4 | Superseded by the [marine HUD](BOAT_HUD.md) | Persistent thrust/neutral feedback is now implemented in the isolated HUD branch; visual review and multiplayer integration remain. |
| 5 | Put an existing crossable wake where its approach and alternate line are visible | Choosing to ease off or cross at an angle is enjoyable and recoverable. Use current wave physics; avoid invisible impulses or required jumps. |
| 6 | Restrained gate/finish confirmation and readable split improvement | Completing a clean line feels rewarding; preserve the accepted peaceful audio balance. Sound changes need listening review. |

Speed/handling follow-ups to defer until play warrants them: high-speed steering-angle limits,
extra yaw damping, automatic countersteer, boosts, drift scoring and manual trim. Each changes
skill or adds systems; the current problem can be tested with input-response changes alone.
Avoid piling on blur, speed lines or camera shake at this resolution before establishing whether
camera placement and world motion provide enough sensation.

Suggested comparison commands (GPU, optional AA):

```sh
./build/wake_velocity --location river --gpu-aa
./build/wake_velocity --location river --gpu-aa --fixed-fov
./build/wake_velocity --location river --gpu-aa --camera-distance 18 --camera-height 6
./build/wake_velocity --course --gpu-aa --data-dir build/handling-review/profile
```

User acceptance, 2026-10-19: "Feels great!" Priority 1 is accepted. Next: preserve this baseline
and collect longer-term feedback; a closer camera comparison is the next optional experiment.
Trim/loading and score-loop work remain unstarted. The physics plan owns the latest verification
record and limits; the feedback does not establish exhaustive scenario or platform coverage.

## Direction

Turn Proteus's waterfront into a small interactive boating playground. The central appeal is
being able to boat around Gothenburg and its surrounding archipelago: moving through a place,
finding passages, carving turns, and enjoying the water.

Start with a satisfying water-skimming toy. A small arcade game can grow from that foundation.
Free exploration is a first-class experience; racing is an optional reason to learn the handling
and revisit routes. Neither a detailed boat simulator nor a large open world is required.

Keep the demo spirit through low-resolution visuals, tracker music, bold effects, and expressive
motion. Player actions give those effects a purpose: wakes describe movement, spray communicates
sliding, and the waterfront becomes somewhere to go.

## Setting and scale

The longer-term setting is Gothenburg's waterfront and nearby archipelago. Begin with one compact
area inspired by the existing Eriksberg scene, with enough open water for sweeping turns and a few
obstacles or passages to explore. Reuse existing scenery where practical.

Prefer a small, recognisable interpretation over exhaustive geographic accuracy. The degree of
real-world fidelity remains open. Do not make map imports or modelling the entire archipelago a
prerequisite for playing.

The [2026-10-25 adventure plan](FREE_RIDING_ADVENTURE_PLAN.md) proposes a continuous expansion
from the existing river through sheltered approaches toward Vinga, with rocky shores, channels
and a few distinctive landmarks. Build independently playable slices and offer nearby starts
for shorter outings. An enjoyable harbour and one island route remain a meaningful first result.

## First playable baseline

- One craft, one compact area, free driving, and instant reset.
- Left/right adjust a spring-centred wheel; up/down adjust a persistent throttle within
  -100..100%; a separate reset key centres the wheel and idles the throttle.
- Momentum persists through turns. Stern thrust creates torque, and rotation builds with inertia.
  Water resistance along the hull damps sliding and rotation.
- Throttle affects both forward push and turning force. Reducing it cuts thrust; momentum remains.
  Defer a dedicated brake until play suggests it is needed.
- Shore contacts cause forgiving rebounds; buoys are non-colliding markers. Collision splashes
  remain a presentation proposal.
- Follow the boat from behind and above, with a stable horizon and gently smoothed heading.

The first success criterion: driving in circles and carving around a buoy is enjoyable for a minute,
even without a score. Tune movement before adding content if this does not hold.

## First timed course

`./build/wake_velocity --course` selects a basin-only S course. The boat spawns south of a
start line; a forward crossing between the posts starts the timer. Two ordered, offset buoy
gates require broad turns before a chequered finish line stops the clock. Yellow marks the
next gate, and a HUD route map shows progress. Gates use swept boat-centre crossings toward
north, with interpolated start/finish times on fixed simulation ticks. Skipping a gate cannot
finish a run. Missed gates can be retried while the clock continues. Markers do not collide.
R resets both the boat and attempt; focus loss pauses both. Free boating is still the default.
Completed runs are recorded as poses at 120 Hz with interpolated START/FINISH samples and gate
splits. The fastest run persists across launches and becomes a non-colliding cyan ghost on retry;
calm water and gentle waves have separate bests. G toggles the ghost. Before START or after FINISH,
L/B replays the last/best run; Space pauses, Enter restarts, and hold Backspace 1 s returns to racing. A retry discards
only the unfinished recording. Replay omits water particles and allows the usual camera controls. Left/Right seek one second
(Shift: five seconds), Home/End jump to START/FINISH, and brackets select 0.25× through 8× playback; backslash restores 1×.
A clickable timeline marks gate splits; clicking/dragging seeks and pauses. Finish pauses playback;
Space starts again from the beginning. Seeking and speed changes preserve the recording and best.
Portable, versioned run files can be exported with `--save-run`, watched with `--replay`, or selected
as a ghost with `--ghost`; see the [README controls and storage notes](../README.md#boating-sketch).
There is one saved best per water preset; a broader leaderboard is deferred. This small time trial
was implemented ahead of the proposed minute-long scoring mode and remaining physics experiments.
The time-trial foundation is now in place; it is not an outstanding prerequisite for physics D1.

## Optional arcade challenge

Deferred proposal: a score-loop alternative to the implemented S time trial. It would add
five or six ordered buoy gates forming a short loop. Clearly highlight the next gate. Include a broad sweeping turn, a tighter corner, and an acceleration straight.

The proposed challenge is a 60-second score run:

- Passing gates in order earns points.
- Completing a lap earns a bonus.
- Keep looping until time expires, then show the score and allow immediate restart.
- Keep the best score in memory initially.

Exact scoring values are tuning decisions. No opponents, enemy AI, or complicated combo system are
needed. Free boating remains available without a timer or mandatory gates.

## Stern-wake motion — 2026-10-18

Status: implementation and automated verification complete; user motion/artistic review
is next. The user likes
high-speed bow spray but reports that the stern wake looks static at full speed. Astra
reviewed the particle implementation and recommends a bounded visual pass here. This
continues the movement feedback in physics B2/C3; the D1 current and D2 physical
crossable wake belong to the separate basin physics pass in the [physics plan](BOAT_PHYSICS_PLAN.md).

Baseline code review, before this pass: `water_step` already left world-space history. It emitted identical paired
foam patches every 0.55 m and identical propeller-wash patches every 0.04 s. Surface
patches spread, grow at the same rate, and fade monotonically. Overlap and regular spacing
can therefore look like a steady V beneath a following camera at constant speed. This was
the code-based explanation of the reported symptom. Matched baseline and revised
sequences have now been captured; see the implementation record below.

Reviewed implementation brief (now implemented with surface foam): prototype stern-local
churn and breaking foam while preserving the current
bow/turning spray, handling, camera, and historical curved trail.

1. Capture a repeatable full-speed straight run before changing visuals, plus acceleration,
   a turn, neutral coast, and reverse. Include the default and low river follow cameras.
2. Give stern foam deterministic per-birth variation in size, spacing, lateral drift, and
   lifetime. Make short-lived, coarse foam clusters visibly emerge, travel aft, split or
   lose coverage, and dissolve; retain quieter spreading foam farther behind. Use bounded
   variation rather than whole-wake brightness pulsing or fine flicker. Keep bow-spray
   emission variation independent so its existing appearance survives the stern pass.
3. Start with the existing fixed-step particle system and shared CPU/GPU pose path.
   Tune the near-stern propeller wash before adding geometry. If flat foam still lacks
   volume, try a few low, short-lived stern droplets, visually below the bow spray.
   Keep wash responsive to signed engine thrust and steering, and hull wake responsive
   to water-relative travel; coasting and reversing must remain understandable.
4. Review the existing 384-particle budget before adding emitters; stern additions must
   not evict front spray. Share appearance calculations between scene-frame and direct
   CPU drawing. Preserve the curved
   history through turns, surface attachment, shoreline masking, and renderer parity.
   No physical wake forces or broader water-surface redesign are required for this pass.

Acceptance: at steady full speed, individual coarse stern features visibly move and
break up in the follow view; motion reads at 320x200 without relying on AA. Preserve the
approved front spray and avoid a continuous white carpet, synchronized pulses, sparkling
pixels, and repeated dotted rails. Compare matched before/after clips with waves on/off
and GPU AA on/off, check CPU representative frames, and manually drive the transition,
turn, neutral, and reverse cases. Run the Debug build and CTest; verify deterministic
replay and particle capacity when the implementation changes. User artistic acceptance
remains pending until the moving result is reviewed.

Verification for this planning step (2026-10-18, macOS): inspected source and existing
plan ownership; Debug build and all 54 CTests passed, including 14 GPU checks with
`PROTEUS_GPU_TESTS=ON`. `git diff --check` passed. No runtime changes, new manual
playback review, or audiovisual acceptance claimed. No dedicated stern-animation task
was found in the live plans; the earlier water review's preservation brief describes its
completed surface pass.

### Stern-wake implementation and verification — 2026-10-18

Implemented in `src/boat.[ch]`: unequal, short-lived propeller-wash lobes with varied
rearward speed and outward drift; a deterministic separate foam-birth sequence varies
the historical trail's spacing, width, lifetime, and spread. Wash first expands then
shrinks as it fades, opening gaps instead of filling a central stripe. Foam size/colour
are shared by direct CPU rendering and scene-frame/GPU rendering. Surface attachment,
shoreline masking, signed thrust direction, and the fixed 384-particle pool remain.
No additional stern droplets were needed in this first pass. Bow-spray birth, trajectories,
size, colour, and its original variation sequence are preserved. Expired slots are reused;
if saturated, new particles may replace surface foam but never live airborne spray.

Completed checks on macOS:

- Debug build passed without warnings. All 54 CTests passed across the initial CPU run
  and desktop-access GPU run: 14 GPU tests plus their replay fixture passed; the final
  XM test also passed separately. The sandboxed suite was interrupted at its first GPU
  test because macOS desktop access was unavailable there.
- Added handling checks that change the stern seed while requiring identical boat
  positions/headings and matching live bow-spray particles throughout acceleration,
  a turn, and reversal. A deliberately full spray pool rejects new foam. Existing
  handling, wave-attachment, reset, and replay tests passed.
- Captured matched before/after 320x200 sequences at 20 Hz with the normal 20/14 camera
  and low 32/6 camera, GPU AA on/off and waves on/off. The eight-second basin sequence
  begins after four seconds of full throttle, then turns, coasts, and reverses; basin
  position and existing foam are translated together at segment boundaries to avoid
  wall contacts. Inspected representative cruise, turning, coasting, and reverse frames
  and temporal strips. The revised trail has irregular gaps instead of continuous rails;
  the near-stern clusters change shape and position while the curved trail remains.
  Peak live particles: 268 flat / 273 waves, versus 256 / 263 in the baseline.
- Repeated the CPU sequence from reset: all 160 exported frames were byte-identical.
  A desktop `wake_velocity --location river --waves --gpu-aa --demo --frames 360` smoke
  run exited successfully. This is scripted playback, not exhaustive manual driving.
- `git diff --check` and changed documentation links passed.

Artifacts are local under `build/wake-review/`: `review.c` and the baseline/revised
review executables, per-preset PNG sequences/logs, `motion-*.png` temporal strips, and
`compare-*.mp4` clips (baseline left, revised right). `compare-aa-w0.mp4` uses the low
camera; `compare-gpu-w0.mp4` uses the normal camera. Review these in motion before
artistic acceptance. Current limits: the frame audit samples 20 Hz, human continuous
motion/input review remains pending, and the existing pose-only saved replay format
does not regenerate water particles. No physical crossing wake was added in this
presentation pass; the later Phase D basin implementation is tracked in the physics plan.

Next task: user review of full-speed stern motion, especially from the low follow view;
then tune churn density/contrast if needed. Keep the accepted bow spray as the reference.

## Presentation and feedback

The current craft, wake, and turning/planing spray provide visual movement feedback.
The table below is a presentation wishlist, not a list of implemented effects: the study has
engine audio, but no buoy impacts, gate sounds, score laps, or impact camera kick. Preserve
nearest-neighbour, integer-scaled presentation while refining feedback.

| Player action | Feedback |
| --- | --- |
| Accelerate | Wake lengthens; engine pitch rises |
| Turn hard | Spray fans out from the craft |
| Bump a buoy or shore | Brief splash, impact sound, and restrained camera kick |
| Pass a gate | Buoys flash and a short musical sound plays |
| Complete a lap | A larger ripple or colour pulse crosses the water |

Steering and gameplay feedback stay immediate. Music may drive decorative pulses and motion;
gate sounds should fit the track. This does not require rhythm-game timing. Explore whether quieter
audio suits free boating once the basic experience exists.

## Implementation stages

These stages describe the design sequence; current implementation is recorded explicitly below.

1. **Handling sketch (implemented):** simple craft, water, steering, thrust, momentum,
   boundaries, and reset.
   Evaluate the pleasure and readability of movement; keep this cheap to revise.
2. **Water toy (implemented except buoy contacts and collision splashes):** wake, turning
   spray, forgiving bumps, and a few buoys in a compact waterfront area.
   Evaluate whether movement feels expressive and whether cruising is enjoyable on its own.
3. **Timed challenge (S time trial implemented):** ordered gates, timer, fast restart, persistent
   bests, ghosts, and replay. The original minute-long score-loop remains deferred.
   Evaluate whether another attempt is appealing without adding progression rewards.
4. **Free-riding adventure (planned 2026-10-25):** extend beyond the bridge, prove tiled world
   costs with a short sheltered/exposed outing, then expand through the approaches to Vinga.
   The [adventure plan](FREE_RIDING_ADVENTURE_PLAN.md) owns data, technical stages and acceptance.

`wake_velocity` reuses the scene and rendering foundation. Its controls and launch options live in
the [README](../README.md#boating-sketch).

## Later water-physics experiments

The accepted next-step roadmap is [Boat physics implementation plan](BOAT_PHYSICS_PLAN.md).
It breaks the ambitions below into independently testable tasks suitable for one coding-agent
session each. Continue those experiments in bounded steps; the initial timed S course is now available
as a separate mode. Do not implement the whole roadmap at once.

Keep calm water as a baseline for comparisons. The following summarizes the implemented
experiments and remaining direction; the physics plan owns task status and acceptance.
These are tunable gameplay models, not calibrated hydrodynamics.

- **Speed-dependent steering (first approximation implemented):** tune stern thrust, passive
  outboard resistance, and speed-dependent hull resistance through coasting turns and low-speed
  manoeuvring. The current model uses forces and torque, not a prescribed speed-to-turn curve.
- **Planing (implemented):** a smooth resistance transition and physical support redistribution
  raise the bow, then settle it into cruise.
- **Current (D1, implemented; provisionally accepted):** an opt-in authored channel in all locations adds drift
  and local water-relative resistance; measured harbor currents remain future work.
- **Gentle waves (C1–C3, implemented and accepted):** physical buoyancy and encounter resistance
  depend on speed and heading. Visual shelter/exposure varies independently of the wave physics.
- **Crossable wakes (arcade default, implemented and enjoyed):** the
  [wake plan](WAKE_REWRITE_PLAN.md) owns bounded per-boat pieces, signed overlap, flight,
  landing and capsize. Free boating uses arcade objects; `--player-wake` selects legacy D3,
  `--no-player-wake` opts out, and `--wake-field` remains a separate native basin experiment.
  The dynamic basin rival shares wakes and mutual impacts; ordinary harbor/traffic trails
  remain visual. Multiplayer sharing/authority is planned separately in
  [the multiplayer owner](MULTIPLAYER_PLAN.md), preserving the accepted arcade feel.

Judge each experiment through driving before combining it with the others. Wake and spray should
help reveal movement through water as these models evolve.

## Scope boundaries

The first complete game can be one arena, one craft, one course, one music track, free driving, and
one score mode. The archipelago is the direction for expansion, not a requirement to build a vast map.

The [free-riding adventure plan](FREE_RIDING_ADVENTURE_PLAN.md) adds a proposed optional
destination/shelter objective and recovery checkpoints. A general mission system remains deferred.
Defer upgrades, unlocks, economy, building systems, procedural terrain,
a level editor, full fluid simulation, and broader progression saves. Course personal bests
and portable run files already persist. Incremental boat physics follows the
separate physics plan rather than being a prerequisite for a complete game. Arcade wake jumps,
landing and capsize now exist in free boating; boost remains deferred. Multiplayer is now a
planning track for shared free boating, superseding its earlier deferral; elaborate networked
competition and changes to the offline course remain outside the first release.

## Questions to revisit through play

- How much sliding feels playful while still feeling like a boat?
- Does relaxed exploration or fast gate running become the stronger experience?
- Does the implemented Eriksberg-to-Älvsborgsbron area offer enough useful routes and landmarks?
- How much geographic fidelity contributes to the pleasure of being there?
- What follow-camera distance and angle best balance scenery and steering clarity?
- Can a few carefully shaped routes provide enough variety without more systems?

## Decision record

- 2026-10-18: Prioritize an inhabited harbor through two dressed berths. Build the moored
  fleet with shared parameterized boat recipes, laying a visual foundation for a possible
  later skiff-to-speedboat upgrade path. Keep gameplay progression and player handling changes
  separate from this first harbor step; preserve free exploration and the basin course.

- 2026-10-17: Prioritize learnable, predictable handling over exhaustive simulation. Adopt the
  [step-by-step physics roadmap](BOAT_PHYSICS_PLAN.md): reverse and docking, planing, buoyancy and
  gentle waves, currents and crossing wakes, then optional trim. Keep calm water, forgiving recovery,
  and stable camera behaviour. Break implementation into bounded tasks with tests and play gates.
- 2026-10-11: Adopt a small playable boating toy as the proposed successor direction for the demo,
  with handling first and an optional short arcade challenge.
- 2026-10-11: Make boating around Gothenburg and its surrounding archipelago a central aspiration.
  Keep the first prototype local and small, and preserve free exploration alongside timed play.
- 2026-10-11: Keep planing, refined speed-dependent steering, currents, and waves as future
  experiments. Implement wake and turning spray first to make the current movement easier to read.
- 2026-10-11: Replace direct heading control with steerable stern thrust, yaw inertia, and water
  resistance at bow/stern stations. Show the thrust direction through an outboard and propeller wash.
  Physical motivation: directed thrust in the [Coast Guard Boat Crew Handbook](https://rdept.cgaux.org/documents/BoatCrewHandbooks/BoatCrewBCH16114.4.pdf).
- 2026-10-13: Add nonlinear hull resistance and passive coasting steering from water crossing the
  angled outboard. Keep these forces dissipative, including their rotational work. The scripted
  preview now holds its wheel angle while lowering throttle to show an unpowered turning coast.
  Quadratic drag is motivated by the [standard fluid drag equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/drag-equation/);
  coefficients remain gameplay tuning parameters, not measured properties of a specific boat.

### Playable gentle waves (physics C1–C3)

`--waves` opts into three deterministic crossing waves in all locations on CPU/GPU.
Ambient waves remain opt-in, while player wakes now default on in free boating. Reset preserves
the launch options while restarting wave time and clearing player history.
Six damped hull supports add physical heave, pitch and roll, including the planing attitude;
attachments share that pose while the follow camera stays level. Foam follows local water height.
GPU wave normals distort restrained planar reflections; CPU uses subdued slope colors.
Use `--camera-distance 12 --camera-height 6` for a closer view, and add `--demo` for a scripted run.
Local water-relative normal resistance now couples wave encounters to horizontal motion and all
three angular rates. Faster crossings increase the load and bow spray; slowing down softens the
response. Foam follows the local surface slope as well as height. Flat mode retains C2 handling.
This remains a support-spring/normal-damping model, without displaced hull volumes, orbital flow,
ventilation, or capsize behavior. The user accepted Phase C after playtesting. Its default baseline
remains available through `--no-player-wake`; bounded arcade wake objects are now the free-boating
default (`--player-wake` selects legacy Phase D wakes), while currents/scripted sources remain opt-in. See
[the physics plan](BOAT_PHYSICS_PLAN.md#phase-c--buoyancy-and-gentle-waves).

The GPU's boat-level water pass now uses a clipped near patch and a much smaller flat
far surface with analytic wave shading. In Eriksberg, shading shifts gradually from
quieter dock reflections to more broken river water. This is visual exposure only;
physical waves and existing recordings are unchanged. The CPU renderer retains the full
wave reference. Geometry, comparisons, and verification are in the
[04f water-surface record](WATER_REVIEW.md#04f-implementation-boat-level-surface-and-bounded-geometry).

## Engine audio — implemented 2026-10-18

The current mix puts soft hull-water wash ahead of a subdued low engine. After user feedback
asking for a peaceful experience, a second Astra consultation recommended lower acoustic
firing frequency, darker engine filtering, and water driven by real hull motion:

- The default acoustic voice spans 750–4800 RPM with one pulse per revolution (12.5–80 Hz).
  This is a sound-design voice, not a literal cylinder count or a change to propulsion physics.
  Continuous irregular firing pulses remain, filtered through roughly 240–320 Hz stages.
  Engine gain and bright combustion texture are substantially reduced.
- Independent filtered noise supplies gentle hull wash: hull speed sets travel gain, planing
  adds a little brightness, and live `wave_spray` bow-entry load creates soft encounter swells.
  Wave intensity has about 60 ms attack and 300 ms release; travel smooths over 150 ms.
  Water quiets as the hull stops and remains audible while coasting at neutral thrust.
- The mix fades in/out on focus and replay pause (about 40/100 ms smoothing constants),
  with continuous filters and noise state. A DC blocker and soft limiter bound output.

`src/boat_audio.h` owns independently tunable engine settings for future higher-revving craft.
The default irregular six-event amplitude pattern is an artistic texture. Screenshots do not
open audio. Replays reproduce speed-driven wash and recorded thrust at normal acoustic pitch;
old recordings lack planing and wave-entry data, so their water sound has no wave-contact swells.
Ghosts are silent. Impact/gate sounds remain proposals.

User acceptance, 2026-10-18: “I like this a lot!” accepts the peaceful engine/water mix.
Keep this balance as the baseline. Prior bass/combustion versions are superseded.
The background loop added below complements this accepted mix.
Next: retain their balance during further boating work. Manual coverage of every control/replay scenario remains unverified.

Generate a repeatable 12-second idle/cruise/wave-swell/coast preview without an audio device:

```sh
./build/validate_boat_audio build/boat-peaceful-preview.wav
./build/wake_velocity --waves
```

Preview wave encounters use synthetic varying inputs every 20 ms to expose the swell envelopes;
the interactive study uses actual hull state. The validator checks finite/bounded samples,
DC, audible idle, stronger loaded output, and buffer-boundary independence with an 8500 RPM
variant. It separately checks silent resting water, wash during coasting without engine sound,
stronger wave wash, and pause fade. These do not establish perceived pleasantness.

### Historical verification before the pulse redesign

Verification on the development Mac, 2026-10-18: Debug configure/build passed without
compiler warnings. All 52 existing CTests passed: tests 1–38 in the sandbox, followed by
39–52 (plus their replay fixture) with desktop access after the sandbox stalled on the
first GPU window. Final procedural-voice changes were rebuilt before the desktop tests.
A desktop CPU `--demo --frames 720` runtime smoke run completed successfully, exercising
forward and reverse thrust. This confirms startup/stream lifecycle and run completion,
not perceived sound quality, manual control/replay listening, or user acceptance. No visual
effects changed, so no additional visual inspection was performed.

Bass revision verification (2026-10-18): Debug configure/build passed. A desktop CPU
`--demo --frames 720` smoke run completed through forward and reverse thrust.
All 52 CTests passed with desktop access, including the 14 graphics tests.
`git diff --check` passed.
The user subsequently found this revision too clean; it is superseded by the pulse redesign.

### Pulse redesign verification — 2026-10-18

Debug configure/build and the new device-free audio validator passed. The exported preview
measures peak 0.276, mean −0.000834, idle RMS 0.042, and full-thrust RMS 0.126 before stream
gain. A desktop CPU `--demo --frames 720` smoke run completed through forward and reverse
thrust. Astra reviewed the DSP implementation and found no material blockers; the fixed
six-event pattern and small post-limiter DC bias remain tuning limitations. All 53 CTests passed with desktop access, including the new audio validator and 14
graphics tests. `git diff --check` passed. No visual effects changed.
Human listening and user acceptance remain pending.

### Peaceful water mix verification — 2026-10-18

Debug configure/build and offline validation passed. Isolated engine RMS is 0.002 at idle and
0.013 under full thrust (peak 0.034); isolated water RMS is 0 at rest, 0.0161 coasting at
speed 10, and 0.0362 with sustained strong bow-entry input. These measurements are before
perceptual listening and do not imply user approval. Astra reviewed the implementation;
its suggested lower wave gain and varying-input preview were incorporated.
A desktop CPU `--waves --demo --frames 720` smoke run completed through forward/reverse
thrust with the water stream active. All 53 CTests passed with desktop access, including
14 graphics tests. The final preview/fallback adjustment was rebuilt and checked with the
offline validator during the suite run. `git diff --check` passed. No visual effects changed.

## Joyful background music — 2026-10-18

Implemented “A little boat” for `wake_velocity`: an original 32-bar, 84 BPM major-key
XM with rounded keys, a simple humming melody, warm bass, and no percussion.
The fixed arrangement repeats after 91.43 seconds at low volume, independent of
handling. Focus loss / replay pause pauses playback; unload precedes audio-device
shutdown. Screenshots do not start audio. The dedicated deterministic generator
reuses existing XM helpers without changing the starter or main-demo soundtrack.
See [music workflow](MUSIC.md#boating-background-loop) for the asset and command.

Current task: background-loop implementation and automated verification complete.
Verification on macOS, 2026-10-18: Debug configure/build passed without compiler
warnings. All 54 CTests passed across the CPU/device-free run (tests 1–39) and
an unsandboxed desktop run (tests 40–54, including all 14 GPU tests); the sandboxed
run was stopped when its first GPU window could not start. The final regenerated
XM's dedicated test passed again: correct fresh-load start, 48.58-second seek,
185 seconds of finite unclipped PCM, and exactly two completed loops. A 180-frame
CPU-rendered desktop `--waves --demo` smoke run exited successfully. This is runtime
smoke evidence, not a listening judgement. A local 100-second WAV preview includes
the first loop transition (`build/a-little-boat-preview.wav`, ignored build output).
User acceptance, 2026-10-18: “It's joyful! Let's commit” accepts the background music.
Next task: retain the accepted music and engine/water balance during further boating work.
Known limitation: exhaustive focus/replay listening remains unverified; automated
decoding cannot establish that.

## Selectable soundtracks — 2026-10-24

Implemented: native SETTINGS → SOUNDTRACK selects A LITTLE BOAT / ACID WAKE / OFF with
Left/Right, Enter or a click. The choice is saved as `soundtrack=` in `title.txt` and
passed to both single-player and joined-room sessions. Old or unknown settings keep the
original default. Direct sessions accept `--soundtrack a-little-boat|acid-wake|off`;
the browser launcher exposes the same choices before PLAY. Off keeps engine/water audio.

Acid Wake is an original 132 BPM, 64-bar D-minor techno loop with TB-303-style resonant
bass phrases, accents, slides, kick/clap/hats, a breakdown and return. The dedicated
`tools/generate_acid_music.py` writes only `assets/music/acid-wake.xm`, leaving accepted
music and starter outputs intact. Native and web bundles include it. The
[music workflow](MUSIC.md#acid-wake-and-soundtrack-selection) owns regeneration and editing.

Verification, 2026-10-24, Fedora 43 x86-64/GCC 15.3.1, Wayland-only builds:

- Release and Debug full builds passed. Each CTest run passed 88 of 89 active tests
  (29.07 s Release, 32.00 s Debug). The sole failure in both was the pre-existing
  `cube_render_checksum`: `e4b24d56dac4a0b3` versus the Ubuntu/GCC reference
  `a920a253d6c6aa0a`; no renderer or baseline changed. Thirteen retired field tests
  were disabled; GPU CTests and the Node.js-dependent check were not included.
- `acid_xm_is_valid` passed fresh-load start, a 48.58-second seek, 235 seconds of
  finite unclipped audio and two loops; peak 0.540010. Regeneration was byte-identical.
- An isolated scripted run through the real title menu selected Acid Wake and persisted
  it. Default/unknown settings and save/load of all three choices passed. Captures of
  the updated settings panel were inspected at 320x200. Harness/logs/captures remain
  ignored build outputs under `build/`.
- An agent-run 180-frame Acid Wake river session and a 120-frame Off session reported
  native `GLFW - Wayland`, audio initialization and clean shutdown. The former loaded
  the new XM; the latter created no music stream. Invalid soundtrack IDs were rejected
  before window startup. A local decoded preview is `build/acid-wake-preview.wav`.

After rebasing onto `main` at `1a79efcd359b`, the Release build and routine suite were
rerun: the same 88/89 active tests passed in 28.92 s, with only the unchanged checksum
failure. The native Wayland Acid Wake river check also passed again for 180 frames.

Current task: implementation and local automated/runtime verification complete.
Next task: listen to Acid Wake and play the selector in single player and a joined room,
then qualify browser and packaged macOS/Windows playback. Human musical acceptance,
physical menu input, connected audio and focus/replay listening are pending; successful
decoding and scripted input do not establish these. The browser changes have not been
compiled or played in this Fedora session (no Emscripten SDK installed).

## Play-testing time controls — 2026-10-28

Implemented in `playtest-time-controls`: `[` / `]` extend the existing replay speed mapping
to live solo play (¼×, ½×, 1×, 2×, 4×, 8×); backslash restores 1× in either context. Live and replay
rates are independent, start at 1×, and live rate survives boat resets. Camera zoom retains
`+` / `−`. The scene displays an amber multiplier while altered and PRACTICE for affected courses.

The application scales the capped real frame delta before accumulating fixed 1/120-second ticks.
Boat parameters, integrator step and protocol/replay revisions are unchanged. Water, traffic,
wakes, voyage and course clocks follow simulation time. Camera adjustment, menu, reset hold,
profiler and audio use real time; music and engine pitch are not time-stretched. Multiplayer
ignores local time controls. Help, Escape menu and focus loss retain solo pause behavior.

Any course attempt simulated at an altered rate is latched as practice, including before START.
It can become the last replay but cannot update bests or export files; returning to 1× alone does
not clear practice. Restart at 1× for an eligible run. A completed eligible run is not invalidated
by changing time after FINISH. Existing saved bests and pinned ghosts remain available.

Completed task: implementation, help/CLI and controls documentation, deterministic rate/physics
and practice-persistence checks. Automated verification, 2026-10-28 on macOS/Apple Silicon,
Debug: build passed; `ctest --preset full` passed all 231 enabled tests (13 retired cases disabled).
The first sandboxed quick run had four socket-access failures; the full run with network/graphics
access passed. The original `boat_time_control` check used 24 virtual ticks in the quick lane.
A temporary instrumented app used raylib key events in a 240-frame windowed basin/waves/course
run (`--no-vsync`) to check ¼×/4×, normal-time restore, ignored menu time keys and a real-time
Backspace reset clearing practice at 1×. Its captures at 4×, ¼× and after reset were inspected;
the time label was moved below course panels after the first capture exposed overlap. This is
automated windowed coverage, not human input or audio acceptance. The final production binary
also completed a windowed river/waves demo (`--location river --waves --demo --no-vsync
--frames 180`); no human audio assessment was performed.

Follow-up, 2026-10-28: the user reports 4× felt fine and requested 8×. The live and replay
controls now include 8× as the highest step; backslash still restores 1×. The deterministic
rate check now compares 32 virtual ticks at all six rates; the replay check includes 8× and
its upper clamp. Follow-up verification on macOS/Apple Silicon, Debug: build passed and
`ctest --preset full` passed all 231 enabled tests (13 retired cases disabled). The temporary
240-frame windowed raylib key-event check passed at 8×, including repeated faster input at
the limit, normal-time restore, ignored menu keys and practice reset; the 8× indicator capture
was inspected. The final production binary completed the 180-frame river/waves demo again.
User acceptance, 2026-10-28: after trying the 8× extension, the user reported “Works great!”
The time-control feature is play-tested and accepted; specific locations, input devices and
audio scenarios were not reported. Next task: integrate the PR after CI checks; additional
platform qualification remains separate. Known limits: fast-forward increases physics CPU
work; audio plays at its normal rate, and the existing stall clamp still discards real time
above 0.1 s per frame. Browser/Windows verification remains pending.
