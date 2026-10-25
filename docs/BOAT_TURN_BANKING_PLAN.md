# Turn banking for the playable boat

Status, 2026-10-20: **T1–T5 implemented: load-driven roll, dynamics/presentation/replay fixtures and quicker turn-exit recovery; banking baseline revision 3**.
T5 tuning and automated/scripted verification are implemented. The subsequent H3 carving
pass and combined banking/carving feel were accepted by the user on 2026-10-20: "This is
way better. I think we're good here." No further tuning is queued. The exhaustive hands-on
controls/replay/focus/camera checklist remains unverified.
The subsequent [H3 carving pass](BOAT_PHYSICS_PLAN.md#h3--stern-led-carving-feel-2026-10-20)
responds to the user's request for more bow grip and a clearer stern-led turn. It changes
planing hull grip and introduced handling revision 4. The later
[arcade gameplay pass](WAKE_ARCADE_IMPLEMENTATION.md) advances compatibility to revision 5; T1–T5's dated
revision-3 evidence below remains historical. H3 acceptance and remaining verification
limits belong to the physics plan.

The [physics plan](BOAT_PHYSICS_PLAN.md) owns the overall handling track; this document
owns the banking specification and task evidence. Optional trim/loading remains separate.

## Outcome

The boat leans into an established forward turn, more strongly as lateral load grows.
Fast straight running stays level. A fast sweeping turn banks less than a fast tight turn.
The hull takes time to lean, carries roll momentum through countersteering, and settles
when the turn ends. Coasting still banks while water forces turn the boat. Docking and
reverse show restrained motion. Waves and banking act on the same physical hull; the
camera keeps its level horizon.

This is a tunable approximation for the current small planing motorboat. It is not a
universal rule for all vessels. Published model experiments report inward heel increasing
with steady turning speed for the studied waterjet planing boat. That supports the chosen
direction, but does not calibrate this outboard boat or its numerical coefficients.
[Research abstract: Turning characteristics of waterjet propelled planing boat at semi-planing speeds](https://www.sciencedirect.com/science/article/pii/S0029801817304079).
The abstract was available through search on 2026-10-20; full-text access was unavailable.

Ambition here means load-driven response, inertia, planing dependence, wave interaction,
reproducible measurements, and coherent presentation. New boat classes, a hull selector,
capsizing, passenger loading, propeller ventilation, collision-induced rolling, and a full
six-degree-of-freedom solver are outside T1–T5. Harbor traffic and moored boats remain
kinematic. Their future banking would need a separate specification.

## Existing implementation and constraints

Source audit at `7d5fd48`, with a clean starting worktree:

| Location | Relevant behavior / required integration |
| --- | --- |
| `src/boat.c`, `boat_step` | 120 Hz. Computes water-relative propulsion, longitudinal drag, two lateral hull-station forces, and skeg force; applies horizontal/yaw motion, contacts, then buoyancy. Capture loads before contacts. |
| `src/boat.c`, `buoyancy_step` | Six unilateral supports already integrate heave, pitch, roll and their velocities. Add one applied roll moment here, before integration. |
| `src/boat.c`, `hull_rotation`, `hull_motion`, `boat_player_pose` | Shared rolled pose and attachment velocities already exist. Rendering reflects simulation X. Test physical sides, not Euler-component guesses. |
| `src/boat.h` | Owns `roll`, `roll_velocity`, `planing`; add only the filtered turn-load state if using the specified filter. |
| `src/boat_replay.[ch]` | Samples already store/interpolate roll. Playback samples poses, not resumable physics state. Physics revision currently 2. |
| `tools/validate_boat.c` | Existing buoyancy/energy, countersteer, calm-reference, contact, planing and wave checks must be reconciled explicitly. |
| `tools/validate_boat_environment.c`, `tools/validate_boat_replay.c`, `tools/validate_boat_profile.c` | Environment bounds, playback/storage, and instrumentation independence also consume the changed state. |
| `tools/wake_velocity.c`, `tools/boat_water_review.c`, `CMakeLists.txt` | Existing preview, route-review and test registration patterns. Prefer a small dedicated banking validator/review fixture over expanding runtime UI. |

Within T1–T5, use C17/raylib and the existing tick rate. Preserve steering response, propulsion, drag,
grip, yaw inertia, camera settings, course rules, controls and audio. Do not introduce
render-time physics or a second cosmetic roll angle. The foundations migration is an
independent track, not a prerequisite. If it lands first, follow its moved owners/APIs and
integer tick authority; do not duplicate the old implementation.

## Fixed model decisions

### 1. Measure the force that actually changes sideways motion

At the existing pre-integration force evaluation, keep these signed scalar loads in
simulation coordinates:

```text
forward = (sin(heading), -cos(heading))
right   = (cos(heading),  sin(heading))
u       = dot(velocity - current_at_center, forward)
F_side  = dot(propulsion_force, right)
          + sum(existing_hull_station_lateral_forces)
          + dot(existing_skeg_force, right)
a_side  = F_side / mass                  # current normalized mass is 1
```

Reuse the forces already evaluated; preserve their horizontal accumulation order.
Longitudinal drag is excluded explicitly. Do not use wheel position as the load, estimate
acceleration from post-contact velocity differences, or add the water-normal resistance
again: `buoyancy_step` already gives that resistance a roll moment.

For a steady turn with little sideslip, the diagnostic approximation is
`a_side ≈ u * yaw_velocity ≈ speed² / radius`. This explains speed/radius dependence; it
is **not** a second force to add. During turn entry, sideways skid, reverse, or countersteer,
the force and this approximation can disagree. The measured force wins. Brief outward
response at entry is allowed when the stern initially pushes outward; a sustained forward
turn must settle inward. Do not force the sign to follow the steering input.

Currents are already sampled at the hull stations and stern. Keep those samples, including
spatial differences. A craft drifting with uniform flow has no hydrodynamic banking load;
side flow resisting a stationary hull can create a small load. No extra ground-speed
multiplier is allowed.

### 2. Convert lateral load into a bounded applied roll moment

Use a small named tuning group in `boat.c`, not a new generic hull-profile framework.
Values below include the T5 tuning pass; they are game handling choices, not experimental
hydrodynamic data. The dated handoffs retain earlier coefficients.

```text
q           = clamp((u - 1.0) / (6.0 - 1.0), 0, 1)
forward_mix = q*q*(3 - 2*q)
gain        = lerp(0.12, 0.28, planing * forward_mix)
load_target = gain * a_side
load       += (load_target - load) * (1 - exp(-dt / 0.06))
phi_scale   = radians(12)
phi_equiv   = phi_scale * tanh((load / gravity) / phi_scale)
K_roll      = sum((BUOYANCY_STIFFNESS / 6) * support_across²)
            = BUOYANCY_STIFFNESS * 0.65²   # 27.04 in the current normalized model
M_turn      = K_roll * phi_equiv
```

`load` is the sole new persistent float, e.g. `Boat.turn_roll_load`, in acceleration units.
The short filter represents unresolved hydrodynamic response and suppresses abrupt force
changes; the existing physical inertia/damping supplies the hull's principal settling.
Zero it on reset. Update it exactly once per tick, including neutral/reverse; it must decay
when the force vanishes. `planing` is the already updated, smoothed state.

The positive sign is intentional: at heading zero, simulation starboard maps to rendered
local **negative X**. Positive `roll` about local Z lowers that side, so positive lateral
force produces inward roll for an established right turn. Verify through transformed
points at headings 0, ±π/2, and near ±π before using this sign.

`phi_equiv` is a bounded demand used to size a moment, not an angle written into `Boat`.
Existing buoyancy supplies righting and damping. Near upright its restoring moment is
approximately `-K_roll * roll`. Nonlinear support unloading means actual equilibrium
need not equal `phi_equiv`. The initial moment limit is about 5.66 normalized torque units,
below the upright single-side support righting capacity `gravity * 0.65 ≈ 6.38`.
This is an initial stability rationale, not a proof for waves, pitch, or all roll angles.
Do not raise the limit for stronger visuals without repeating the support/stability checks.

Add `M_turn` once to `roll_torque` in `buoyancy_step` and retain semi-implicit integration
through `roll_velocity` then `roll`. Pass the computed moment explicitly into the helper.
Do not alter support heights, gravity, pitch trim or roll damping to manufacture the bank.

### 3. Keep combined wave and turn motion recoverable

Ordinary banking is bounded by the moment above, not by clamping the final roll angle.
Waves retain their existing forces. Use a passive large-angle recovery term only beyond
25° total roll, leaving ordinary banking and small-wave response untouched:

```text
excess = max(abs(roll) - radians(25), 0)
q      = clamp(excess / radians(5), 0, 1)
M_guard = -180 * excess * sign(roll)
          -12 * q*q*(3 - 2*q) * roll_velocity
```

Add it to the same torque sum. The spring is continuous at engagement; damping always
opposes angular velocity. Its potential is `0.5 * 180 * excess²` for energy tests.
T3 increased spring/damping from the initial 60/4 to 180/12 after measured
wave/own-wake overshoot; engagement and blend angles remain 25°/5°.
This is a deliberate game stability aid, not calibrated hull stability. It is not a
hard cap: stress tests must measure overshoot and recoverability. Never silently clamp
roll velocity, erase roll at contacts, or reset the boat to pass a test.

The new moment uses a pre-contact force snapshot, never an impact impulse. A one-tick stale
load followed by filtered decay after impact is acceptable; sustained impact-driven roll
or growing oscillations are not. Log representative quay and traffic impacts.

This is one-way coupling from horizontal handling to roll. It does not debit horizontal
energy for roll work, so it cannot claim conservation of total six-axis energy. Calm-water
horizontal coasting must still dissipate energy; unforced floating recovery must still
dissipate energy. Turning in waves may change the trajectory because the existing water
normal forces already use the full pose. Measure and report that consequence.

### 4. Presentation, replay and compatibility

Hull, cabin, engine, spray births, shadows, reflections and ghost use the existing shared
pose. Keep floating foam on the water. Audit emitter positions and velocities for assumptions
that became visible with larger roll; correct concrete attachment errors without redesigning
spray or audio. The chase camera stays upright and retains the accepted speed FOV.

Bump `BOAT_PHYSICS_REVISION` once when the new model becomes active (2 → 3 if still current).
Use the repository's existing revision-derived best paths and rejection rules; preserve old
files. Do not bump `REPLAY_VERSION` unless the serialized layout changes. No layout change
is needed: roll is already recorded. A sampled replay/ghost must not run the turn-load
filter or physical simulation to reconstruct its appearance. Do not add a state field to
the file merely because live simulation gained one. If resumable simulation is introduced
by another change, revisit that contract explicitly.

## Tasks for the implementation model

Implement one named task at a time unless the user requests a range. Each task must end
with a compiling tree, relevant checks, and a dated handoff here. No commit or push unless
requested. Never advance the foundations, trim/loading, or harbor tracks as part of this work.

| Task | Deliverable | Depends on | Status |
| --- | --- | --- | --- |
| T1 | Baseline traces and coordinate/load fixtures | — | Complete (2026-10-20; evidence below) |
| T2 | Load-driven roll moment, filter, recovery term and compatibility revision | T1 | Complete (2026-10-20; evidence below) |
| T3 | Full dynamics/environment regression coverage | T2 | Complete (2026-10-20; evidence below) |
| T4 | Presentation/replay review fixtures and documentation | T3 | Complete (2026-10-20; evidence below) |
| T5 | Measured tuning, playback and user feel review | T4 | Tuning/automated and scripted verification implemented; combined H3 feel accepted (2026-10-20); exhaustive hands-on checklist unverified |

### T1 — Make the behavior measurable

Add a focused deterministic validator (`tools/validate_boat_banking.c`, using the existing
validator linkage pattern). Add a small `--report <path>` mode if useful; check write errors.
Keep artifacts under `build/`. Do not change production physics in this task.

Record per-tick time, signed water-relative speed, wheel, engine angle, planing, yaw rate,
roll/rate, height/pitch and horizontal energy. Capture a pre-change executable or trace
for later comparison. Assert that tests intended for open water do not touch basin walls.
Extend named-state comparisons when the new field arrives; avoid raw struct-byte hashes.

Provide reproducible fixtures for straight acceleration, sustained left/right turns,
countersteer, neutral coasting, reverse, and stopping. For matched curvature comparisons,
use a controlled-tow fixture: prescribe horizontal velocity and yaw before each tick,
keep vertical state evolving, and measure load/roll. Explicitly label the imposed
horizontal motion as external work. Keep it separate from freely simulated handling.
An aligned velocity plus yaw alone is insufficient: the two lateral stations can cancel
without any net centripetal force. For each prescribed `u` and `r = u / radius`, solve a
constant body-side slip velocity such that the existing net lateral force is `u*r`.
In calm water with neutral propulsion, use copied boats and one-tick velocity differences
projected on the **pre-tick** right vector to measure that force; bracket/bisect the slip
in the fixture and fail clearly if no root is found. Do not add a production force just to
make the fixture turn. Restore the prescribed `u`, solved slip and `r` each tick, rotating
the velocity with heading. Record the residual and call this a nominal radius: sideslip
means it is not an exact freely simulated trajectory radius. Test actual free turns too.

Verify side-height signs from `boat_player_pose`/transformed points for an imposed positive
and negative roll at several headings. Record the existing small-angle restoring stiffness
and recovery time from the support formula and a free-decay run. Add passing assertions for
existing behavior only; new banking expectations activate with T2.

### T2 — Implement the specified model

Edit `src/boat.[ch]`, the focused validator, replay revision and directly affected fixtures.
Expose load diagnostics only as needed for tests/reports; no runtime HUD/API framework.
Preserve existing force ordering and use the specified moment/filter/guard equations.

Acceptance: correct sign in established mirrored turns; zero turn load at still rest and
straight calm running; bounded moment; deterministic reset; filter and roll continue through
neutral; low reverse gain; free recovery; no full-lock tilt in a stationary unpowered boat.
Audit all resets and named-state comparisons with `rg`.

The old `validate_calm_reference` includes **zero roll during turning**. Preserve its old
position, velocity, heading, yaw and planing expectations. Replace only the now-obsolete
turning vertical/attitude expectations with independently checked banking assertions and
documented measured baselines. Straight/rest expectations remain. Investigate each other
changed bound, especially environment/replay bounds; do not bulk regenerate goldens or
loosen tolerances until green.

T2 must already pass the existing suite plus core banking checks. T3 adds broader coverage,
not a deferred repair of a failing T2.

### T3 — Verify dynamics and difficult cases

Required independent properties and initial acceptance targets:

| Scenario | Check |
| --- | --- |
| Controlled radius 30 units, speeds 4/8/12, planing held consistently for the comparison | Steady mean inward roll grows with measured lateral load before saturation; report radius/load/roll together. |
| Speed 8, radii 20/40/80 | Tighter turn gives larger steady bank before saturation. |
| Same imposed load at planing 0/0.5/1, positive cruise speed | Stronger response on plane, continuous through the blend. |
| Mirrored calm turns | Roll/rate mirror; height, pitch and speed agree within recorded float tolerances. |
| Straight 0/4/12/18 speed, no lateral flow or steering | No sustained bank; speed alone cannot drive roll. |
| Forward turn then neutral | Bank follows remaining lateral force, not throttle; horizontal kinetic energy does not increase in calm stationary water. |
| Established turn then countersteer | Continuous roll, delayed side change, then inward bank on the new turn; no instantaneous angle sign flip. |
| No-force release fixture | With zero filtered load and no horizontal motion, from 10° roll settle below 0.5° and 0.5°/s within 3 s; no growth of unforced floating energy, including guard potential where engaged. Separately verify decay after a driven turn; residual filtered load can still perform work. |
| Low-speed docking / reverse | Controlled 1-unit/s docking bank below 2°; prescribed reverse `u=-3`, `yaw=0.4` below 3°. Test force direction, not a universal reverse-wheel sign. |
| Matched hull-relative velocity under uniform flow | Identical bank in a controlled fixture with equal local relative motion; production spatial-current presets are not uniform-flow fixtures. |
| Beam/head/following waves, scripted wake, own wake in a turn | Finite coupled state, no growing oscillation; initial target total roll below 30° on documented standard drives, recovery after forcing passes. |
| Shore, basin corner, moored and moving traffic contacts | No impulse-derived banking spike, escape, or sustained instability; valid footprint retained. |
| Repeated turns and transitions, at least 120 simulated seconds | Finite state and bounded roll; reproducible rerun on the same build; no long-run drift at rest. |

For controlled circles reset/recenter only horizontal coordinates as needed on flat,
current-free water; do not teleport through a spatial water field and call it an invariance
test. Use the existing query/helper conventions to supply synthetic uniform flow if needed,
without adding a user-facing water preset. Controlled-tow tests cannot prove passive energy
behavior. Real free-drive tests must cover that separately.

Print mean/peak roll, peak roll rate, lateral acceleration, settling time, and peak combined
wave/turn roll. Targets are design acceptance criteria, not measured results yet. If an
initial coefficient misses a target, diagnose force/sign/integration first; tune only the
new named banking constants, record why, and rerun the affected matrix.

### T4 — Make the result reviewable

Extend the focused fixture or existing review tooling with deterministic snapshots at
straight running, established left/right turns, S-turn reversal, coasting, and combined
wave/turn peaks. Share fixture inputs between measurement and capture; avoid two independent
driving scripts. Register a fast headless regression test and GPU checks under the existing
GPU option/label. Avoid screenshot comparisons that merely bless whatever the model produced.

Inspect CPU/GPU captures at native 320x200, with a close side/rear review view as well as
the default camera. Compare transformed hull/cabin/outboard attachment points numerically;
require tight pose agreement, not pixel identity between renderers. Inspect waterline,
spray, engine attachment, reflections/shadows and stable horizon.

Record and reload a banked course run; compare live/sampled poses at sample times and
intermediate times, ghost versus replay, and seek forward/backward while paused. Test old
revision rejection and preservation of old best files. Replay bounds must admit the tested
physical angles without accepting arbitrary/nonfinite values.

Update the boating section in `README.md`, the current summaries in `BOAT_PHYSICS_PLAN.md`,
`BOATING_PLAN.md` where affected, and `docs/README.md`. Explain speed **and turn tightness**,
inertia and continued wave motion. Keep historical accepted handling records dated.
Do not describe planning targets as implemented behavior.

### T5 — Tune and review the feel

Use the recorded matrix to tune only the new model. Aim for readable 5–10° sustained bank
in ordinary fast turns and roughly 10–12° in hard calm-water turns; these are initial
artistic targets, not strict monotonic promises for identical wheel input at different
speeds. Real turn radius, slip and speed loss all change with that input.

Manually run basin, Eriksberg GPU+AA and river: accelerate straight, sweep both ways, make
small corrections, countersteer into an S, coast, cross a wake while turning, reverse along
a quay, reset, and replay a banked run. Check window focus/pause and camera comfort. A
scripted desktop smoke is useful evidence but is not hands-on handling acceptance.

Record automated verification, manual playback, and user acceptance separately. T5 may be
implemented and verified while user feel acceptance remains pending. Ask for that feedback
with concrete captures/measurements once the result exists; do not claim acceptance from
a successful test run. Further hull/roll-to-grip coupling is a new design decision after
this gate, not automatic follow-on work.

## Verification and handoff

At implementation handoffs, run from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure --timeout 60
git diff --check
```

Record the current `PROTEUS_GPU_TESTS` value. Use desktop graphics access for the GPU suite;
report unavailable checks explicitly. Do not silently turn the shared option off. New
fixtures must use separate output paths and temporary replay storage, not personal bests.
Publish exact new report/capture commands after implementing their parsers; their CLI is
not available at planning time. Use Instruments only if the added constant-time arithmetic
or review exposes a runtime problem; no performance improvement is claimed here.

For each T task record: starting revision/dirty files; completed change; changed coefficients;
commands/platform/results; report/capture paths; manual/user review; known limits; next task.

Current gate closed: the user accepted the combined banking/H3 carving feel on 2026-10-20.
No further coefficient tuning is queued. Preserve the dated evidence below; the physics
plan owns H3 acceptance and remaining hands-on/platform verification limits. Optional
trim/loading, foundations and harbor work remain separate tracks.

### Planning evidence — 2026-10-20

Source/consumer and existing-test audit completed on macOS at `7d5fd48`. This change adds
documentation only. Banking behavior, coefficients, new fixtures and acceptance targets
have not been implemented or measured.

Debug configure/build succeeded with the existing `PROTEUS_GPU_TESTS=ON`; CMake emitted
raylib's existing macOS OpenGL deprecation notice. The full CTest command above passed
70/70 tests, including 18 GPU checks, in 143.31 seconds. This validates the existing
runtime baseline, not the proposed banking model. Changed local file links and whitespace
were checked. No new audiovisual review was required or performed for this documentation
change. Implementation, manual banking review, and user feel acceptance all remain pending.

### T1 handoff — 2026-10-20

Starting revision: `7d5fd48`, macOS Apple Silicon. Existing dirty files were
`docs/BOAT_PHYSICS_PLAN.md`, `docs/README.md`, and the untracked copy of this plan;
those planning edits were preserved. T1 adds `tools/validate_boat_banking.c`, its
shared-world/harbor linkage in CMake, and the headless `boat_banking` CTest. Production
physics, tuning, rendering and replay revision remain unchanged. T2 is next; T3–T5 and
manual/user banking review remain pending.

The validator provides straight acceleration (6 s), sustained mirrored turns (8 s),
countersteering (4 s each direction), neutral coasting with steering from speed 12,
reverse acceleration (5 s), braking/stopping (2 s reverse command then 6 s neutral),
and a 3 s unforced roll-decay fixture. Every free-drive tick is repeated independently
and compared using named physical/control fields. Conservative hull clearance is checked
before and after stepping; initial straight/reverse fixture durations/positions were
corrected when these checks detected approach to a basin wall. No contact baseline was
accepted as open-water evidence.

The separate 8 s controlled-tow cases impose horizontal position/velocity/yaw and planing
before each tick while leaving vertical state evolving. This is **external work**, not a
passive-energy test or a freely simulated circle. Planing is reset to 1 before each tick
(the existing speed-dependent update still runs inside that tick). Calm water is spatially
uniform. A copied, neutral, one-tick probe and bracketed bisection solve constant body-side
slip for the specified lateral load. No production force/API was added. Nominal radius is
signed by turn direction; it is not an exact path radius in the presence of sideslip.

Measured positive-turn baseline (negative turns mirror these loads/slips):

| Forward speed | Nominal radius | Solved side slip | Lateral acceleration |
| --- | --- | --- | --- |
| 4 | 30 | -0.193978 | 0.533333 |
| 8 | 30 | -0.553380 | 2.133333 |
| 12 | 30 | -0.964936 | 4.800000 |
| 8 | 20 | -0.809129 | 3.200000 |
| 8 | 40 | -0.420329 | 1.600000 |
| 8 | 80 | -0.214162 | 0.800000 |

Across both directions the maximum measured load residual was 0.00006437; the per-tick
assertion allows 0.0003 for finite-difference float rounding at rotated headings. Steady
mean/peak roll rounds to 0.000000° in all current calm fixtures, confirming the missing
banking baseline rather than asserting that turns should stay level in T2. Free mirrored
turns reach 13.495104 peak lateral acceleration; countersteer reaches 18.021446. Those are
transient loads, not steady circular-turn calibration values.

Transformed hull-side checks at heading 0, ±π/2 and near ±π confirm that positive roll
lowers simulation starboard (renderer local negative X); negative roll lowers port.
Small-angle one-tick restoring stiffness measures 27.040218 against the independent
support-formula value 27.04. An initially stationary 10° roll disturbance remains below
both 0.5° and 0.5°/s from 0.625 s through the end of the 3 s test. This is the existing
small-angle recovery baseline, not verification of the proposed large-angle guard.

Reproduce from the repository root (the report directory must already exist):

```sh
mkdir -p build/banking-review
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/validate_boat_banking --report build/banking-review/baseline.csv \
  > build/banking-review/baseline.txt
./build/validate_boat_banking --report build/banking-review/rerun.csv \
  > build/banking-review/rerun.txt
cmp build/banking-review/baseline.csv build/banking-review/rerun.csv
ctest --test-dir build --output-on-failure --timeout 60
git diff --check
```

Each CSV has 18,000 per-tick rows plus its header. Angles/rates use radians, time uses
seconds, and all columns describe pre-tick state except lateral acceleration (the next
velocity change projected onto the **pre-tick** right vector) and its target residual.
The report records signed water-relative speed, wheel, engine angle, planing, yaw rate,
roll/rate, height/pitch, horizontal kinetic energy including yaw, nominal radius and slip.
Report open/write/close failures produce a nonzero exit. Invalid-option and nonexistent
parent-directory cases were exercised and rejected. A subprocess restricted to a 1,024-byte
file limit also returned failure on an incomplete report, exercising the write-error path.
Two complete reports matched byte
for byte. T1's preserved executables are
`build/banking-review/validate_boat-pre-banking` (existing handling validator) and
`build/banking-review/validate_boat_banking-t1` (the focused baseline validator).
All artifacts are local ignored build outputs, not committed baselines.

Verification: Debug configure/build passed without project compiler warnings; raylib
retains its existing macOS OpenGL deprecation configure notice. `PROTEUS_GPU_TESTS=ON`
was preserved. The first sandboxed full-suite run was interrupted at the first GPU
screenshot after macOS window startup stalled; the suite was rerun with desktop graphics
access (log: `build/banking-review/ctest-desktop.log`). No new audiovisual
review is required for this fixture-only task; no manual playback, hands-on handling,
user acceptance, other-platform execution or Instruments measurement is claimed.

Full verification result, 2026-10-20: the desktop Debug run passed **71/71 CTests**,
including 18 GPU-labelled tests, in 144.92 seconds with `PROTEUS_GPU_TESTS=ON`.
Changed documentation file targets exist and `git diff --check` passed.

### T2 handoff — 2026-10-20

Starting revision: `39c3786` (T1 committed at the user's request), clean worktree.
T2 implements the specified pre-contact propulsion/station/skeg side-load snapshot,
smoothed gain-weighted `Boat.turn_roll_load`, bounded applied moment, and passive
large-angle guard. The buoyancy helper takes the moment explicitly and retains physical
roll inertia, support forces and damping. All initial coefficients are exactly those in
this plan; none were tuned. Horizontal force accumulation order is preserved. Handling
revision is 3; replay layout/version remain unchanged, sampled playback does not integrate
the new filter, and revision-derived best paths preserve older files.

Core checks cover still full lock without power, straight running, signed established
turns, exact neutral filter decay, reset/location/course reset, saturated moment integration,
reverse low gain, recovery from ±32°, and repeatable live state including the new field.
The filter is independently checked against finite-difference lateral acceleration on every
free-drive/tow tick. Neutral coasting dissipates horizontal energy while banking. Existing
profile/environment/wake named-state comparisons now include the new field. Reset and replay
reconstruction sites were audited with `rg`; aggregate initialization zeros the new field.

Measured results (macOS Apple Silicon, Debug):

- All 18,000 T1 report rows match T2 exactly for the recorded horizontal/control/load
  fields, including speed, yaw, energy, lateral acceleration and solved-slip residual.
  Two T2 reports also match byte for byte; the CSV adds pre-tick `turn_roll_load`.
- Radius-30 tow mean bank at speeds 4/8/12 is 1.806°/8.647°/11.931°. At speed 8,
  radii 20/40/80 give 10.688°/7.046°/3.855°. Mirrored signs agree to report precision.
  These are externally driven nominal-radius fixtures, not free trajectories.
- Free left/right peak bank is 15.077°; countersteer peaks at 25.857° and 70.441°/s.
  Neutral coast peaks at 7.488°. A reverse tow at `u=-3`, yaw `.4`, slip `.8` peaks
  at 1.124°. Still/straight turn load is zero (attitude has sub-microradian float noise).
- The moment saturates at 5.663244 normalized torque units. Small-angle restoring
  stiffness remains 27.040218 and 10° unforced recovery remains 0.625 s at the T1 thresholds.
- The accepted calm-reference horizontal/planing and straight/rest expectations remain
  unchanged. Only the turning attitude sample changes: height `.0500000007`, pitch
  `.0531556047`, roll `.216758370` radians (12.419°), filtered load `15.8966875`.
  Tight measured attitude checks supplement the independent force/filter/sign checks.
- The existing 300 s powered/reversing gentle-wave stress drive exceeds its old `.4` rad
  roll bound: peak 33.757816° at 185.358 s (speed 16.660, filtered load -8.5804, roll rate -0.12634 rad/s).
  This combined forced transient can unload supports;
  the 25° guard is a passive spring/damper, not an angle cap. Its new explicit 35° bound
  admits this measured behavior while retaining all height/pitch and post-forcing recovery
  checks. No other environment/replay bounds were relaxed. This peak exceeds the proposed
  T3 standard-drive 30° target and needs explicit follow-up in T3/T5; no stronger guard or
  lower moment was silently tuned to hide it.

Reproduce reports using the T1 commands with report names `t2.csv` and `t2-rerun.csv`.
Additional review commands actually run:

```sh
./build/wake_velocity --renderer cpu --no-player-wake \
  --screenshot build/banking-review/t2-straight-cpu.png --time 2
./build/wake_velocity --renderer cpu --no-player-wake --camera-distance 8 --camera-height 4 \
  --screenshot build/banking-review/t2-turn-cpu.png --time 5
./build/wake_velocity --renderer cpu --waves \
  --screenshot build/banking-review/t2-waves-cpu.png --time 8
```

The native 320x200 CPU frames were inspected: straight attitude, close turning hull/outboard,
and combined own-wake/wave turn are readable with an upright horizon. Detailed attachment,
spray, replay/ghost and seek review remains T4. No user banking acceptance or other-platform
execution is claimed. T3 is next; T4/T5 and optional trim/loading remain unstarted.

Verification, 2026-10-20: Debug configure and full build passed without project compiler
warnings (raylib retains the macOS OpenGL deprecation configure notice).
`PROTEUS_GPU_TESTS=ON` was preserved. Full desktop CTest passed **71/71**, including
18 GPU-labelled checks, in 148.10 s (`build/banking-review/ctest-t2-desktop.log`). The final
reverse-tow assertion was then built and the focused CTest rerun passed; all runtime code
and the other fixtures were already included in the full suite. The earlier T1 recheck
passed through CPU test 52 before the first sandboxed GPU launch stalled and was interrupted;
T1's original 71/71 desktop evidence remains recorded above.

Additional desktop commands completed successfully:

```sh
./build/wake_velocity --renderer gpu --gpu-aa --no-player-wake \
  --camera-distance 8 --camera-height 4 \
  --screenshot build/banking-review/t2-turn-gpu.png --time 5
./build/wake_velocity --demo --location eriksberg --gpu-aa --frames 600
```

The native GPU frame was inspected alongside the CPU turn frame: rolled hull/cabin/outboard,
shadow and level horizon are coherent in this view. Scripted desktop playback is a smoke
check of motion/rendering/asset loading, not hands-on controls, subjective listening, or
user feel acceptance. No personal-best/course recording was written by these commands.
The report pair, handling measurements, build logs, captures and smoke log are ignored local
outputs under `build/banking-review/`. Changed Markdown file targets and `git diff --check`
passed. No performance issue requiring Instruments was observed; no profiling claim is made.


### T3 handoff — 2026-10-20

Starting revision: `4ff2473`, clean worktree, macOS Apple Silicon. The user authorized
continuing the plan and committing each step. T3 extends the existing headless banking
validator, strengthens only the new large-angle recovery coefficients, and tightens the
existing 300-second stress roll bound from 35° to 30°. T4 is next; no presentation/replay
fixture work or T5 feel acceptance is claimed here.

The controlled-tow matrix now asserts speed/load and radius/load monotonicity and mirrored
steady means (0.00001 rad tolerance). The equal-load planing fixture solves side slip
independently for each imposed pre-tick planing value 0/0.5/1; at speed 8, nominal radius
30 and lateral acceleration 2.133333, mean bank is 1.551674°/5.829083°/8.646818°.
As in T1, pre-tick planing is imposed and the normal speed-dependent update runs in each
tick; these fixtures supply external horizontal work. A neighboring-planing demand check
covers continuity. Uniform synthetic flow `(1.7, -0.8)` with the same hull-relative motion
matches roll/rate within 0.00001 and filtered load within 0.00002. No production preset or
force diagnostic API was added. Straight prescribed speeds 0/4/12/18 have no turn load or
sustained roll. A 1-unit/s docking tow, yaw .4 and slip .2, peaks at 0.206514°; the existing
reverse tow remains 1.123860°.

A freely driven mirrored countersteer pair checks roll/rate, height, pitch and speed on
every tick, at tolerances 0.00002 rad, 0.00005 rad/s, 0.00001 height/pitch and 0.00005 speed.
Roll changes continuously through integrated angular velocity; it crosses zero 1.05 s after
the opposite command and finishes inward on the new turn. Its subsequent neutral release
keeps the filter and momentum, dissipates horizontal kinetic energy each tick, and settles
within the checked 20-second window. Independent unforced floating-energy checks include
unilateral spring, gravity, angular/vertical kinetic energy and guard potential. Maximum
per-tick numerical energy increase is below 0.0000001 (tolerance 0.000002); ±10° releases
settle below 0.5° and 0.5°/s at 0.625 s, and ±32° releases at 1.016667 s. Residual-load driven
release is deliberately separate from this passive-energy check.

Six reproducible 120-second free circles start at speed 8, throttle .55 and held wheel .55:
initial beam/head/following headings relative to the gentle-wave preset, scripted wake
starting at `(0, 1.6)`, own wake, and calm water. Turning changes the encounter direction;
the first three names describe the initial orientation. No spatial-field teleporting or
contacts are accepted in these measurements. Initial alternating S routes reached basin
boundaries and were replaced by contained circles. A separate 120-second repeated-transition
fixture uses the existing stress inputs `sin(tick*.005)` wheel and `sin(tick*.001)` throttle
adjustment, checks valid footprints and exactly repeatable named physical state, and peaks
at 29.204840° and 177.445450°/s. The high transient rate is measured evidence for T5 review,
not a claim of comfortable motion.

The first circle matrix exposed 30.832241° ambient-wave and 34.048386° own-wake roll with
the T2 constants. The forcing/attachment/filter tests pass; unilateral support unloading
and angular momentum carry roll past the weak passive guard. T3 increases only
`ROLL_GUARD_STIFFNESS` **60 → 180** and `ROLL_GUARD_DAMPING` **4 → 12**. Gain, filter,
12° moment scale, 25° engagement, 5° damping blend, buoyancy, propulsion and grip are unchanged.
No angle/rate clamp, contact reset, or increased acceptance bound was introduced. This
remains a game stability aid, not calibrated hull stability. Revision stays 3 for this
banking implementation sequence; replay layout is unchanged.

Final circle measurements:

| Initial environment | Peak total roll | Peak roll rate | Peak absolute height | Peak pitch |
| --- | --- | --- | --- | --- |
| Beam waves | 28.039995° | 63.621998°/s | 0.371059 | 11.408874° |
| Head waves | 28.061476° | 64.219955°/s | 0.406635 | 10.711516° |
| Following waves | 27.991322° | 64.639542°/s | 0.410857 | 11.711240° |
| Scripted wake | 20.132694° | 20.462963°/s | 0.130115 | 9.047836° |
| Own wake | 29.665262° | 109.920059°/s | 0.614577 | 13.364295° |
| Calm | 12.460248° | 20.113857°/s | 0.053814 | 9.659960° |

Each late-half peak stays within 1° of its first-half peak; all total roll stays below 30°.
The own-wake fixture uses explicit height/pitch bounds 1/.65 rad, consistent with existing
wake-environment coverage; ambient stress retains its existing .5/.4 bounds. Removing
forcing preserves momentum/filter and recovers on calm water; after 120 s coast, a further
120 s rest has zero measured positional drift. Every driven tick is reproduced independently.
The older 300-second powered/reversing gentle-wave drive now peaks at **29.457630°**
(previously 33.757816°), with existing height/pitch/recovery checks preserved.

The guard can change wave-coupled horizontal motion through existing water-normal forces.
Against the preserved pre-tuning executable on the identical circle inputs, maximum forward
speed changes are 0.033263 for ambient waves and 0.085691 for own wake; maximum yaw-rate
changes are 0.010102 and 0.027089 rad/s. Integrating the per-tick yaw-rate differences gives
at most 0.566° heading evolution difference over 120 s in the ambient cases. This is a yaw
integral diagnostic, not a measured positional path error. Calm and scripted-wake circle
horizontal/control reports remain identical. All 18,000 T2 horizontal/control/load rows match T3 exactly, and
the accepted calm-reference sample remains covered; no six-axis energy conservation claim
is made.

Eight contact fixtures cover basin shore/corner, the moored workboat and pontoon, and all
four travelling vessels. Each tick compares against an identical pre-contact hydrodynamic
probe in flat, safe basin water: filtered load, roll and rate match within 0.000001, with
measured filter difference exactly zero. Each fixture observes 1–2 impulse ticks, keeps
shore/harbor/traffic footprint clearance, stays below 30° (maximum 7.868683°), and settles
below 0.5° in 10 s. No post-impact velocity difference is used as a banking load.

Reproduce from the repository root:

```sh
./build/validate_boat_banking --report build/banking-review/t3.csv \
  > build/banking-review/t3-measurements.txt
./build/validate_boat_banking --report build/banking-review/t3-rerun.csv \
  > build/banking-review/t3-rerun.txt
cmp build/banking-review/t3.csv build/banking-review/t3-rerun.csv
./build/wake_velocity --renderer cpu --waves --camera-distance 8 --camera-height 4 \
  --screenshot build/banking-review/t3-waves-cpu.png --time 8
```

Reports contain 105,360 per-tick rows and match byte for byte. Spatial-wave rows use `nan`
for unavailable lateral acceleration/load residual; actual filtered load, attitude, velocity
components and energy columns retain their stated meaning (no velocity-difference claim of
pre-contact force in waves). Detailed new energy/blend/contact/long-transition summaries
are printed to stdout rather than mixed into the CSV. Baseline executable/report,
measurements, logs and captures are ignored local outputs under `build/banking-review/`.

Verification, 2026-10-20: Debug configure and full build passed without project compiler
warnings; raylib retains its existing macOS OpenGL deprecation configure notice.
`PROTEUS_GPU_TESTS=ON` was preserved. Full desktop CTest passed **71/71**, including
18 GPU-labelled checks, in 161.27 s (`build/banking-review/ctest-t3-desktop.log`).
The expanded banking check took 13.38 s in that run. Formatting, changed Markdown file
targets and `git diff --check` passed. No performance problem requiring Instruments was
observed; no runtime performance improvement is claimed.

The CPU frame above and the GPU+AA frame below were inspected at native 320x200. Both
show coherent rolled hull/cabin/outboard and a level horizon. Detailed emitter, attachment,
reflection, ghost/replay and seek acceptance remains T4. Additional commands actually run:

```sh
./build/wake_velocity --renderer gpu --gpu-aa --waves --camera-distance 8 --camera-height 4 \
  --screenshot build/banking-review/t3-waves-gpu.png --time 8
./build/wake_velocity --demo --location eriksberg --gpu-aa --waves --frames 600
```

Both desktop commands exited successfully with desktop graphics access. The latter is a
scripted motion/rendering/asset-loading smoke check, not hands-on control, subjective audio
review or user feel acceptance. No personal-best files were written. Windows/browser
execution, human banking acceptance, exhaustive wave/contact coverage and general stability
outside these fixtures remain unverified. High measured roll rates and the narrow own-wake
margin below 30° remain explicit T5 tuning/review concerns. T3 is complete; implement only
T4 next, preserving this evidence and the accepted horizontal handling baseline.

### T4 handoff — 2026-10-20

Starting revision: `79c32cd`, clean worktree, macOS Apple Silicon. The user authorized
continuing and committing each step. T4 implements presentation/replay review tooling;
T5 is next. No banking coefficients, horizontal handling, camera settings, physics revision
or replay layout changed. Hands-on banking acceptance remains pending.

`tools/boat_banking_fixture.h` now owns the existing T1/T3 free-drive reset, tick counts and
inputs, shared by the dynamics validator and new `boat_banking_review`. The new tool checks
every driven tick and selects deterministic straight, established left/right, S reversal,
coast peak and gentle-wave/turn peak snapshots. The coast and wave captures select the
largest absolute roll over the existing eight-second drive; they are peaks of these named
fixtures, not the longer T3 stress matrix. All drives stay inside open basin water.

| Snapshot | Tick | Roll | Roll rate |
| --- | --- | --- | --- |
| Straight | 720 | 0° | 0°/s |
| Left | 960 | -12.418953° | -0.000051°/s |
| Right | 960 | 12.418953° | 0.000048°/s |
| S reversal | 600 | 3.504119° | -70.072227°/s |
| Coast peak | 269 | 7.488263° | 0.010200°/s |
| Gentle-wave/turn peak | 495 | 28.049162° | 6.270194°/s |

Independent elementary rotations compare transformed hull/cabin/outboard probes within
0.00003 world units, including headings 0, ±π/2, near ±π, and ±29° roll. Every driven tick
also checks the shared scene's upright camera. Both renderers consume this shared pose;
this is numerical pose agreement, not pixel identity or a GPU vertex readback claim.
New spray births are compared with rolled rail origins, the queried waterline and an
independent symmetric derivative of attachment height (lift tolerance 0.0002). This exposed
one concrete attachment error: calm-water spray inherited a small-angle vertical-velocity
estimate, whereas displaced-water spray already inherited full hull attachment velocity.
Both now use `motion.velocity.y`; displaced water retains its existing entry boost.
Floating foam/wash continue following water, and spray's existing horizontal emission
recipe is preserved. Particles do not feed the physical solver.

The existing replay validator now drives a complete S course with ordinary steering/throttle
inputs, without prescribed position/velocity/attitude. It reaches all four gates, peaks at
12.348737° bank, and writes `build/replay-tests/banked.run` using the normal course recorder.
Live physical poses match recorded end-of-tick samples, excluding the interpolated finish.
Reloaded sample and midpoint roll agree with independently calculated expected roll within
0.000002 rad; original/reloaded attachment positions agree within 0.00002 world units.
The checks traverse forward and backward while paused, compare direct ghost sampling with
transport replay, and verify sampled playback has no roll-rate/filter reconstruction.
The original fractional gate recording and transport checks remain intact.

Replay validation previously accepted any finite pitch/roll up to 100000 radians. The same
save/load validator now bounds both to ±π/2: this admits the measured <30° physical stress
excursions with margin while rejecting inverted/arbitrary attitudes. Tests save a 29.7°
sample and reject π/NaN without replacing the valid output. Handling revision remains 3,
serialized format remains 1. The previous-handling rejection fixture now uses revision 2
and explicitly rereads the rejected source header to verify preservation. Existing
revision-derived best-path and destination-preservation behavior is retained; no personal
best paths were used for this review.

Reproduce from the repository root:

```sh
./build/boat_banking_review
./build/boat_banking_review --output build/banking-review/t4-cpu
./build/boat_banking_review --gpu --output build/banking-review/t4-gpu
./build/validate_boat_replay build/banking-review/t4-replay
./build/validate_boat_banking --report build/banking-review/t4.csv \
  > build/banking-review/t4-measurements.txt
cmp build/banking-review/t3.csv build/banking-review/t4.csv
./build/wake_velocity --renderer cpu --replay build/replay-tests/banked.run --time 5 \
  --camera-distance 8 --camera-height 4 --screenshot build/banking-review/t4-replay-cpu.png
./build/wake_velocity --gpu-aa --replay build/replay-tests/banked.run --time 5 \
  --camera-distance 8 --camera-height 4 --screenshot build/banking-review/t4-replay-gpu.png
./build/wake_velocity --demo --location eriksberg --gpu-aa --waves --frames 600
```

CPU/GPU output directories each contain 19 native 320x200 images: six fixtures in default,
close rear (8/4) and offset side/rear cameras, plus a banked ghost view. Representative
straight/default, left/rear, right/side, reversal/side, coast/default, wave-peak/rear and ghost
pairs were inspected, along with the reloaded course frames above. Hull/cabin/outboard and
spray/waterline remain coherent; GPU reflections/shadows follow the rolled craft and the
camera keeps its upright view. CPU reference lighting/water differs by design. These are
sampled presentation checks, not exhaustive occlusion or human comfort acceptance.
The full 105,360-row dynamics report matches the preserved T3 report byte for byte.

The 600-frame Eriksberg GPU+AA wave demo exited successfully with desktop graphics access:
scripted motion/rendering/asset-loading smoke only, not hands-on controls, subjective audio
review or user acceptance. An initial sandboxed GPU capture stalled on macOS desktop
services, was stopped, and succeeded with desktop access. No Instruments run was warranted
by an observed performance issue. Windows/browser execution, human control/seek comfort,
and high-rate/narrow-stability-margin tuning remain unverified; T5 owns those review gates.

Verification, 2026-10-20: Debug configure and full build passed without project compiler
warnings (existing raylib macOS OpenGL deprecation configure notice). `PROTEUS_GPU_TESTS=ON`
was preserved. Full desktop CTest passed **73/73**, including **19 GPU-labelled tests**,
in 168.24 s (`build/banking-review/ctest-t4-desktop.log`). The new headless presentation check
and GPU capture fixture passed; the final live-versus-recorded end-of-tick replay assertions
also passed in a focused replay rerun. Formatting, changed Markdown file targets and
`git diff --check` passed. Logs, CSVs, replay fixtures and captures remain ignored under
`build/`. T4 is complete; stop before T5. T5 owns measured tuning, the full hands-on location/
controls/replay checklist, and user banking acceptance; the high transient roll rates and
own-wake margin recorded in T3 remain open review concerns.

### T5 handoff — 2026-10-20

Starting revision: `8afe75a`, clean worktree, macOS Apple Silicon. The user requested T5
and retained the instruction to commit each step. Initial hands-on T4 feedback was positive
overall, with one specific concern: “it stays banked a little too long after I straightened
out.” That is initial feedback, not acceptance of this revised tuning or the complete
location/controls checklist. T5 implementation and automated/scripted verification are now
available for review; full hands-on playback, focus/pause/camera comfort and user acceptance
remain pending. No other project track was advanced.

The new shared steering-release fixture starts at speed 12 and full throttle, holds wheel
0.25/0.55/1 for eight seconds, then releases steering for three seconds while keeping power.
It drives through normal controls and preserves yaw/roll/filter momentum. At release it
translates the boat and its particle history once to basin centre on uniform, flat,
current/wake-free water, keeping the entire measured exit clear of contacts. This changes
neither horizontal motion nor physical attitude; it is not a spatial-wave invariance test.
Mirrored runs check sign symmetry and continuous roll integration. Per-tick measurements
and capture views use these same inputs. Recovery times below are the final crossing below
2° bank, with no later recrossing during the three-second observation window, measured
from steering release; they are not measured from zero yaw or zero lateral force.

| Held wheel | Steady bank before → after | Below 2° bank before → after | Bank at 1 s before → after |
| --- | --- | --- | --- |
| 0.25 | 12.381° → 9.712° | 1.517 s → 1.075 s | 8.185° → 2.601° |
| 0.55 | 12.419° → 11.771° | 1.750 s → 1.300 s | 11.165° → 5.026° |
| 1.00 | 12.419° → 11.786° | 1.892 s → 1.458 s | 12.126° → 7.355° |

Yaw falls below 0.05 rad/s at 0.450/0.633/0.808 s in both versions. The old high gain
saturates the moment through much of the fading lateral load; a .25 wheel fast turn already
holds nearly maximum bank. Shortening only the filter to .06 s improves recovery by about
.083 s and leaves that saturation intact (and increases some turn-entry overshoot).
T5 instead changes only **`TURN_ROLL_PLANING_GAIN` .85 → .28** and
**`TURN_ROLL_RESPONSE_SECONDS` .12 → .06 s**. Lowering gain is the main recovery change;
the shorter filter removes the remaining avoidable delay. The quarter-wheel sustained fast
turn now meets the 5–10° artistic target, and harder turns remain around 12°.
A gain .30/filter .12 comparison gave 10.056° in the quarter-wheel fixture and slower
1.183/1.400/1.558 s recovery; the selected values give a little more recovery margin.

Low-speed gain .12, speed blend 1–6, 12° moment scale, physical roll inertia, buoyancy,
damping, 25°/5° guard engagement/blend and guard 180/12 are unchanged. There is no steering-
position multiplier, angle/rate reset, hard clamp, or change to propulsion/grip/yaw/camera.
Countersteering still carries the old bank through reversal; the mirrored transition crosses
zero after 0.991667 s (formerly 1.05 s). Its peak drops to 12.263609°. Passive ±10°/±32°
recovery remains 0.625/1.016667 s with the same energy checks. The reverse tow peaks at
1.169904°, docking tow at 0.208131°, and neutral-coast free drive at 2.815781°: reduced gain
still allows force-driven coasting bank. Speed/radius monotonicity and uniform-flow checks
pass with the new gains. Controlled tow remains external horizontal work, not free handling.

The new exit assertions require sustained 5–10°/10–12° banks, <13° entry peaks, below-2°
recovery by 1.10/1.35/1.50 s, and below-2°/s angular motion within another .4 s. They also
retain >5° lean at .5 s, so a cosmetic instant return cannot pass. The accepted calm
reference's horizontal/planing samples remain unchanged; its turning attitude is updated to
height `.0500000045`, pitch `.0531335883`, roll `.213792399` radians, load `5.26432753`,
backed by independent force/filter/tow tests. All **37,320** common calm-water horizontal,
control, acceleration and energy CSV rows match the preserved T4 executable exactly.

Final wave-circle and stress observations:

| Initial environment | Duration | Peak roll | Peak roll rate |
| --- | --- | --- | --- |
| Beam waves | 120 s | 26.419369° | 60.120811°/s |
| Head waves | 360 s | 23.375481° | 68.070900°/s |
| Following waves | 120 s | 21.598619° | 44.692997°/s |
| Scripted wake | 120 s | 11.217952° | 9.446991°/s |
| Own wake | 120 s | 28.775251° | 107.906357°/s |
| Calm | 120 s | 10.294631° | 10.245651°/s |

The existing repeated-transition fixture drops from 177.445450°/s to 99.506981°/s, peaking
at 28.908022°. The existing 300-second powered/reversing gentle-wave drive peaks at
28.954075° (formerly 29.457630°). Own-wake margin improves from 0.335° to 1.225° below 30°;
its high angular rate remains a hands-on review concern. Its peak height is .672679 and
pitch 14.325754°, within unchanged 1/.65-rad wake bounds. Contacts retain finite state,
clear footprints, filter agreement and calm recovery; maximum contact roll is 8.093884°.

One wave assertion was revised explicitly: the old `late_peak <= early_peak + 1°` failed
for the head-wave drive even though its entire roll envelope was smaller than T4. The first
60 seconds peaked at 19.421°, the next at 23.308°. Free turning changes wave encounter
phase; those windows are not identical forcing. T5 extends this drive to **360 seconds**:
successive minute peaks are 19.421/23.308/19.473/20.121/23.375/20.851°. The late-half check
now permits sub-guard phase variation using `late_peak <= max(early_peak, 25°) + 1°`.
This relaxes the window-comparison assertion below guard engagement; it does not increase
the per-tick 30° bound, height/pitch limits, deterministic repetition or post-forcing
recovery requirements. The measured long envelope is bounded, not monotonically growing;
this is fixture evidence, not general stability proof.

Wave-coupled horizontal consequences were measured against the T4 executable over common
120-second inputs. Maximum speed differences for beam/head/following waves are
.058520/.058095/.062197, maximum yaw-rate differences .013080/.013686/.015994 rad/s.
Integrated yaw-rate differences at 120 s are -2.389/-3.482/-1.985°. Scripted wake gives
.022759 speed, .006045 rad/s yaw-rate and -.195° integrated yaw difference. Own wake gives
**1.125332 speed**, **.112229 rad/s yaw-rate** and **-71.505° integrated yaw difference**:
a noticeably different repeated own-wake encounter history over two minutes. These are
speed/yaw diagnostics, not positional path-error measurements. Shared wave-normal forces
use the changed hull pose, so identical force coefficients do not imply an identical wake
trajectory. That feedback behavior and wake comfort remain explicit user-review limits.

The presentation audit found a concrete numerical problem at heading 1.57253, pitch
.00121496, roll .287961: `asin` Euler extraction produced about .0000405 world-unit probe
error, exceeding the existing .00003 attachment tolerance. Extraction now uses
`atan2(sine, hypot(cosine components))`, with a tighter singular branch; physics continues
using its original matrix. Exact and nearby ±π/2 cases were added. The tight pose and
spray checks pass without increasing their tolerances. Physics revision stays 3 for this
banking sequence; the serialized layout stays unchanged and old best files remain separate.

Reproduce current measurements and review artifacts from the repository root:

```sh
./build/validate_boat_banking --straightening
./build/validate_boat_banking --report build/banking-review/t5.csv \
  > build/banking-review/t5-measurements.txt
./build/validate_boat_banking --report build/banking-review/t5-rerun.csv \
  > build/banking-review/t5-rerun.txt
cmp build/banking-review/t5.csv build/banking-review/t5-rerun.csv
./build/boat_banking_review --output build/banking-review/t5-cpu
./build/boat_banking_review --gpu --output build/banking-review/t5-gpu
./build/wake_velocity --demo --location basin --no-player-wake --gpu-aa --waves --frames 900
./build/wake_velocity --demo --location eriksberg --no-player-wake --gpu-aa --waves --frames 900
./build/wake_velocity --demo --location river --no-player-wake --gpu-aa --waves --frames 900
./build/wake_velocity --replay build/replay-tests/banked.run --gpu-aa --frames 240
```

The final 138,120-row report pair matches byte for byte. Baseline executable/report and
candidate comparisons are local ignored artifacts under `build/banking-review/`; the
baseline report adds the same steering-release measurement inputs to T4 physics before
changing coefficients. CPU/GPU directories each contain **43** native 320x200 images:
T4's 19 views plus quarter/half/full-wheel exit captures at 0/.5/1/1.5 s, in default and
close rear cameras. Native CPU/GPU right-side, S-reversal-side, wave-peak-rear,
quarter-wheel one-second default/rear and full-wheel 1.5-second rear pairs were inspected,
along with baseline/current quarter-wheel one-second rear frames. Faster settling is
visible, with coherent hull/cabin/outboard, spray/waterline and GPU shadow/reflection
presentation and an upright camera.

All four final desktop playback commands above exited successfully. They exercise scripted
basin/Eriksberg/river GPU+AA wave motion, neutral/reversing demo controls and the reloaded
banked course. This is scripted desktop playback/asset-loading evidence, not a person
performing the hands-on checklist or listening assessment. Automated tests cover reset,
reverse, countersteer, coasting, contacts, wave/wake forcing, recording, paused seek and
transport focus behavior; OS focus/input ergonomics, camera comfort, the full hands-on
location checklist, Windows/browser execution and user acceptance remain unverified.
No performance issue warranting Instruments was observed; no profiling claim is made.

Verification, 2026-10-20: Debug configure and full build passed without project compiler
warnings; the existing raylib/macOS OpenGL deprecation configure notice remains.
`PROTEUS_GPU_TESTS=ON` was preserved. Full desktop CTest passed **73/73**, including
**19 GPU-labelled tests**, in **172.79 s** (`build/banking-review/ctest-t5-desktop.log`).
The final suite includes the updated dynamics/reference checks and tighter pose conversion
cases. Earlier focused checks exposed the pose precision failure described above; the
final suite and refreshed CPU/GPU captures include its correction. Formatting, changed
Markdown file targets, documented command executions and `git diff --check` passed.
All logs, reports, baseline/candidate executables, fixtures and captures remain ignored
under `build/`. T5's tuning and automated/scripted verification are implemented; its full
hands-on checklist and user acceptance are the next gate. No further coefficient changes
or optional-track work are implied by this handoff.
