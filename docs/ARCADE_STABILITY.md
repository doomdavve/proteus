# Arcade stability and steering roll study

2026-10-24, branch `arcade-stability`. Owner: [wake plan](WAKE_REWRITE_PLAN.md).
The user finds the current boat too easy to capsize, requests lower default waves and
chooses steering to control air roll. This is a first tuning pass for play, not accepted feel.
The candidate was developed in a separate worktree based on `a55fd5e`, while multiplayer
development continued independently in the main checkout. It is integrated into
`main` after `a4660b1`, retaining LAN discovery, held-thrust stepping, reversed scroll thrust
and the title's mouse-steering setting.

Handoff feedback, 2026-10-24: the user responded "Great!" and requested this candidate be
committed. No additional hands-on play session or platform acceptance was reported.

## Current tuning

- The default object crest scale is **1.2 m**, down from 1.8 m. Lifetime, emission cadence,
  propagation and signed overlap stay as before. Overlapping crests can still be taller.
  `--wake-height 1.8` selects the earlier tall setting for single-player comparison.
- In an object-wake world, wetted-hull roll gets extra damping and restoring assistance
  beyond a 15-degree bank. Assistance fades as the hull goes fully inverted; the existing
  inverted-contact capsize latch and R reset remain.
- In flight, wheel position also commands roll. Keyboard A/D or Left/Right and mouse
  steering use the same wheel. The response is progressive: half wheel asks for one quarter
  of the maximum spin. Full wheel targets 4.5 rad/s (about 258 degrees/s) and permits full
  barrel rolls. The existing modest air yaw remains.
- Centre the wheel to brake rotation and help recover a partly tipped hull for landing.
  Air pitch also gets damping and upright recovery. Assistance fades near inversion, so
  releasing steering after an upside-down maneuver does not guarantee a safe landing.
  In mouse mode, move back to centre; stopping the mouse holds the current wheel setting.
- There is no extra thrust or horizontal drag in dry flight, and no new input field or
  network byte layout. Calm/legacy/course worlds use their existing handling. Handling
  compatibility advances to **7**, so recordings and peers from revision 6 do not mix.
  Old best files remain on disk under their earlier names.

Solo launch, from the repository root on main or in the study worktree:

```sh
./build/wake_velocity --location river --mouse-steering --gpu-aa
./build/wake_velocity --location basin --wake-objects-crossing --mouse-steering
./build/wake_velocity --location basin --wake-height 1.8 --mouse-steering
```

The new shared default also applies to `boat_server` and the fleet visual review. Its
authority advertises the actual crest scale to clients as before. The integration into
`main` retains the server's LAN-discovery changes and advances its handling revision to 7.

## Main integration, 2026-10-24

The merge keeps both histories. Its only content conflict was the README control text:
the newer scroll-down-to-increase direction is retained alongside the air-roll explanation.
Apple Silicon/macOS Debug configure/build and all **110 active CTests** pass in 111.00 s,
including the current LAN-discovery checks and twenty active GPU cases. Fifteen retired
field experiments stay disabled. Project sources build without warnings. The merged basin
time-9 CPU capture matches the reviewed worktree candidate byte for byte; changed local
documentation targets and diff whitespace checks pass. Logs:
`/private/tmp/proteus-arcade-merge-{build,ctest}.log`; capture:
`build/arcade-merge-basin.png`. Human play of the combined build and platform CI remain pending.
An agent-run windowed basin-rival demo completed 600 frames / 1232 ticks and emitted 29
wake objects at the new default; log: `/private/tmp/proteus-arcade-merge-desktop.log`.

## Candidate evidence and next task

Baseline, Apple Silicon/macOS Debug, 2026-10-24: the existing 108-case stress envelope at
1.8 m records 84 flights, 84 landings and 43 final capsizes; peak hull height 45.49 m.
The fixture duplicates 1–4 seeded ridges, so these are stress outcomes rather than the
frequency of capsizing during ordinary driving.

Candidate encounter results (same 108 cases per setting):

| Handling / crest scale | Flights followed by landing | Final capsizes | Peak hull height |
| --- | ---: | ---: | ---: |
| Baseline / 1.8 m | 84 | 43 | 45.49 m |
| Candidate / 1.8 m | 85 | 33 | 45.49 m |
| Candidate / 1.2 m default | 83 | 6 | 22.41 m |

The control validator adds mirrored commanded barrel rolls, neutral recovery from a
70-degree bank with an initial roll rate, spin braking, dry horizontal momentum, and a
fully inverted water-contact capsize. The encounter sweep runs both 1.8 m and the new
default. Both settings must retain flights, landings and capsize risk, with fewer capsizes
than the baseline and fewer at the lower default than the tall stress setting.

Automated verification, Apple Silicon/macOS Debug, 2026-10-24: all **109 active CTests**
pass across the full run and a focused rerun; twenty active GPU tests are included and
fifteen retired field experiments remain disabled. The full run took 110.38 s and exposed
two stale assumptions; the corrected clock/net/benchmark checks pass in 10.49 s. Debug
builds are warning-clean in project sources. Logs: `build/arcade-ctest.log`,
`build/arcade-followup-tests.log`, and `build/arcade-corrected-build.log`.

The clock test still compares every physical/wake hash across starts at zero, one day and
thirty days, and requires emission, landing and expiry. The revised trajectory has 19 births
and one landing instead of the old 17 births. Three retained benchmark runs have identical
physical traces and hash `a991b713315ef1e4`; the JSON now records `physics_revision`.

Remote-view diagnostics exposed 30 Hz interpolation errors at sharp 120 Hz pair impulses
on the new routes. Outliers at ticks 3173 and 7171 coincide with actual fleet contacts.
The test retains the 5 cm smooth-motion gate, separately allows 15 cm near recorded pair
contacts, and requires mean error below 5 mm. Observed smooth maxima are 7–10 mm, contact
maxima 76–114 mm. No remote rendering code or packet layout changed. The changed profile-F
trajectories show at most 20 mm own-prediction corrections with keyboard bots; this remains
automated integration evidence rather than a multiplayer feel/play acceptance.

CPU basin/river before/after frames and candidate GPU basin/river frames were inspected.
The river time-9 CPU view is partly behind a quay in both versions; the time-3 GPU view
provides the unobstructed river check. Artifacts: `build/{basin,river}-{before,after}.png`,
`build/{basin,river}-after-gpu.png`, and `build/wake-objects-gpu.png`.
The visible `wake_objects_review --desktop` run completed its 2400-tick scripted self-wake
loop and stationary-camera sequence. At the new default it reached 3.929 m hull height,
recorded two landings and finished in the landing state, without a latched capsize. The
production GPU wake upload/recipe comparisons passed. Log: `build/arcade-visible-review.log`.
This is agent-run scripted playback; physical mouse/keyboard feel, listening and human
acceptance are not established. Changed local documentation links and `git diff --check` pass.

The benchmark's existing 1.8 m stress workload is intentionally retained; its historical
physics-6 timing/trajectory evidence is not a baseline for physics 7.

Next: play repeated single-wake crossings,
turning jumps, mouse/keyboard roll-and-recover attempts and four-boat encounters. Tune the
air rate/recovery and wetted-hull damping from that feedback. Native Windows/Linux,
browser control feel and human multiplayer qualification of the combined build are pending.
