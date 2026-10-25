# Boat physics: learnable handling

2026-10-24 current tuning: [landing weight L1](#l1-candidate--2026-10-24) advances handling to
revision 8 on top of [arcade stability and steering roll](ARCADE_STABILITY.md) reduces
the default object wake to 1.2 m, adds object-world recovery and lets steering command
air roll (revision 7). The user liked L1's impact feel once its camera dip was removed
(2026-10-25); acceptance of the stability candidate is still pending.

2026-10-24 next requested work: the user wants to work on boat physics, especially making
landings more satisfying so the boat's weight against the water is felt. The first
[landing weight candidate L1](#l1-candidate--2026-10-24) is implemented: landing speed scrub
(handling revision 8), splash and hull slap. The first version also dipped the camera; on
2026-10-25 it was removed for comparison and the user said: "Now I like the impact feel!"
L1 without the dip is the accepted landing feel.

2026-10-23: the user enjoys the current jumping, landing and capsizing. Preserve this
arcade handling baseline in the [multiplayer plan](MULTIPLAYER_PLAN.md), which owns shared
authority, prediction and fleet contact scheduling. A diagnostic network room exists since M3
(server-driven, no prediction); it reuses the fleet step and changes no handling. Multiplayer
M2 (2026-10-23) adds a simultaneous fleet contact solver used only by fleet worlds (the
`--wake-objects-crossing` rival and `boat_server`). Single-player handling, `boat_collide_pair`
arithmetic and replays were unchanged at that checkpoint, so `BOAT_PHYSICS_REVISION` stayed 6. Future
clock/contact changes must record their physical effects and compatibility revision here;
successful network tests will not substitute for human handling acceptance.

Status, 2026-10-22: A1–D3 implemented; the user-requested
[handling feel pass](#handling-feel-pass--2026-10-19) adds quicker countersteering and a speed view, accepted by the user on 2026-10-19. Phase C is accepted; Phase D handling and
player-wake motion are provisionally accepted by the user, with continued evaluation
over time. On 2026-10-20 the user requested longer-lived, coherent wakes for jumping and
expressed enthusiasm for crossing wakes. The [wake rewrite review and plan](WAKE_REWRITE_PLAN.md)
owns that follow-up: W1 measurements/prototype complete on 2026-10-21; the strip
prototype failed its physical gates; the isolated periodic heightfield fallback passes its
free-propagation gates. W2.1 now completes the isolated owned C field, ledger, deep snapshots and matched-accuracy
boundary/island proof. A native basin candidate now uses exact cosine evaluation, fixed-tick wetted stern forcing
and common queries/rendering behind `--wake-field`. At that checkpoint D3 remained default; the provisional
2 ms budget is not met. [Candidate evidence](WAKE_CANDIDATE_RESULTS.md) owns costs, limits
and continued play review. The user likes the revision-3 basin results as a foundation;
[performance and large-water scaling](WAKE_SCALING_RESULTS.md) led to an isolated compact
quadrature rejection. The user then selected a third, arcade-oriented alternative:
`--wake-objects` now provides bounded per-boat pieces, tick expiry, tall jump crests and
signed overlap, with an optional second craft. [Object results](WAKE_OBJECT_RESULTS.md)
own current commands, measured jumps/cost, automated verification and play feedback. The user enjoys the huge waves; the [continuity revision](WAKE_OBJECT_SMOOTHING.md)
now shortens source pieces and smooths strength/growth/fading while retaining the default
crest and lifetime. The user reports the revision is very fun and requests it as the
free-boating default. Objects now launch by default; `--player-wake` selects legacy D3.
The [arcade gameplay pass](WAKE_ARCADE_IMPLEMENTATION.md) now defines flight, landing and capsize,
height-aware obstacle contact and an interactive basin rival. It advances compatibility to
revision 5 while retaining the powered H3 steering references. Tight-turn crest tracking and
appearance work are recorded there; the exact user-reported stuck-wave case remains unconfirmed.
This supersedes physical-packet refinement as the active task.
The 2026-10-18 acceptance remains a historical baseline.
Current/scripted-wake options now work in all locations; player-path wakes are
enabled by default in free boating, with `--no-player-wake` available for calm practice.
The earlier T5 banking pass is recorded in the [turn-banking plan](BOAT_TURN_BANKING_PLAN.md). T1 supplies
baseline/load fixtures; T2 adds filtered lateral-load roll, planing dependence, and passive
large-angle recovery through the shared physical hull. H3 below introduced handling revision 4;
revision-3 and earlier best files remain preserved. T3 adds broader dynamics coverage and stronger passive
recovery beyond 25°. T4 adds shared deterministic capture/measurement inputs, attachment and
spray checks, and a physically driven banked course recording/seek fixture. T5 reduces
lingering turn-exit bank using lower planing gain and a shorter load filter; the ordinary
fast-turn fixture settles below 2° in 1.075 s (previously 1.517 s). Automated/scripted
verification is recorded there. The user reports somewhat better recovery but requests a
firmer, stern-led carving turn. [H3](#h3--stern-led-carving-feel-2026-10-20) implements that
named follow-up; the user accepted its current feel on 2026-10-20 ("This is way better.
I think we're good here."). The exhaustive hands-on checklist remains unverified. H3 deliberately
changes planing lateral grip while retaining the accepted command/thrust/drag/camera settings.
Windows CI follow-up remains tracked in the [Windows build guide](WINDOWS_BUILD.md).
Closer camera comparisons remain an optional design experiment; E1/E2 have not started.
Waterborne thrust/drag settings, uncommanded wheel return and the C3 wave recipe are preserved;
revision 5 explicitly disables water handling in dry flight and limits assistance at large angles;
H3 changes passive lateral grip as described below;
commanded wheel reversal is deliberately faster under the 2026-10-19 request.
The separate [04h harbor pass](HARBOR_BOATS.md) adds fixed berth collision envelopes and
kinematic moored-boat wave poses. [Travelling harbor vessels](HARBOR_TRAFFIC.md) add
player contacts using obstacle-relative velocity and visual foam wakes. Those harbor
passes remain kinematic and their foam adds no physical wake. The separate D1/D2
source below supplies the physical test.

Accepted direction: 2026-10-17. Parent design: [Boating playground](BOATING_PLAN.md).
Public controls: [README](../README.md#boating-sketch). [All plans](README.md).

## Landing weight request — 2026-10-24

**Requested by the user:** work on the boat physics next, with the landing as the main focus:
it should be more satisfying, and the weight of the boat against the water should be felt.

**Baseline measured (revision 7, 2026-10-24):** a scratch probe dropped a planing hull at
15 m/s onto gently waving basin water. Landings settled within about 0.15 s with peak heave
deceleration of 4–16 g (1–8 m drops) and no speed loss: thrust carried the boat from 15 to
about 16.2 m/s half a second later. Nothing reacted in presentation: no landing splash,
sound or camera response (the camera height ignores the hull's heave).

**Constraints carried forward:** keep the enjoyed jumping/capsizing baseline (2026-10-23) and
the pending revision-7 stability candidate in view; physics changes must bump
`BOAT_PHYSICS_REVISION`, keep prediction replays deterministic, and be judged by human play,
not by automated checks alone. The [wake plan](WAKE_REWRITE_PLAN.md) owns crest shapes and
the [race plan](RACE_PLAN.md) relies on current jump counts, so record any change to either.

### L1 candidate — 2026-10-24

**Physics (`src/boat.c`, revision 8).** While the motion state is `BOAT_LANDING` (the
0.35 s after a whole-hull flight regains water support), each wetted support plunging faster
than 2.5 m/s adds its squared excess entry speed to a plunge measure. Water-relative
horizontal velocity then decays by `exp(-0.14 × plunge × dt)`. Hard and bow-first landings
cost speed; small hops and ordinary wave bobbing stay free. Vertical support, damping and air
control are unchanged. No state, input or network field is added; `BOAT_PHYSICS_REVISION`
is 8, so revision-7 recordings, bests and peers do not mix.

**Presentation (cosmetic, outside physics).** `BoatPresentation` notices a new landing and
grades it 0..1 from the previous tick's fall speed (1.5 → 10.5 m/s). It throws spray sheets
along both sides plus a spreading foam ring. `boat_study` passes the strength and a landing serial to
the engine audio, which adds a dull 90 ms hull slap (filtered noise, roughly 80–450 Hz, no
tone) under a 350 ms splash wash. Replays reset presentation, so recorded playback shows no landing splash.

**Measured (scratch drop probe, 15 m/s, 8° bow-up unless noted; speed 0.5 s after touchdown,
revision 7 ≈ 16.2 m/s in every case):**

| Drop | Fall speed | Revision 8 speed | Loss vs revision 7 |
| --- | --- | --- | --- |
| 1 m | 3.3 m/s | 16.1 m/s | ~0% |
| 2 m | 5.5 m/s | 16.0 m/s | ~1.5% |
| 4 m | 8.3 m/s | 15.2 m/s (14.9 at 8° bow-down) | ~6.5% (8%) |
| 8 m | 12.2 m/s | 13.4 m/s | ~17% |

The first trial (no free entry speed, gain 0.14, then 0.08) scrubbed 18%/11% at 4 m and
slowed the race bots to 67.5–78 s with 11–18 jumps; it was replaced by the thresholded form.
On the race course (`validate_boat_race`), four bots now finish in 63.96–75.65 s with 7–12
jumps and no recoveries, against 63.06–70.57 s with 6–10 jumps at revision 7. The skill-course
preview's six landings grade 0.32–0.80. The hardest landing's audio peaks at 0.24
(engine alone 0.085; first 200 ms RMS 0.070 against 0.025) beneath the 0.75 soft limiter.

**Audio feedback, 2026-10-24:** the user disliked the first landing sound, a tonal 90→42 Hz
sine thump peaking at 0.36 (RMS 0.139): "Maybe it's too strong. I can't really explain it."
It was replaced by the noise slap above at about half the loudness, with the splash wash
lowered. Offline preview (planing engine, landings 0.3/0.6/1.0 at 1, 3 and 5 s):
`build/landing-slap-preview.wav`, rendered by a scratch program, not a project tool.

**Verification, 2026-10-24, Apple Silicon/macOS Debug:** project build without warnings;
all 118 active CTests passed with GPU checks; 15 retired wake-field checks stayed disabled.
CPU frames at 16.10/16.17/16.22/16.45 s and GPU frames at 16.22/16.45 s of
`boat_race_trial --skill-course` were inspected: the side sheets and foam ring render on both
renderers. The camera dip (then part of L1) was subtle in stills, so its amplitude was raised
from 0.8 to 1.2 m before the GPU captures. `boat_benchmark` wake-loop now reports `physics_revision` 8, 18
births and one landing (hash `82a88bf33eff247a`). Changed lines pass `git clang-format`.

A 900-frame windowed `boat_study --location river --demo` run exited 0, but its scripted
drive made no wake jumps, so it did not exercise a landing. The full suite was rerun on the
final tuning (0.14/2.5 m/s scrub, 1.2 m dip) and passed.

**Play feedback, 2026-10-25:** after the camera dip was removed, the user liked the impact
feel. Verification of that change (Apple Silicon/macOS Debug): build without warnings, all 118
active CTests passed.

**Not verified / next:** splash size and slap loudness were not singled out in that feedback. Race pacing grows slightly (above). Remote boats' splashes appear only where their
presentation is stepped; remote slaps are not played. Possible follow-ups depending on
feedback: restoring a camera dip (removed 2026-10-25, last tuned to 1.2 m peaking at 70 ms),
heavier heave/no-rebound settling, bow slap on nose-down entries, and landing
quality shown in the HUD.

## Experience to build toward

Leave Eriksberg carefully, open the throttle, feel the boat climb onto plane, ease off for a passing
wake, then come alongside a small island jetty. The handling should make that trip satisfying even
without missions or scoring. Island scenery is a separate project, not a physics prerequisite.

Feeling in control means understanding and anticipating the response, not eliminating inertia.
Prefer a believable, tunable game model over an engineering simulator.

- Calm water stays genuinely calm and remains available for practice and regression tests.
- Visible waves and their physical effects agree; avoid unexplained random impulses.
- Slowing down usually makes rough conditions easier to manage.
- Keep the camera steadier than the hull, with a stable horizon.
- Ordinary mistakes cost speed or another approach. The later arcade wake mode deliberately
  adds capsize after sustained inverted water contact, with an immediate R restart.
- Use posture, wake, spray, and eventually sound to explain changes in handling.
- Add controls only when they offer useful choices. Automatic trim precedes manual trim.
- Do not change the main demo, soundtrack timing, or integer-scaled presentation.

## Priorities and estimates

Subjective 1–5 ratings: realism is added believability; fun is potential for satisfying control;
difficulty includes integration and tuning, not just initial code. These are not delivery estimates.

| Feature | Realism | Fun | Difficulty |
| --- | --- | --- | --- |
| Reverse, neutral, engine response | 4 | 5 | 2 |
| Planing transition | 5 | 5 | 3 |
| Buoyancy, pitch, roll | 5 | 4 | 3 |
| Waves affecting handling | 5 | 5 | 4 |
| Spatial currents | 4 | 4 | 3 |
| Wind and gusts | 4 | 3 | 3 |
| Crossable wakes | 4 | 5 | 4 |
| Engine trim and weight distribution | 5 | 4 | 4 |
| Hull-aware collisions and forgiving fenders | 4 | 5 | 3 |
| Propeller ventilation, severe instability, capsizing | 5 | 2 | 5 |
| Fully coupled fluid simulation | 5, if calibrated | 2 | 5+ |

Wind, propulsion failures and full fluid simulation remain deferred. The later
[arcade gameplay pass](WAKE_ARCADE_IMPLEMENTATION.md) implements a deliberately simple capsize
outcome for the user-requested tall wakes.

## Working contract for implementation agents

Implement only the first unchecked task unless the user names another. Read AGENTS.md, this plan,
and the relevant current source before editing. No renderer rewrite, new physics dependency, map
expansion, or unrelated cleanup. If a task proves too large, split it here rather than expanding scope.

Likely entry points (verify against the current tree):

- `src/boat.c`, `src/boat.h`: fixed-step simulation, CPU boat rendering, shared GPU player snapshot.
- `src/boat_map.c`: navigable water and shoreline boundaries.
- `src/boat_gpu.c`, `src/renderer_gpu.c`, `src/waterfront_scene.h`: GPU integration.
- `tools/boat_study.c`: input, HUD, deterministic preview and capture options.
- `tools/validate_boat.c`: handling regression tests; `CMakeLists.txt`: test registration.
- `README.md`: public controls and launch commands.

Keep simulation deterministic at the existing 120 Hz. Express tuning constants in named groups.
Keep simulation state separate from presentation; CPU/GPU use the same physical pose and water
parameters. Do not silently replace existing steering or coasting behaviour. Flat-water, neutral,
and reset cases must stay well-defined. New passive resistance should dissipate energy relative to
the water; moving water and powered thrust can supply energy, so test these separately.

For each task: add targeted deterministic tests, build, run CTest, and update affected documentation.
For visual/input changes, capture representative frames and run the interactive demo, including
Eriksberg GPU+AA where applicable. Report checks that could not run. Mark a task complete only after
its acceptance checks pass; record files changed, tests, tuning choices, and remaining limitations.
Do not commit unless asked. Stop for player feedback at the play gates before adding complexity.

For turn banking, use the task IDs and implementation prompt in
[its owning plan](BOAT_TURN_BANKING_PLAN.md); that named follow-up precedes optional Phase E.
Otherwise use this reusable prompt for a smaller coding model:

> Read AGENTS.md and docs/BOAT_PHYSICS_PLAN.md. Implement only task [ID]. Preserve existing unrelated
> changes. Follow its scope, acceptance checks, and the working contract. Do not start later tasks.
> Update the task status and handoff notes, and report verification and limitations. Do not commit.

## H3 — Stern-led carving feel, 2026-10-20

Status: implemented and user-accepted; automated/scripted verification is recorded
in the [H3 handoff](#h3-handoff--2026-10-20). The user accepted the current carving feel
on 2026-10-20; exhaustive controls/platform checks remain separate. This named handling experiment precedes optional Phase E.
The user reports slightly better banking recovery but wants the sense of cutting through
water, with the engine initiating the action; they selected “More bow grip and a clearer
stern-led turn.” That initial T5 feedback preceded the H3 acceptance recorded below.

Scope: keep the existing outboard steering/slew and stern-applied thrust as the cause of
the turn. Increase lateral hull-station resistance smoothly with existing planing state,
using one named multiplier, so the forward hull holds an arc and the stern moves around it.
Keep displacement/reverse gain at its existing value when planing is zero. Balance the two
stations: exploratory bow-only emphasis increases yaw feedback and makes recovery linger.
No prescribed yaw/velocity/roll, extra delay, grip driven by wheel position, bank-to-grip
coupling, camera change, new controls, or audio/renderer redesign. Banking constants stay
at T5 values; measured forces continue driving the same physical roll.

Acceptance/verification: preserve first-tick engine-force causality, mirrored response,
rest/reset/straight travel, passive coasting energy and uniform-flow invariance. Measure
engine/yaw/bank onset, bow/stern lateral motion, slip, speed and turn-exit recovery at
120 Hz. Keep T5 recovery and sustained-bank bounds. Reconcile changed turning trajectory
references explicitly, bump handling compatibility to 4, preserve revision-3 files, record
and replay a current banked course, and rerun dynamics/environment/contacts plus CPU/GPU
attachment/capture and desktop playback checks. Capture early entry phases from shared
measurement inputs. Keep human feel acceptance separate from automated verification and
exhaustive wake/camera/focus checks.

## Handling feel pass — 2026-10-19

Scope: the user requested steering/handling review and more fast, fun play. This named follow-up
precedes optional Phase E. Research, the wider experience audit, and proposed next experiments
belong to the [boating design review](BOATING_PLAN.md#handling-and-speed-review--2026-10-19).

Implemented, automatically verified, and accepted by the user on 2026-10-19 ("Feels great!"):

- **H1 — Catch a turn promptly.** Opposite input unwinds the existing wheel setting at 3 units/s;
  after crossing centre it returns to the original 1 unit/s turn-in. Full lock reaches centre in
  about 0.33 s, instead of 1 s. The tick is split at centre so the faster rate cannot leak into
  the new turn. Fractional inputs retain their proportional rate. Normal turn-in, release spring,
  outboard slew, thrust/neutral/reverse, drag, lateral grip, yaw inertia, and waves keep their
  existing tuning. This changes player command response, not the passive hull model.
- **H2 — Bounded speed-view experiment.** Shared CPU/GPU camera widens from 55° to 62° vertical
  FOV over 4–18 units/s forward ground speed using a smoothstep curve. Docking/reverse retain
  55°. Height, distance, level horizon and heading lag keep their existing settings. `--fixed-fov`
  restores the constant lens and survives C/reset. The camera derives the lens from pose and
  velocity, including replay, with no state that needs to catch up after seeking.
- At this 2026-10-19 baseline, handling revision became 2 (superseded by banking revision 3).
  Its personal-best filenames included `physics2`; older files were preserved. Old exported recordings are rejected by the existing compatibility contract.

Deterministic comparison at 120 Hz: start at 12 units/s, establish one second of full steering,
then hold opposite steering. Powered yaw reversal improved from 0.992 s to 0.383 s; heading
overshoot fell from 40.0° to 16.8°. In neutral/coasting, reversal improved from 1.133 s to
0.542 s and overshoot from 16.2° to 8.1°. These are scripted basin measurements, not player
ratings or a guarantee for every speed/wave/contact situation. Build-local comparison probe
and pre-change executable are in `build/handling-review/`.

Acceptance checks: mirror left/right recovery at cruise and in neutral; no energy gain when
coasting under countersteering; fractional/clamped inputs and centre crossing; existing docking,
contact, planing, waves, currents and long-run checks; bounded/monotone lens with stable horizon
and unchanged docking/reverse lens; fixed-lens captures and compatible replay/best handling.

Verification on 2026-10-19, macOS Apple Silicon, Debug:

- Configure/build completed without project compiler warnings (raylib retains its existing
  macOS OpenGL deprecation configure notice). All 69 CTests passed across the full run and
  targeted rerun, including 18 GPU checks. The initial run exposed an overly exact float
  comparison in the new steering test; it passed after using a tolerance. No production tuning
  was changed to satisfy it. Existing environment, route, replay, hull and audio checks passed.
- CPU river before/after acceleration frames at 2.9 s were inspected. The new `--fixed-fov`
  river capture matches the pre-change capture byte for byte, preserving the baseline view and
  straight acceleration. GPU AA Eriksberg acceleration and GPU player-wake turning frames,
  plus CPU neutral/coast and reverse frames, were inspected at 320x200. Hull/spray attachments
  and horizon remain coherent; the river boat is still small at its distant default view.
- Desktop `--location eriksberg --gpu-aa --demo --frames 360` exited successfully with desktop
  graphics access. The sandboxed window stalled on macOS application services and was terminated
  before retrying. This is a scripted desktop smoke check, not hands-on keyboard or listening
  acceptance. Reference captures and the smoke log are under `build/handling-review/`.
- Changed documentation links, launch flags, and whitespace were checked. No Instruments run
  was needed for this small input/presentation change; no performance improvement is claimed.

User acceptance, 2026-10-19: "Feels great!" accepts this handling/speed-view pass.
The feedback does not specify individual scenarios, platforms, or fixed-FOV comparisons.
At that handoff, the next task was to retain this baseline and collect longer-term feedback.
The 2026-10-20 [turn-banking plan](BOAT_TURN_BANKING_PLAN.md) now owns the requested follow-up.
Repeatable play checklist for future evaluation: accelerate, tap small corrections, hold a sweeping turn, countersteer into an S,
coast through a turn, then reverse alongside a quay. Compare speed view with `--fixed-fov` in
the basin and river. Judge recoverability, boat readability at 320x200, speed sensation, and
camera comfort. Trim controls remain optional future work.

Known limitations: the lens follows instantaneous forward velocity; impacts or a large sideways
slide can close it rapidly. It has no camera occlusion solution. The river's 32/6 view already
makes the boat small. The lens change is an experiment, not a proven perceptual improvement.
The user accepted the overall feel; exhaustive keyboard, camera-comfort and platform coverage
is not established by that feedback.

## Phase A — Reverse and docking

- [x] **A1: Signed propulsion in the simulation.** Add a bounded reverse command and a small,
  deterministic engine-response state. Keep thrust applied at the stern; neutral removes powered
  thrust while existing coasting resistance remains. Do not add input/UI or docking geometry yet.
  Acceptance: forward and reverse impulses and steering torque have the expected signs; idle cannot
  turn a stationary boat; commands saturate; engine response and reset are deterministic; sustained
  reverse stays finite and bounded. Retain current forward-handling tests.
- [x] **A2: Controls and readable feedback.** Use Up/Down for held signed thrust, with neutral at
  zero and reverse below it. Show signed thrust and engine response separately from speed. Update
  the deterministic preview to demonstrate stopping and backing up. Acceptance: releasing input
  never changes direction; held Down passes smoothly through neutral before selecting reverse;
  reverse steering and wash match stern thrust; focus loss pauses; reset returns to neutral;
  CPU/GPU agree.
- [x] **A3: Hull-aware shoreline contact.** Replace or refine the circular clearance with a simple
  oriented hull footprint, reusing existing water boundaries. Use damped, forgiving contacts and
  contact torque; do not add damage or overhead-structure collisions. Acceptance: bow/stern clearance
  across several headings, glancing slides, bounded rebound, no energy gain against stationary
  walls, and no escape through shore or map edges at the tested maximum speed.

Play gate A: approach an existing quay, stop, reverse, and come alongside without fighting the
controls. Use the basin walls for repeatable practice. Tune before continuing.

## Phase B — Planing

- [x] **B1: Smooth handling transition.** Introduce a bounded planing state driven by forward
  water-relative speed, with smooth response and separate enter/exit behaviour if needed to avoid
  chatter. Blend deliberately tuned resistance/lift-response parameters; this is an approximation,
  not a claim of calibrated hydrodynamics. No waves or full vertical dynamics yet. Acceptance:
  no discontinuous acceleration, no rapid toggling near the transition, no reverse planing, neutral
  coasting does not create energy, and speed stays bounded. Preserve low-speed manoeuvrability.
- [x] **B2: Explain the transition visually.** Add bow rise followed by settling, and blend wake
  and spray with the planing state. Initially the vertical attitude may be presentation-only; keep
  that approximation explicit and isolated for C2 to replace. Apply the same pose to hull, cabin,
  engine, attachments, reflections, and shadows. Acceptance: CPU/GPU captures at displacement,
  transition, and cruise; no detached engine; slowing down returns smoothly to rest attitude;
  camera does not copy pitch/roll. Engine audio remains a separate optional task.

Play gate B: accelerating into open water feels rewarding and understandable, not like a hidden
gear shift. Confirm docking still works.

## Phase C — Buoyancy and gentle waves

- [x] **C1: Shared water query.** Add a small deterministic water API exposing height, surface
  orientation, and water velocity at a position and simulation time. Default to perfectly flat,
  still water. Add opt-in analytic waves and matching rendered surfaces for both renderers;
  do not apply it to the boat yet. Keep shared parameters/time explicit, not independent CPU/GPU
  tuning. Acceptance: known sample points, finite normals/velocities, flat-mode regression, and
  visual/physical crests aligned. Scope water rendering to the playable area if necessary and
  document approximations; do not redesign the entire waterfront renderer.
- [x] **C2: Floating hull.** Add vertical motion, pitch, roll, inertia, and several hull buoyancy
  samples with damping. Replace B2's presentation-only attitude with physical state; approximate
  planing lift must not be double-counted. Keep the existing horizontal/yaw model initially.
  Acceptance: stable rest, recovery from a small tilt/drop, no unforced growth of oscillations,
  finite long runs, reset, and correct engine/particle origins as the hull moves.
- [x] **C3: Playable gentle waves.** Couple local water-relative motion to resistance and tune
  encounter response by speed and heading. Use the gentle crossing-wave preset, not storms. Acceptance:
  repeatable head-on/beam/following encounters; reducing speed provides a manageable response;
  flat mode preserves calm handling; spray and wake remain attached to the water/hull correctly.
  Document simplified effects rather than adding destabilizing impulses to imitate realism.

Implemented C1 revision, accepted after visual feedback: three smaller crossing components,
subdued shading, and restrained GPU reflections replaced the single long swell and exaggerated
color bands. It was implemented together with C2 to assess the surface with a floating hull.

Play gate C: small waves are enjoyable to meet at different angles, and the camera remains calm.
Stronger waves and triangle-based buoyancy are optional upgrades only if this model proves limiting.

## Phase D — Currents and crossing wakes

- [x] **D1: Spatial current.** Add a smooth, opt-in current field through the water API. Evaluate
  hull/outboard resistance using local water-relative velocities, not ground speed; distinguish the
  two in debugging output. Start with an authored test channel, not claimed real Gothenburg currents.
  Acceptance: neutral drift, smooth field boundaries, uniform-current equivalence to moving through
  still water, deterministic routes, shoreline containment, and unchanged zero-current handling.
- [x] **D2: One crossable wake.** Add a deterministic moving wake source to the test area, with
  matching visible and physical waves and bounded lifetime/amplitude. A scripted source is enough;
  traffic AI and full fluid coupling are out of scope. Acceptance: repeatable crossing at several
  angles/speeds, clear approach cues, stable recovery, and no enduring energy growth after it passes.

- [x] **D3: Cross your own wake (user-requested follow-up, 2026-10-18).** Add
  `--player-wake` for free boating: retain a bounded travelled-path history, spread/fade
  physical crests long enough to circle back across, match visible geometry/normals and
  hull response, and preserve the current front spray. Keep overlapping loops bounded,
  reset deterministic, and calm/default/course behavior intact. Implementation and
  automated checks are complete; the user provisionally accepted Phase D handling and
  the revised wake motion on 2026-10-18, with continued evaluation over time.

Implementation: `--current` and `--crossing-wake` in all free-boating locations,
independently or combined with `--waves`. Zero initialization still means flat, still water. The current
API supports uniform flow or a compact polynomial channel with zero slope at its edges;
the authored preset peaks at (0.85, -0.25) units/s, centred on simulation X=0 with a 32 m
half-width. Axial drag/planing use centre water-relative speed; lateral hull stations and
the outboard sample their own local flow. Surface foam drifts with the flow, and spray
load and water audio use water-relative travel. The water-speed HUD readout was removed
in the 2026-10-19 full-window presentation pass. Ground velocity remains
the navigation/contact velocity. Material vertical water velocity includes current travel
along the local slope as well as the explicit time derivative.

In the basin, the scripted source moves from simulation (-24, -8) to (24, -8) at 4 units/s
during the first 12 seconds. Mapped locations translate it by (-140, -220), giving the
open-river route (-164, -228) to (-116, -228). Height, derivatives, source boxes, and GPU
uniforms share this translation. Both mapped routes have been checked for 3 m clearance.
The mapped current channel is centred on X=-12; its width/velocity recipe is unchanged.
Current streaks sample the channel near the boat and are masked to mapped water. It emits an outward-travelling compact wave packet: 0.38 m amplitude
bound, 6 m wavelength, 2.4 units/s spreading speed, 5 m packet half-width, and 12 s local
lifetime. Smooth sine-squared source/age windows and a cubic compact packet envelope
bound height and derivatives; the whole wake is exactly zero after 24 seconds. Analytic
height, normal, and velocity drive the existing six-support buoyancy/normal resistance.
The same packet is evaluated in CPU water geometry and GPU displacement/normal shading;
pale crests and the visible source provide approach cues. Source boxes are shared between
renderers; they are a non-contact scripted cue, with no navigation or fluid coupling.

```sh
./build/boat_study --current
./build/boat_study --crossing-wake --gpu-aa
./build/boat_study --current --crossing-wake --waves --gpu-aa --demo
./build/boat_study --renderer cpu --crossing-wake --time 9 --screenshot build/wake-crossing.png
```

Reset preserves the options and restarts time. With `--no-player-wake`, the test demo
waits six seconds, approaches at 65% thrust, and returns to neutral after ten seconds;
screenshot time uses that script. With player wakes enabled (the free-boating default),
the loop script below takes precedence. The flags reject course/replay/ghost modes,
preserving existing comparisons and formats. Measured harbor currents and physical wakes
from the existing traveling fleet remain future work. No ambient-wave preset or main-demo
arrangement was changed.

Verification record: see the handoff log below. The user provisionally accepted the
Phase D play gate on 2026-10-18, reporting that handling feels good while planning to
continue evaluating it over time. This is user acceptance, not a claim of exhaustive
manual coverage of every current/angle/speed combination.

### Player-path wake follow-up

The earlier D2 pass supplied one scripted source; the player's own foam still had zero
physical height. Following the user's basin feedback, D3 adds a separate `--player-wake`
option (all free-boating locations), now enabled by default. `--no-player-wake` opts out;
explicit `--player-wake` remains supported. Existing `--crossing-wake` is the separate
scripted-source test; combine it with the default player wake if desired. Courses/replays/
ghosts automatically retain their old water settings and reject explicit player-wake enabling.

The boat records a stern-offset path segment every 0.4 seconds when water-relative speed
exceeds 2 units/s. Each stored segment owns its centre, direction, support length, birth,
speed-based amplitude, and inverse travel speed. The 64-entry history is embedded in
`BoatWater`, so snapshots/copies do not alias another boat and reset discards all history.
Above 12 units/s, the source amplitude reaches a 0.65 m bound before packet weighting;
actual crest height is lower. Two outward packets per path segment spread at 1.6 units/s;
individual crests travel through them at 3 units/s with a 6 m wavelength. The original
1.15 units/s shared packet/phase speed was superseded after motion-readability feedback.
Packets rise smoothly over local ages 0.4–1.4 s and fade over ages
14–20 s. Longitudinal overlap joins the path; smooth normalization bounds densely looped
patches, including analytic spatial/time derivatives. No random impulses or extra buoyancy
forces are added: the existing water query and six-support response produce the encounter.

CPU tessellation, GPU displacement/normal/crest shading, foam attachment, and actor wave
poses all use that owned history. The shader receives live segments as two vec4 arrays;
expired segments are omitted. No new airborne emitter or bow-spray variation is introduced.
The existing near/far geometry representation remains; this is an authored wake approximation,
without wave reflection, shoreline propagation barriers, or full wave/current transport.
Mapped water topology still clips visible geometry and existing hull contacts still apply.

```sh
./build/boat_study --player-wake --gpu-aa
./build/boat_study --player-wake --gpu-aa --demo
./build/boat_study --player-wake --waves --current --gpu-aa --demo
./build/boat_study --renderer cpu --player-wake --time 9 --screenshot build/player-wake.png
```

The player-wake demo accelerates into a loop starting at 3.5 s, then returns to neutral
at 12 s; screenshot mode uses the same script. Manual driving uses normal controls.
R clears history while preserving the launch option. Free boating now defaults to player
wakes; `--no-player-wake` restores the earlier calm baseline when other water flags are absent.
The earlier wake acceptance is the baseline for the proposed
[coherent-wake rewrite](WAKE_REWRITE_PLAN.md), requested on 2026-10-20 after feedback about
early disappearance and fragmented fronts. W1 measured gaps/expiry and rejected its isolated
strip prototype; its periodic heightfield fallback passes the isolated propagation gates.
W2.1 completes the isolated owner and matched-accuracy domain/island proof. The subsequent
[basin candidate](WAKE_CANDIDATE_RESULTS.md) integrates an exact cosine backend and shared
queries/rendering behind `--wake-field`. It remains opt-in, misses the 2 ms target, and
awaits human play review. The D3 runtime above remains the default.
Turn-banking work is recorded separately in the
[turn-banking plan](BOAT_TURN_BANKING_PLAN.md).

Play gate D: currents reward anticipation and wakes reward choosing speed/angle; neither feels like
an invisible punishment. Keep a calm-water option and do not make exposure mandatory for exploration.

## Phase E — Optional trim and loading

- [ ] **E1: Automatic trim and loading experiments.** Only after phases A–D feel good, try a few
  fixed mass/centre-of-mass presets and bounded automatic trim. Acceptance: stable rest and planing
  across presets, visible but recoverable handling differences, and no new mandatory controls.
- [ ] **E2: Optional manual trim.** Add only if E1 exposes a genuinely enjoyable tradeoff between
  acceleration, cruise, and wave comfort. Keep automatic mode as the default and show the setting.
  Acceptance: manual control is understandable without specialist knowledge; returning to automatic
  is smooth. If playtesting finds no useful choice, explicitly defer this task.

## Reference approaches

These motivate models, not exact coefficients or a requirement to adopt another engine:

- [Hydrodynamic Lift on Boats](https://arxiv.org/abs/1808.03313): planing and hydrodynamic support.
- [Habrador boat resistance implementation](https://www.habrador.com/tutorials/unity-boat-tutorial/5-resistance-forces/):
  an example of decomposed buoyancy/resistance forces and a possible later triangle-based approach.

## Handoff log

- 2026-10-18: Windows CI exposed a brittle mapped reverse assertion: endpoint
  separation can be small after forward braking and successful backward travel.
  A local Release probe with a -0.00001-radian heading offset reproduced the old
  failure (2.27 units final separation despite 10.17 units backward travel).
  The validator now accumulates actual backward navigation displacement along
  the hull, retains the three-unit minimum and every-tick shoreline containment,
  and exercises both quay directions with zero and ±0.00001-radian offsets.
  No runtime physics or rendering changed. Local Release handling passed;
  full Debug suite and Windows rerun evidence belong in the
  [Windows build status](WINDOWS_BUILD.md#current-status-and-verification).
  The corrected Windows action passed at `3bffdb7`; manual Windows handling/audio
  remains pending. See the Windows guide for the run and next playback check.

- 2026-10-18: Default/availability follow-up requested by the user. All free-boating
  locations now enable player wakes at launch/reset. `--no-player-wake` opts out;
  explicit `--player-wake` remains compatible. Course/replay/ghost modes keep their
  existing settings and explicit opt-in rejection. Current/scripted-wake flags now accept
  Eriksberg and river as well as basin. The mapped source uses a translated, verified
  open-water corridor; current streaks are shoreline-masked and available in mapped
  snapshots and direct CPU rendering. No accepted wake speed/amplitude was retuned.
  Added CLI checks for both mapped profiles and byte-identical default/explicit player
  wake output. CPU checks verify translated field equivalence and 3 m source clearance
  through both maps; the production shader probe also checks a translated scripted source.
  `--no-player-wake --time 3` matches the earlier calm CPU PNG byte for byte.
  On macOS, Debug configure/build passed without compiler warnings (raylib retains
  its CMake OpenGL deprecation notice). All 69 CTests passed, including 18 GPU checks,
  the mapped profiles, default/explicit equivalence, and course/replay regressions.
  Inspected Eriksberg and river GPU+AA time-3 captures with current/scripted wake enabled
  and default player wakes; mapped CPU captures also passed. A 600-frame river desktop
  demo smoke run exited successfully. The HUD now identifies the default player-wake
  loop script. Artifacts/logs: `build/wakes-default/`. Changed documentation links and
  `git diff --check` passed. Scripted/mapped checks do not establish exhaustive manual
  handling or other-platform coverage.
  Longer-term user evaluation remains open; test currents are authored, not measurements,
  and the mapped one-shot source is in open river away from the dock spawn.


- 2026-10-18: Phase D handling provisionally accepted by the user: "It feels good. I will
  evaluate it over time but let's ok it for now." This closes the current D1–D3 play gate
  while retaining longer-term evaluation. The revised player-wake appearance/motion was
  already accepted. Preserve the current settings; optional Phase E remains unstarted.
  This acceptance supersedes the pending handling status in the implementation records
  below and does not claim exhaustive manual coverage. Documentation-only update;
  changed links and `git diff --check` passed, with no new build or audiovisual checks.


- 2026-10-18: D3 motion refinement after the user reported that player wakes still looked
  static. Previously the phase and compact packet both advanced at 1.15 units/s, leaving
  a steady crest inside each moving ribbon. The packet now advances at 1.6 units/s and
  the wave phase at 3 units/s, so crests travel through, emerge from, and dissolve within
  the slower packet. CPU and both GPU shader stages use the separate phases. Spatial
  gradients and vertical surface velocity include the different envelope/phase speeds;
  hull response follows the same travelling feature as the rendered crest.
  Added a propagation regression: the same positive crest advances 1.5 m in half a
  second, while its former location becomes a zero crossing. Existing finite-difference
  derivatives, full 64-slot shader parity, loop/recovery, and overlap bounds also passed
  the targeted environment check. Revised loop encounter height: 0.397 m; maximum hull
  height over the full acceleration/loop/coast run: 0.682 m. The original D3 figures in
  the entry below describe its superseded 1.15 units/s baseline.
  On macOS, Debug build and all 66 CTests passed, including 18 GPU checks. A 900-frame
  GPU+AA desktop player-wake demo smoke run exited successfully. Inspected native CPU
  and GPU+AA cruise/loop/coast frames and combined-current/ambient-wave samples from
  24 s sequences captured at 10 Hz. Wake-only maximum hull angle was 0.190 rad and
  recovered to negligible height/pitch/roll by 24 s; the combined profile stayed bounded
  (0.230 rad maximum), with ongoing ambient wave motion. This is scripted verification,
  not continuous human driving or artistic acceptance.
  Artifacts: `build/wake-motion/loop.mp4`, `combined.mp4`, per-frame PNGs and logs;
  `compare.mp4` places the original D3 baseline left and revised motion right under the
  same control script. Physics changes mean boat poses can differ. `review.c` and its
  executable retain the local capture driver. `git diff --check` and changed documentation
  links passed. The user accepted the revised player-wake appearance/motion on
  2026-10-18 ("Looks good!"). Preserve the 3 units/s phase and 1.6 units/s packet speeds
  as the accepted visual baseline. This does not establish acceptance of every Phase D
  current/angle/speed handling case. Next task: broader Phase D handling review.


- 2026-10-18: D3 player-path wake implemented after committing the D1/D2 baseline as
  `b3c79dd`. Extended the environment checks for analytic derivatives through curved and
  overlapping path segments, finite support/lifetime, dense-overlap height bounds, owned
  copies, capacity/reset, and no emission while drifting with uniform current. A repeatable
  free-boating loop returns through its own wake and then settles after coasting: measured
  local encounter height 0.381 m and maximum hull height 0.714 m over the full run,
  including acceleration/planing. Two identical runs match position/heave/roll every tick.
  The production GPU probe now compares combined player/scripted packet height and
  slopes at 165 samples, including actual vertex displacement. New CLI captures cover
  player-only CPU/GPU and player+current+ambient-wave GPU+AA; course use is rejected.
  On macOS, Debug configure/build passed without compiler warnings; CMake retains
  raylib's existing OpenGL deprecation notice. All 66 CTests passed, including 18 GPU
  checks. The environment CPU/GPU checks passed again after strengthening the probe
  to exercise all 64 packed history slots, with the local packets in the final entries.
  A `--player-wake --gpu-aa --demo --frames 900` desktop smoke run exited successfully.
  This is scripted playback; manual control/comfort acceptance remains open.
  `git diff --check` and changed documentation links passed.
  Representative native CPU and GPU+AA frames before/during/after recrossing were inspected.
  Local artifacts: `build/player-wake/loop.mp4`, `combined.mp4`, 10 Hz 24 s PNG sequences,
  logs, and `review.c`/its executable. The calm CPU time-3 capture is byte-identical to
  the pre-D3 baseline. Sampled mapped Eriksberg CPU and river GPU+AA time-3 captures
  also loaded and rendered correctly. The 24 s wake-only capture settled to negligible
  height/pitch/roll, with a maximum hull angle of 0.222 rad; the combined current/ambient
  wave run stayed bounded (0.329 rad maximum), with ongoing ambient motion as expected.
  Continuous human handling/artistic acceptance remains pending;
  no physical fleet wake, water transport solver, or saved wake replay was added.


- 2026-10-18: D1/D2 implemented as the authored basin profiles above. Added
  `tools/validate_boat_environment.c`: smooth compact field boundaries, uniform-current
  equivalence through steering, neutral drift, basin containment, bounded wake amplitude,
  finite-difference spatial/time derivatives (including current travel along slopes),
  deterministic repeat crossings at 0/3/9 units/s and 0/45/90-degree headings, and stable
  recovery after the source passes. A production-shader readback probe checks GPU vertex
  height and fragment height/slopes against the water API at 165 points/times. CLI tests
  exercise separate/combined presets and reject courses/mapped locations.
  On macOS, Debug configure/build completed without compiler warnings (CMake retains
  raylib's existing macOS OpenGL deprecation notice). All 62 CTests passed, including
  16 desktop GPU tests. A `--current --crossing-wake --waves --gpu-aa --demo --frames 900`
  desktop playback smoke run exited successfully. This is scripted playback, not human
  handling acceptance. `git diff --check` and changed documentation links passed.
  Calm-profile regression: 160 CPU frames are byte-identical to the pre-D stern-wake
  baseline across cruise, turn, coast, and reverse. Native 320x200 CPU frames and 10 Hz
  GPU+AA passage sequences are under `build/phase-d/`; reviewed wake-only and combined
  current/ambient-wave frames before/during/after crossing. `wake-passage.mp4` and
  `combined-passage.mp4` show 24 s of motion; `review.c` and its executable preserve the
  local capture driver. The wake-only script ended at height 1.7e-8 m and negligible
  pitch/roll at 24 s; its peak hull angle, including acceleration/planing, was 0.228 rad.
  The combined script retains the expected ongoing ambient-wave motion after the wake. Manual continuous handling/artistic acceptance is pending;
  the source is a bounded non-contact test cue and real harbor flow remains unimplemented.


Dated records describe each step at the time it landed. In particular, B2's cosmetic
`visual_pitch` was replaced by C2's physical support model, and the older B/C play-gate
notes are superseded by the C3 acceptance below. Test counts refer to those revisions.

- 2026-10-17: C3 implementation adds local normal resistance at the six supports in
  `src/boat.[ch]` and `src/boat_water.[ch]`. It replaces wave-mode vertical damping with a
  dissipative 3D normal force using point velocity (translation plus pitch/roll/yaw), local
  analytic normal, and water surface velocity. Generalized torques apply the same force to
  angular motion; there are no random impacts or additional spring/lift terms. Exit damping
  is capped by available support and dry stations exert no resistance. Total damping remains
  13 with at most 35% gain by 12 units/s. Flat mode retains the accepted C2 calculation.
  A smoothed bow-entry load (1.2 units/s scale, 8/s response) modulates spray and wake width;
  foam now aligns to the sampled surface normal through one shared CPU/GPU pose helper.
  Spray inherits the exact local attachment velocity. The wave preset, rendering, camera, controls,
  engine steering, and original horizontal drag/skeg terms are unchanged. Orbital flow/current,
  hull-pressure simulation, ventilation, and capsize behavior were outside this phase.
  The subsequent arcade pass adds capsize rules and water-contact handling gates.
  `tools/validate_boat.c` tests negative relative work and dry/exit limits separately from energy
  transfer by moving water; controlled 2/12-unit/s head-on, beam and following encounters;
  repeatability, lower slow-speed angular motion/vertical acceleration/spray load; particle
  surface alignment; shoreline containment; bounded neutral drift; and a recorded C2 calm-water
  forward/turn/reverse trace. Existing long-run and buoyancy recovery tests remain enabled.
  Debug configure/build and all 27 CTests passed, including GPU review and wave capture.
  Inspected close CPU basin/Eriksberg and Eriksberg GPU+AA captures at transition/cruise
  (`build/boat-c3-*.png`); both CPU and GPU+AA windowed demo smoke runs passed.
  Controlled 2-to-12-unit/s encounters measured angular-rate RMS rising from 0.155 to 0.303
  rad/s head-on, 0.145 to 0.261 beam-on, and 0.061 to 0.134 following. Vertical acceleration
  and bow-entry load also decrease at the lower tested speed for all three headings.
  These are tuning checks, not calibrated sea-state measurements. Play gate C accepted after the user reported that C3 feels and looks great.
  Preserve this baseline when starting Phase D.

- 2026-10-17: Revised C1 and completed C2 after the user approved improving the surface and
  adding buoyancy together. `boat_water.[ch]` now superposes three components: amplitude/wavelength/
  angular-speed 0.13/16/1.96, 0.06/9/2.62, 0.025/5.5/3.35, travelling along (0.6,-0.8),
  (-0.8,-0.6), (0.96,0.28). CPU shading is subdued; shared two-unit tessellation resolves the
  dominant waves (tested centroid height error <0.035). GPU uploads water topology once and uses
  the explicit snapshot parameters/time in `waterfront_water.vs/fs`; analytic fragment normals
  drive restrained reflection distortion, without the old independent shader ripples in wave mode.
  Planar reflection source and water shadows are restored. Flat/timeline shader behavior remains.
  `boat.[ch]` replaces `visual_pitch` with heave/pitch/roll and velocities. Six unilateral damped
  supports use gravity 9.81, total stiffness 64, damping 13, pitch/roll inertia 2/0.45 at 120 Hz.
  Dry stations provide no buoyancy/damping. The previous 8-degree transition/3-degree cruise trim
  now redistributes support, with at most 0.05 units of planing rise; no second attitude is added.
  The horizontal/yaw model remains unchanged. Attachments, reflections/shadows and spray follow
  the rigid hull pose; foam follows local wave height and spray expires at the water surface.
  Tests cover exact calm rest, dry drop/submerged and tilt recovery, bounded passive energy,
  slope response signs, rigid attachments through 16 headings, a level camera, repeatable
  encounters at 0/5/10 speed and four headings, a five-minute powered/reversing wave run, reset,
  and foam surface attachment. C3 still owns horizontal encounter resistance and comfort tuning.
  Debug configure/build and all 27 CTests passed, including a new GPU wave capture and the
  existing GPU review/material checks. Inspected close CPU/GPU+AA captures at rest, transition,
  and cruise (`build/boat-c2-*.png`, `build/boat-wave-gpu.png`). CPU basin and Eriksberg GPU+AA
  windowed demo smoke runs passed. Subjective comfort and enjoyment still need player feedback;
  CPU water has no scene reflections, GPU reflection projection remains planar, and mesh heights
  between vertices approximate the analytic surface.

- 2026-10-17: C1 complete following the user's request to start Phase C. Added
  `src/boat_water.[ch]`: explicit analytic height, unit normal, and vertical surface velocity
  at simulation X/Z and time; zero settings return exact flat/still water. The opt-in `--waves`
  preset is amplitude 0.22, wavelength 48, period 4 seconds, travelling along (0.6, -0.8).
  CPU and GPU consume the same tessellated surface and exaggerated slope colors. Eriksberg
  reuses imported water triangles, preserving shoreline coverage; the basin uses its rectangle.
  Longest-edge subdivision targets four-unit edges; tested centroid height error stays below
  0.012 units. Both paths reflect simulation X once. GPU keeps a dynamic vertex/color buffer;
  wave mode deliberately omits the old independent ripple/reflection/shadow water material.
  Flat mode retains the previous rendering and handling. Wave query velocity is surface motion,
  not an orbital-flow/current model; no wave forces or hull/foam following are applied until
  C2/C3, so intersections remain visible. Reset restarts time and preserves the CLI wave option.
  Updated `boat.[ch]`, `boat_gpu.[ch]`, the CPU boating scenery entry point, study CLI,
  CMake, README, boating notes, and the validator. Tests cover known crests/travel, normal and
  time derivatives, finite long sampling, mesh/query alignment at both locations, reset, and
  exact horizontal/attitude equivalence with waves selected. Debug build and all 26 CTests
  passed, including GPU review; inspected basin/Eriksberg CPU and Eriksberg GPU+AA captures
  (`build/boat-c1-*.png`) and completed short CPU/GPU+AA windowed demo smoke runs.
  C2 is next; player assessment of buoyancy and wave encounters waits for C2/C3.

- 2026-10-17: A1 complete. Added bounded reverse propulsion and signed, rate-limited
  `engine_response` in `src/boat.[ch]`; thrust and wash use the response at the stern.
  Before A2, keyboard throttle remained forward-only. `validate_boat` covers forward/reverse signs,
  saturation, deterministic response/reset, and a sustained reverse run. A2 adds player controls
  and feedback.
- 2026-10-17: A2 complete. Replaced the proposed selector with held signed thrust in `src/boat.[ch]`:
  Up/W moves toward forward and Down/S moves through neutral into reverse. The HUD shows signed
  thrust and engine response, while the deterministic preview demonstrates the transition.
  `validate_boat` covers the signed range, reversal, saturation, and reset; CPU/GPU continue to
  consume the same `Boat` pose through `boat_scene_frame`. Focus loss retains the existing
  fixed-tick pause.
- 2026-10-17: A3 complete. Replaced the centre-point clearance with six oriented bow, stern, and
  beam samples. `boat_map` returns the nearest shoreline normal; contact applies a damped impulse
  and the associated torque, then preserves the last safe pose. The same footprint contains basin
  walls. `validate_boat` covers a glancing rebound and all eight cardinal/intercardinal headings;
  Eriksberg runs retain shoreline and map-crop containment. Contacts remain a deliberately simple
  footprint, without quay geometry above the waterline or damage.
- 2026-10-17: B1 complete. Added a bounded, rate-limited `planing` blend in `src/boat.[ch]`, driven
  only by forward speed. Separate 6.5/5.0 unit-per-second enter/exit thresholds prevent chatter;
  the blend reduces the existing longitudinal resistance to 62% at full plane. This remains a
  horizontal handling approximation: no visual attitude or vertical lift has been added. The
  deterministic boat validator covers smooth entry, neutral energy decay, planing decay, and the
  reverse exclusion; README and boating notes describe the limitation.
- 2026-10-17: B2 complete. `src/boat.[ch]` adds isolated `visual_pitch` at 120 Hz: an
  8-degree transition hump plus a 3-degree cruise target, smoothed with a 5/s exponential response.
  No physical lift or horizontal forces were added. A shared pose helper rotates cabin/outboard
  offsets with the hull and composes local engine steering; CPU rendering and the GPU snapshot
  use it, so GPU reflection/shadow passes inherit the same pose. The camera stays independent.
  Planing blends wake spread/size/lifetime and adds symmetric spray; slip-driven turning spray
  remains. Foam stays on the water while spray originates at a hull attachment.
  `tools/boat_study.c` now holds straight cruise before turning at 3s and lowering thrust at 4s;
  README and boating notes document the visual approximation and revised preview.
  `tools/validate_boat.c` covers transition rise/cruise settling, smooth return to rest/reset,
  attachment alignment through 16 headings (including Euler singularities), planing spray,
  camera stability, and independence from horizontal handling. Debug configure/build and all
  24 CTests passed, including GPU review. Inspected CPU and Eriksberg GPU+AA captures at
  0.5/1.8/2.9s (`build/boat-b2-cpu-*.png` and `build/boat-b2-gpu-*.png`); CPU and Eriksberg GPU+AA
  windowed demo smoke runs also passed. Attitude remains cosmetic until C2; no engine audio.
  Play gate B still needs player feedback on acceleration and docking before starting C1.
- 2026-10-17: Plan saved. Control recommendations and coefficients remain proposals until their
  respective implementation step. Each play gate needs user feedback before starting the next phase.

## Windows CI impact-bound follow-up — 2026-10-19

Implemented: corrected the long-run basin validator after Windows run 39273971121
failed the 4 rad/s yaw assertion. A local 41-heading probe (±0.000020 radians in
0.000001-radian steps) reproduced brief wall-impact yaw above 4; the +0.000010 case
peaked at 5.06927 rad/s, and the -0.000008 case at 5.65856 rad/s. Collision-sensitive
trajectories can differ with floating-point rounding; this does not identify the exact
MSVC trajectory because the CI log reports only the failed assertion.

The stress test now checks combined energy against the former combined limits:
`0.5 * 23² + 1.1 * 4² = 282.1` (normalized mass 1, yaw inertia 2.2).
This allows conversion between translation and rotation while retaining a finite energy
budget, finite-state checks, the 23-unit/s speed limit, and basin bounds. It runs headings
-0.000010, zero, and +0.000010 on every platform and reports heading/tick/yaw/speed/energy
if the energy bound fails. A separate 19-unit/s unpowered beam impact exceeds 4 rad/s
while checking non-increasing energy and hull containment on every tick. Production
handling, replay compatibility, and user acceptance remain as recorded above.

Verification on 2026-10-19, Apple Silicon/macOS: Debug configure/build completed;
Release configure/build and the three Windows-selected CPU checks passed locally.
All 69 Debug CTests passed across split runs: tests 1–50 passed before the sandbox
stalled on graphics startup; all 18 GPU tests then passed with desktop access (along
with their replay fixture), and the remaining boating XM test passed separately.
No new audiovisual review was performed for this validator/documentation change. The existing raylib OpenGL deprecation configure
notice and Release stb_vorbis warning remain dependency warnings.

Next: verify the Windows workflow after this correction is pushed. Known limitations:
Windows/MSVC execution is not available locally; the precise failing trajectory is not
in the supplied CI log. Human Windows GPU/audio/control review remains outstanding.

### H3 handoff — 2026-10-20

Starting revision: `24aaa27`, clean worktree, Apple Silicon/macOS. The user reported that
T5 was a little better, but wanted the sense of cutting through water with the engine
initiating the action. They clarified the intended direction as “More bow grip and a
clearer stern-led turn.” This authorizes a named handling follow-up, distinct from T1–T5's
banking-only scope; no foundations, trim/loading or harbor work was advanced. The standing
instruction to commit each step remains in effect. The user subsequently accepted the
current feel: "This is way better. I think we're good here." This accepts the current
banking/carving response; it does not establish that every location/focus/platform checklist
item was performed.

One production handling coefficient was added: **`PLANING_LATERAL_GRIP_MULTIPLIER = 3`**.
Each existing lateral hull-station grip is multiplied by `lerp(1, 3, planing)` after the
normal planing update. Both stations use equal scaling, with their existing ±1.2 arms.
A higher bow-only weighting was explored locally: it moved the force centre forward,
increased yaw feedback and prolonged banking recovery. Balanced stronger grip reduces
bow cross-flow while retaining an outward-moving stern, without that extra yaw coupling.
These are game handling measurements, not calibrated hydrodynamics.

The outboard still follows the same wheel/slew and applies thrust at the same stern arm;
its skeg still resists local flow. There is no imposed heading/velocity, artificial engine-
first delay, visual roll, or bank-to-grip feedback. All T5 roll gains/filter, buoyancy/guard,
engine/throttle, longitudinal drag, command response, camera and audio settings are unchanged.
Displacement/reverse grip is unchanged when planing is zero; stale planing can briefly retain
extra lateral resistance while the existing state decays. The six unilateral vertical
supports and wave-normal resistance remain unchanged. Each horizontal station still obeys
`force * local_cross_flow <= 0`, so the added resistance is passive relative to local water.

A new fast CTest, `boat_turn_entry`, independently verifies first-tick stern thrust/skeg
force and yaw torque, mirrored trajectories, continuous roll, phase ordering and passive
coasting from mixed lateral/yaw motion at three initial planing states. Its shared fixture
starts at forward speed 12, full response/throttle/plane, holds progressive steering for
one second, then releases for three. It never approaches a basin wall. Results:

| Measurement | T5 baseline | H3 |
| --- | --- | --- |
| Engine angle at 1 s | 31.512657° | 31.512657° |
| Heading change at 1 s | 36.430260° | 27.916134° |
| Yaw rate at 1 s | 74.305023°/s | 56.806902°/s |
| Sideslip at 1 s | -19.850861° | -8.670749° |
| Forward-station cross-flow at 1 s | -3.745516 | -1.129083 |
| Stern cross-flow at 1 s | -7.376744 | -3.905192 |
| Speed at 1 s | 15.613008 | 15.381422 |
| Bank at 1 s | 9.494347° | 10.078505° |

Forward-station sideways motion drops **69.9%** while stern cross-flow remains 3.46 times
larger. The motor crosses 1° at .033333 s, heading at .241667 s, and inward bank at .500000 s.
This is a measured response sequence, not a simulated waiting period. Peak slip is 8.764486°
through the four-second fixture. Engine movement is identical to T5; firmer water resistance
reduces spin/skid and changes the hull's response. The new validator requires <9.5° slip at
one second, bow cross-flow <35% of stern cross-flow, engine-before-heading-before-inward-bank
ordering, physical momentum and passive energy loss rather than only snapshot equality.

T5's sustained-bank and below-2° recovery bounds are retained unchanged. The .25/.55/1
steering-release fixtures now sustain 9.779864°/11.653434°/11.677377° and return below 2°
at 1.091667/1.316667/1.458333 s. This is a two-tick increase in the first two cases and no
change in the hard case; angular settling also remains within the T5 bounds. Yaw decays
more gradually under the still-turned outboard (.733/.975/1.158 s below .05 rad/s), with
less hull sideslip. Existing coasting, forward/neutral/reverse, countersteer, flow invariance,
planing, energy and contact checks pass without weaker physical bounds. The stale-planing
reverse tow peaks at 2.019243° (still below its unchanged 3° bound).

The old countersteer capture fixture from `(0,40)` reached the basin margin under the new
arc. Its shared initial position is now `(0,0)` for measurement and capture, keeping the
same inputs/duration and all contact-free assertions. There is no in-drive recenter/reset.
The ordinary calm-reference straight sample is unchanged; all ten fields of its turn and
later coast/brake samples are checked against explicitly measured H3 trajectories. Its
turn attitude is height .0500000007, pitch .053120777, roll .211947575, load 4.68448925.
The earlier special-case skip of turning attitude is removed. The trajectory changes are
intentional here; the old accepted handling records above remain dated history.

The banked S-course test driver's prior heading controller orbited a missed gate after the
grip change. Its input-only controller now uses heading gain 3, yaw braking .7, full wheel
limits and throttle .25/.45 across an error threshold .4 rad. This is test-driver routing,
not a change to runtime player input or the course. All four real swept gates are crossed,
with 8.184977° peak bank, and the normal recording/save/reload/seek/ghost checks pass.
Handling compatibility advances **3 → 4** because horizontal trajectories changed. Current
best paths use `physics4`; revision-3 files are rejected without alteration. Replay text
layout/version remains unchanged. No actual personal-best directory was used in this review.

The 138,120-row banking/environment report and the 480-row entry report each reproduce
byte for byte. Standard beam/head/following wave circles peak at 20.423410°/19.613256°/
16.575899° (head drive 360 s, others 120 s). The scripted-wake circle now peaks at
27.380024°/77.843719°/s, up from T5's 11.217952°/9.446991°/s: its changed path produces a
stronger encounter. The own-wake circle peaks at 28.954529°/119.465973°/s, versus T5's
28.775251°/107.906357°/s. Its late peak is 8.525308°, so the early encounter does not grow
into sustained oscillation. The repeated-transition fixture peaks at 28.331104° and
139.676697°/s; the 300-second drive peaks at 28.331104°. All unchanged 30°/footprint/height/
pitch/recovery checks pass. Firmer grip changes wave/wake encounter history and turn radius;
strong wake transients, camera comfort and subjective carving feel remain review limits.
No claim of identical turning paths, universal stability or six-axis energy conservation is made.

Reproduction commands, from the repository root:

```sh
mkdir -p build/turn-entry-review
./build/validate_boat_turn_entry --report build/turn-entry-review/h3-entry.csv
./build/validate_boat_banking --straightening
./build/validate_boat_banking --report build/turn-entry-review/h3-banking.csv \
  > build/turn-entry-review/h3-banking.txt
./build/boat_banking_review --output build/turn-entry-review/h3-cpu
./build/boat_banking_review --gpu --output build/turn-entry-review/h3-gpu
./build/validate_boat_replay build/turn-entry-review/replays
./build/boat_study --demo --location basin --no-player-wake --gpu-aa --waves --frames 900
./build/boat_study --demo --location eriksberg --gpu-aa --waves --frames 900
./build/boat_study --demo --location river --no-player-wake --gpu-aa --waves --frames 900
./build/boat_study --replay build/turn-entry-review/replays/banked.run --gpu-aa --frames 240
```

Each capture directory contains 57 native 320x200 images: the existing 43 banking/release
views and seven new entry phases (0/.1/.3/.5/1/1.5/2 s) in default and close side/rear views.
Measurement and capture share `boat_turn_entry_reset/step` in the existing fixture header.
Native CPU/GPU .1/.3/.5/1-second side pairs, one-second default, wave-peak rear and
full-wheel 1.5-second exit pairs were inspected. Motor attachment/steering precedes the
hull arc/lean coherently; waterline, spray and GPU shadow/reflection presentation remain
coherent with the shared pose and upright camera. All four desktop commands above exited
successfully, including default player wakes in Eriksberg and the current banked replay.
This is scripted desktop motion/rendering/asset-loading evidence, not human input/focus,
subjective audio/camera comfort or carving acceptance. No Instruments run was warranted
by an observed performance problem; no performance improvement is claimed.

Verification, 2026-10-20: Debug configure and full build passed without project compiler
warnings; raylib retains the macOS OpenGL deprecation configure notice.
`PROTEUS_GPU_TESTS=ON` was preserved. Full desktop CTest passed **74/74**, including
**19 GPU-labelled tests**, in **172.69 s** (`build/turn-entry-review/ctest-h3-desktop.log`).
The new fast entry test, full dynamics/environment checks, current replay/old-revision
preservation checks and refreshed CPU/GPU capture fixture all pass. Report pairs match
byte for byte, formatting and `git diff --check` pass, and changed Markdown local file and
heading-anchor links were checked. Logs, reports, exploratory comparisons, temporary
replay/best storage and captures remain ignored under `build/`.

User acceptance, 2026-10-20: "This is way better. I think we're good here." The current
banking/carving feel is accepted and no further handling tuning is queued. The exhaustive
hands-on location/reset/replay/focus/camera checklist and Windows/browser execution remain
unverified; acceptance is not evidence that those checks occurred. Optional Phase E remains
unstarted and is not automatically authorized by closing this feel pass.
