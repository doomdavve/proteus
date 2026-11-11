# Arcade wake gameplay review

2026-10-22, revision `9acee61`. Owner: [wake plan](WAKE_REWRITE_PLAN.md).
Review only: no gameplay or rendering code changed. Source inspection, targeted temporary
probes, current CPU/GPU captures and the routine suite support the findings below.
Hands-on driving, audio listening and new user acceptance were not performed.

## Implementation follow-up

The user subsequently requested addressing this review in order on 2026-10-22.
[Implementation and verification](WAKE_ARCADE_IMPLEMENTATION.md) owns the resulting rules,
code changes, outcome tests, visual pass and remaining limits. The findings below remain
historical source/probe observations for revision `9acee61`.

## Assessment

Keep this direction. The small, bounded wake world is a good foundation for an arcade
boat playground. Steering a loop to make your own ramp, approaching it at different
angles, and combining wakes are already meaningful possibilities. The next constraint
is the boat/gameplay model: tall wakes expose assumptions inherited from gentle-water
handling. Several findings below predate the rewrite; their significance increases with
multi-second flights and full rotations.

The existing checks reproduce 7.195 m above-calm launches, 5.483 m for a diagonal single
wake and 14.34 m for crossed wakes, plus 6.162 m hull height in an actual own-trail loop.
These demonstrate capability, not an established difficulty curve or reliable player
control. Dry-stern counts are not measurements of continuous whole-hull airtime.

## Findings, ordered by gameplay impact

1. **Overturning has no defined outcome.** `src/core/boat.c` applies a roll guard beginning at
   25 degrees, even during flight, while pitch can rotate freely. In a temporary extension
   of the seeded diagonal fixture, one wake reaches 183.09 degrees of pitch and settles
   at 180 degrees after 25 s; simulation continues. Two perpendicular wakes reach 387.85
   degrees and settle at 360 degrees. There is no capsized/game-over state in `Boat`.
   An 80-degree roll initialized 20 m above flat water rebounds to -29.97 degrees in .5 s
   without touching water. This produces an inconsistent distinction between pitch flips
   and sideways flips. Add explicit airborne, landing and capsized outcomes; determine
   inversion using hull orientation relative to world up, not an unwrapped Euler angle.
   Keep normal turn assistance, but deliberately choose its flight/large-angle behavior.
   A brief inverted flight could be a stunt; sustained inverted water contact could fail
   with a fast restart. Those are proposals, not implemented rules.

2. **Airborne motion still uses water propulsion and grip.** In `boat_step()`, engine
   thrust, hull drag, lateral grip, skeg resistance and turn banking do not depend on wet
   contact. A boat starting stationary at height 20 m with full throttle/steering reaches
   4.549 m/s and turns 22.87 degrees in .5 s while its stern remains fully dry. Air control
   can be fun, but should be an explicit, tunable ability rather than the full submerged
   handling model. Separate water contact forces from limited air control and use actual
   contact/orientation to judge landings. Preserve the accepted water steering baseline.

3. **Jump height does not clear collision obstacles.** `hull_contact()` and
   `resolve_traffic()` use only the horizontal footprint and heading. Shore, berth and
   traffic contact checks receive no player height. An airborne basin-boundary probe at
   height 10 m still rejects translation and changes horizontal velocity. The basin edge
   can remain an intentional boundary, but traffic and low obstacles need vertical overlap
   before jumping over them becomes a fair action. Avoid simply disabling all airborne
   contacts: a high quay or bridge must still be solid.

4. **Ordinary boats react visually, not dynamically.** `waterfront_traffic_evaluate()`
   applies the shared water pose to an analytic route; harbor boats similarly follow
   four height samples at fixed berths. They rock/heave but cannot be knocked off course,
   launched ballistically, capsized or pushed away. Moving-hull collision changes the
   player only. The optional basin crossing craft does use real `Boat` physics and shares
   wake emission, but has no hard collision with the player. This is a useful starting
   point for one genuinely interactive rival. Add a dynamic receiver with visible mass,
   inertia and recovery before attempting the whole fleet. Four emitter slots are a
   deliberate limit, not automatic support for every traffic/harbor vessel.

5. **Overlap is exciting but its play envelope is largely untested.** Existing capacity
   stress places emitters 100 km apart; it establishes bounded storage, not four-boat
   local dynamics. Duplicating the seeded ridges into four emitter slots (two in each
   perpendicular direction) launches the receiver to 44.54 m with 27.89 m/s upward speed
   and over two pitch rotations. It eventually settles inverted. This is a synthetic
   amplification probe, not a claim that ordinary driving commonly produces that jump.
   Keep the satisfying reinforcement, but review reachable crossings over speed, angle,
   age and source count, with landing/recovery/capsize outcomes. Height-only and finite-value
   assertions will not catch an upside-down boat continuing normally. Prefer controlled
   response/source tuning over silently removing the visible wave's contact height.

## Performance

The world occupies 5320 bytes on this Mac, with 128 slots, no per-tick allocation and a
4096-byte GPU upload. Lifetime/capacity and derivative/parity tests are useful and pass.
Map area does not allocate a larger simulation. Query and rendering work still scale
with live objects: CPU queries scan the pool; both water shader stages scan all packed
records, fetching two texels per record before local support rejection.

Current full-suite diagnostic: two physics/source ticks median **.1437 ms**; renderer
**2.3199 ms**; combined median **2.4637 ms**, p95 **2.6631 ms**. This is the existing
20-live-object basin loop, Debug with optimized numerical validators, on the development
M1 Max/macOS. It excludes audio, presentation and frame limiting.

A separate temporary optimized renderer probe fills a 16-second radius-15 m circular
source history, freezes world/camera, and duplicates the path for four emitters. It
renders 180 frames per case, discards 60, and measures 120 with `glFinish`:

| Local records | Rendering | Median | p95 | Observed max |
| --- | --- | ---: | ---: | ---: |
| 32 | 320x200 | 1.3744 ms | 2.0328 ms | 2.2064 ms |
| 128 | 320x200 | 2.8422 ms | 3.2577 ms | 3.6767 ms |
| 32 | 1280x800 supersampled to 320x200 | 2.2207 ms | 2.4104 ms | 2.4427 ms |
| 128 | 1280x800 supersampled to 320x200 | 4.4879 ms | 4.7000 ms | 5.2305 ms |

This includes scene construction, upload and complete basin rendering; it excludes
physics, four rendered boats, city traffic, presentation and cold shader compilation.
Overlapping wakes change geometry too, so the difference is not an isolated shader-loop
cost. These single-run measurements are encouraging, not a worst-case frame guarantee.
The initial temporary executable lacked adjacent shader assets; that failed run was
discarded and the asset path corrected before measurement.

Before increasing emitter count or adding expensive foam, measure the actual river and
browser with multiple dynamic boats. If cost warrants it, spatially shortlist objects
for queries/render patches while retaining all live simulation state. Do not reintroduce
a map-sized solver or delete offscreen wakes just to optimize drawing.

## Accuracy and visual follow-up

The useful accuracy contract for this arcade mode is consistency: the drawn crest,
normal, propagation and hull contact should agree, and identical approaches should give
repeatable outcomes. The shared analytic recipe and CPU/GPU probes are strong choices.
Signed addition is internally consistent, but fixed-speed ridges, prescribed lifetimes,
no source energy budget and no shore blocking are deliberate simplifications. Current
does not transport the wake geometry. Nothing in this review reopens the retired
physical-field acceptance gates.

The stuck-wave report remains undiagnosed. In the recipe, longitudinal support is fixed
while crests move sideways; bent spans spread/overlap, so stationary-looking patterns are
plausible. Straight-span continuity does not establish curved-front continuity. Track
individual crest motion through a tight turn in a stationary camera before changing it.

For the subsequent appearance pass:

- Preserve the accepted large launch shapes initially. Improve their readability as
  moving water with narrow crest foam, a discernible trough, face lighting and restrained
  reflection detail tied to the same wake coordinates/age.
- Replace the legacy `clamp(wake.x / .38, 0, 1)` whitening in the fragment shader. With
  1.8 m crests it saturates across broad positive faces. Use crest location/steepness and
  moving breakup rather than whitening most of the ramp or adding stationary noise.
- Address geometry where it matters. A 2 m outer face on the 1.5 m GPU mesh has very few
  samples; current CPU captures visibly show triangular ridges and GPU captures retain
  broad mound-like forms. Test a bounded finer near patch or crest-aware geometry before
  adding detail everywhere. Analytic normals alone do not improve the silhouette.
- Recheck geometry/normal agreement across the near-patch distance fade, curved spans,
  crossings and land boundaries. Keep nearest-neighbour presentation and compare both
  stills and motion; appearance must help the player anticipate takeoff and landing.

Recommended sequence: define flight/landing/capsize behavior and height-aware contacts;
make one other boat respond dynamically; validate achievable trick/crossing outcomes;
then improve wake appearance and resolve the stuck-motion report. This is a review
recommendation for the user's gameplay goals, not an implementation authorization.

## Verification and artifacts

2026-10-22, M1 Max/macOS: Debug configure/full build and optimized review-target build
pass. **All 83 active CTests pass in 95.61 s, including 20 active GPU cases**; the fifteen
retired field cases remain disabled. The initial sandboxed suite was interrupted at GPU
startup and the complete suite was rerun with desktop access. Current CPU object time-9
and GPU loop-end captures were inspected; the existing continuity river capture was also
inspected as historical visual evidence. No new manual driving, audio review, Windows,
browser, or Instruments trace is claimed. There is no unexplained measured spike requiring
deeper attribution in this review.

Temporary probe sources, executables and logs: `build/wake-design-review/`. Full logs:
`/tmp/proteus-wake-review-{configure,build,optimized-build,desktop-ctest}.log`.
The probe sources include the existing review fixture and link the corresponding built
target's objects; they are local review artifacts, not new registered regression tests.
Future implementation should turn the chosen gameplay outcomes into lasting tests.
