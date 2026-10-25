# Arcade wake continuity follow-up

2026-10-24 current tuning: [stability and steering roll](ARCADE_STABILITY.md) lowers the
default scale to 1.2 m and adds deliberate air-roll control. The 1.8 m results below are
the dated continuity baseline; the first stability candidate awaits user play.

2026-10-22. Owner: [wake plan](WAKE_REWRITE_PLAN.md). The user enjoys the initial
arcade mode and its huge waves, but notices the individual pieces. This revision
tries shorter source spans and smoother joins while retaining the 1.8 m crest
setting, 1920-tick lifetime and signed cross-boat stacking.

Current status: implementation, continuity/jump/resource checks, production shader
and upload comparisons, inspected before/after captures and scripted desktop motion
are complete. Full-suite verification passes. The user subsequently reports this revision
is very fun and requests it as the default. **Next: investigate occasional stuck-looking
waves**, especially tight turns and the river. See [default promotion](#default-promotion).
The subsequent [arcade review](WAKE_ARCADE_REVIEW.md) also identifies flight/capsize and
boat-interaction limits, measures full-pool rendering and recommends the next gameplay
and appearance sequence; the [wake plan](WAKE_REWRITE_PLAN.md) owns the current next task.

## Changes

- Emit every **60 ticks / .5 s**, previously 120 ticks / 1 s. At 20 m/s a straight
  source span is about 10 m rather than 20 m. There are **32 slots per boat / 128
  total**, retaining the same maximum sixteen-second lifetime and four-emitter limit.
  No live piece is overwritten to make room.
- Filter speed/wetness-derived strength with a **.35 s response** and average it
  across the emitted span. A brief stern-support dip no longer sets the strength of
  an entire piece from one endpoint sample. Fully dry or slow boats still stop
  emitting and restart their source span; smoothing does not bridge an airborne path.
- Grow and fade along each span using its **local wave age**, the same age used for
  crest travel. Adjacent straight-running pieces therefore agree about their stage
  of growth/expiry. A separate .3 s birth gate keeps a newly inserted piece from
  flashing into existence. Fade timing includes the supported age offset so the
  youngest edge finishes before the hard object expiry tick.
- Include the new envelope's spatial derivatives in both CPU contact and shader
  normals, and its time derivative in vertical contact motion. Crest/trough profile,
  propagation speed, longitudinal window, configured amplitude and cross-boat addition
  are retained. There is no combined-height normalization or clamp.

The world is **5320 bytes** on the development Mac, up from 2728 bytes. Rendering
uploads a **4096-byte object-record table** per frame, rather than enlarging the
legacy D3 uniform arrays. This is two rows of per-object parameters, not sampled
water heights. Object staging is another bounded 4096-byte stack buffer, in addition to the existing
D3 temporary arrays; GPU/driver
overhead is excluded. The table is owned and destroyed by the GPU scene and reused
after creation. The profiler accounts for its 4 KiB upload. D3 retains its existing
64-entry uniform arrays and rendering recipe.

## Behavior and continuity evidence

The extended `wake_objects_review` tests show:

- Five half-second emissions with repeated seven-tick wetness dips, including the
  emission tick, retain at least **1.612 m** source strength from a 1.8 m setting.
  Maximum adjacent strength difference is **.1308 m**. An instantaneous sample at
  that dip would prescribe .18 m. A subsequent fully dry interval emits nothing.
- New emission does not change the already-published height at its birth tick.
  Overlapping, fully grown straight-running windows retain the full crest with
  maximum sampled ripple **2.384e-7 m**. This protects height while increasing density.
- Growth/fade gradient checks differ from spatial finite differences by at most
  **.0002768**; analytic vertical motion differs from a 120 Hz centred difference by
  **.01225 m/s**. The latter also contains finite-difference truncation error.
- Production VS/FS selector-path probes cover birth/growth, mature, fading, oblique and overlapping
  pieces, plus all **128 records** with the local pieces in the last slots. The
  production record texture also reads back exactly as the current CPU-packed world.
- Three minutes of four-emitter turns at world origins 100 km apart reaches exactly
  **128 live objects**, expires **1308 pieces**, and keeps storage fixed.
- The seeded actual hull crossing still jumps **7.195 m above its calm twin**, with
  **10.35 m/s** upward launch and **310 dry-stern ticks**. It recovers after expiry.
  On the same diagonal approach, a single wake produces **5.483 m** above calm and
  crossed wakes **14.34 m**. The fixture's span now represents a half-second emission;
  these are current behavior checks, not identical-source numerical comparisons to r1.
- Actual own-trail looping reaches **6.162 m** hull height and **572 dry-stern ticks**
  over twenty seconds. The visually apparent packet boundaries are reduced in the
  inspected basin/river comparison, but tight turns and the 1.5 m mesh can still show
  separate ridges/facets. No claim of seamless geometry everywhere is made.

GPU captures compare identical scripted inputs at basin time 9 and river time 12.
The new water response also changes the boat path/camera, so they are not same-pose
image differences. The wave setting was not reduced to hide the artifacts. The visible
diagnostic completed 1200 rendered frames / 2400 fixed ticks, including jumping and
landing; this is scripted motion, not hands-on controls or user acceptance.

## Cost observations

M1 Max development Mac, macOS 26.6.2, Apple clang, `RelWithDebInfo`, native 320x200 GPU,
no AA. The same one-boat diagnostic renders all warmup frames and measures the final
120 frames after eighteen simulated seconds. Each sample includes two complete hull/
world/source ticks, queries, record upload, scene construction/rendering and `glFinish`.
Audio, presentation and frame limiting are excluded. Other desktop/test activity was
present; this is not exclusive-device or worst-case timing.

| Workload | Before median | First after median | Cached-startup repeat median |
| --- | ---: | ---: | ---: |
| Two physics/source ticks | .1373 ms | .1480 ms | .1464 ms |
| Complete renderer | 1.9021 ms | 2.2717 ms | 2.2941 ms |
| Combined | **2.0453 ms** | **2.4188 ms** | **2.4479 ms** |
| Combined p95 | 2.2159 ms | 2.6322 ms | 2.6500 ms |
| Combined observed max | 2.7846 ms | 2.8194 ms | 3.3934 ms |

Observed combined cost grows about **.37–.40 ms**. The measured new loop has twenty
live pieces instead of nine; changing contact response also changes camera/scene work.
These samples do not isolate the extra GPU-record cost or establish a hard 2 ms bound.
Four fully overlapping boats and other GPUs remain unmeasured.

The first draw with newly compiled shaders took **261.65 ms**, outside warmed samples.
Repeating with cached shaders took **5.01 ms** (old cached first draw: 4.56 ms). This
startup observation must not be presented as a warmed frame spike or silently discarded
from first-use considerations.

## Commands and verification

Existing play flags are unchanged; the optimized executable includes the new behavior:

```sh
cmake -S . -B build/review -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/review --target wake_velocity wake_objects_review -j
./build/review/wake_velocity --wake-objects --location river --profiler
./build/review/wake_velocity --wake-objects-crossing --profiler
./build/review/wake_objects_review
./build/review/wake_objects_review --gpu
./build/review/wake_objects_review --desktop
```

2026-10-22 verification, development Mac:

- Debug configure/build and optimized builds pass without project compiler warnings.
  The full suite passes **all 97 CTests**, including **23 GPU checks**, in 759.43 s.
  The final five object-mode cases pass again after adding the selector-path probes
  (10.81 s); the full record table is exercised without enlarging D3's uniform arrays.
- Portable ASan/UBSan passes source smoothing, continuity, capacity/lifetime stress,
  actual jumping, neutral recovery and own-trail checks. Emscripten builds successfully;
  this is build evidence rather than new browser gameplay/shader execution.
- Before/after basin and river GPU captures and the current basin CPU capture were
  inspected. CPU and GPU ridge locations agree; CPU tessellation still shows facets.
  The visible scripted review completed 1200 frames / 2400 physics ticks.
- C formatting, changed local documentation files/anchors and `git diff --check` pass.

No browser gameplay, Windows timing, new hands-on key/reset review or audio listening
was performed. The user has subsequently reviewed the revision, enjoys it, and reports occasional
stuck-looking waves.

Artifacts under `build/wake-smoothing/`: `before.log`, `after.log`, `cost-before.log`,
`cost-after.log`, `cost-after-cached.log`, `check-optimized.log`, `ctest.log`,
`ctest-focused.log`, `sanitized.log`, `web-build.log`, `desktop.log`,
`basin-{before,after}.png`, `river-{before,after}.png` and `basin-after-cpu.png`.


## Default promotion

2026-10-22: the user reports the continuity revision is very fun, notes waves sometimes
appear stuck, and requests wake objects as the new default. Free boating now selects
objects in basin, Eriksberg and river, including bundled app startup. `--wake-objects`
remains an explicit selector; `--player-wake` selects legacy D3, `--wake-field` selects
the native basin field, and `--no-player-wake` selects the visual-only trail. Existing
course/replay/ghost water stays unchanged. Tuning flags now work with the implicit default.
Explicit incompatible mode combinations still fail as before.

This changes mode selection, not wave propagation. Fixed longitudinal support with
sideways crest travel and overlap on turns are possible contributors to the stuck-wave
impression; the report has no confirmed reproduction or diagnosis yet. Investigating it
is the next task, preserving the accepted tall-wave fun.

Default-switch verification, 2026-10-22, development Mac:

- Debug configure/full build, optimized boat build and Emscripten boat build pass.
- The three focused default/capture cases pass (1.32 s); implicit default and explicit
  object-mode CPU time-9 captures match byte for byte. The CPU capture and river GPU
  time-12 capture were inspected.
- Thirteen CLI selection checks pass: all locations, legacy D3, visual-only trail,
  course, default tuning, explicit object/opt-out ordering and incompatible modes.
- A visible 180-frame river `--demo` startup run completes, reporting 380 physics
  ticks and four emitted wake objects. This is scripted playback, not hands-on input
  or audio listening. New browser execution and Windows timing remain unverified.
- The original full suite passes all 98 cases, including 23 GPU checks, in 748.21 s.
  The subsequent user-requested [test workflow change](TESTING.md) disables retired
  field research by default and measures the retained suite separately. C formatting,
  changed local file links/anchors and `git diff --check` pass.

Artifacts: `build/wake-default/` owns the CLI logs, focused comparison, river GPU
capture, desktop log and browser build log. Full configure/build/suite logs are in
`/tmp/proteus-default-{configure,build,review-build,ctest}.log`.
Earlier measurements above remain historical evidence for the unchanged wake recipe,
not new measurements of this selection change.

## Subsequent arcade gameplay pass

[Arcade implementation](WAKE_ARCADE_IMPLEMENTATION.md) now owns flight/landing/capsize,
height-aware contacts, a mutual-impact basin rival, outcome sweeps and crest readability.
The tight-turn stationary-camera case tracks moving individual crests within persistent
similar-looking overlaps; the user says this sounds very similar to the reported symptom.
An exact reproduction and propagation/readability fix remain unconfirmed. Earlier test,
mesh and performance measurements in this document remain historical.
