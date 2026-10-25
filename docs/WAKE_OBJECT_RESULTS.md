# Bounded arcade wake objects

Current tuning, 2026-10-24: [stability/air-roll study](ARCADE_STABILITY.md), with a 1.2 m
default crest scale. Measurements below describe the earlier 1.8 m implementation.

2026-10-22. Owner: [wake plan](WAKE_REWRITE_PLAN.md). Originally implemented as the third
alternative, `--wake-objects`, and now the free-boating default. The user redirected this work toward tall, fun launch
waves, bounded per-boat objects and tick lifetimes. The earlier physical-field and
compact-quadrature experiments remain available for comparison; their physical
acceptance requirements do not gate this arcade model. `--player-wake` selects legacy D3.

Current status: implementation, deterministic jump/crossing/stress checks, CPU/GPU
recipe comparison and scripted desktop review are complete. The user enjoys the initial
huge-wave mode; the subsequent [continuity revision](WAKE_OBJECT_SMOOTHING.md) shortens
pieces and smooths their transitions. The user subsequently reports it is very fun and
requests the default switch. **Next: investigate occasional stuck-looking waves.**
See [default promotion](WAKE_OBJECT_SMOOTHING.md#default-promotion) for scope and verification.
The subsequent [arcade review](WAKE_ARCADE_REVIEW.md) assesses gameplay, accuracy and
performance; the [wake plan](WAKE_REWRITE_PLAN.md) owns its recommended next steps.

## Play and controls

```sh
cmake -S . -B build/review -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/review --target wake_velocity wake_objects_review -j
./build/review/wake_velocity --wake-objects --profiler
./build/review/wake_velocity --wake-objects-crossing --profiler
./build/review/wake_velocity --wake-objects --wake-height 1.2 --wake-life-ticks 1440
./build/review/wake_velocity --wake-objects --location river
```

Normal steering/thrust controls apply. `R` resets the boat, the shared object world,
and the optional crossing craft. `--demo` uses the existing own-trail loop. The blue
`WAKE OBJECTS` diagnostic label was removed on 2026-10-24; exit statistics remain available. The
crossing option enables object mode and adds an opaque scripted craft following a
four-waypoint loop in the basin. Both boats emit from their actual transformed stern
and sample the same combined wake surface. The review craft shares water interaction.
At this initial checkpoint it did not add hard hull-to-hull collision with the player;
[the later arcade pass](WAKE_ARCADE_IMPLEMENTATION.md) adds mutual impacts and capsize recovery.

- `--wake-height`: .1–3 m, default **1.2 m**, the full-speed single-piece crest scale.
  Speed, stern wetness, longitudinal blending and age modify actual local height.
  Overlapping pieces add directly, so a crossing can exceed this setting.
- `--wake-life-ticks`: 240–1920, default **1920 ticks / 16 s** at 120 Hz.
  Pieces grow over .65 s of local age with a .3 s birth gate, then fade along the
  span during the final quarter, finishing before hard expiry.
- Object mode supports basin/river free boating, plus ambient waves and
  authored current/scripted-wave options. The extra crossing craft is basin-only.
- Field mode, explicit D3 `--player-wake`, course, replay and ghost combinations are
  rejected. `--no-player-wake` disables the default physical wake; when combined with
  explicit `--wake-objects`, the explicit object mode remains selected.
  Tuning flags without object mode and out-of-range settings are rejected.

For a gentler first review, use `--wake-height 1.2`. At the default setting the measured
crossing launch is intentionally substantial; the existing hull/contact response supplies
the jump, without a separate scripted launch impulse.

## Representation and ownership

`boat_wake_objects.[ch]` is a small caller-owned shared world: **5320 bytes** on the
development Mac, with no per-tick allocation or map-sized array. It supports four
emitter IDs, thirty-two independent pieces per emitter and one emission per 60 ticks.
The maximum lifetime fits those slots, so continuous emission expires an old piece
before its slot is reused. Slots belonging to another boat are never stolen. Gaps in
tracking, dry sterns and low speed restart the source span instead of joining an
airborne or teleported path. Counters expose live/peak/emitted/expired objects.

Each piece keeps its stern-span midpoint, chord direction, support length, smoothed span-average amplitude,
inverse source speed and integer birth tick. Independent pieces cannot fold a connected
ribbon at a turn. They travel outward from both sides of the span at 2.4 m/s. The
cross-section has a narrow outer launch face, broader inner landing slope and smaller
signed trough. A smooth longitudinal window blends adjacent straight-running pieces.
Tight turns may reinforce their overlapping pieces; there is no overlap normalization.

Wake–wake interaction is signed addition: crests reinforce, troughs subtract, and
objects pass through each other. There is no pairwise wake collision solver. Expiry
is intentional gameplay behavior. Camera movement or simulation distance never clears
a live piece. The fixed pool bounds query work independently of map extent.

The app advances the world once per fixed tick, advances each participating boat, then
tracks its wetted stern. Physics uses the world's current integer-tick age, and GPU
records carry the same small relative ages; they do not subtract large absolute float
times. CPU queries return height, analytic gradient and vertical velocity. The ordinary
water query combines these with ambient waves/current and supplies the result to hull
supports, particle attachment and scenery poses.

`BoatWater.objects` is a borrowed const pointer. The application owns its lifetime and
resets it explicitly; copying water settings does not copy or own this shared world.
D3 still owns its separate by-value history. The renderer uploads a bounded two-row, 4096-byte object-record texture with a separate
object-mode selector; D3 keeps its original 64-entry uniform arrays. Both production water shader stages
use the same ridge recipe as CPU contact. There is no height-grid upload. Existing CPU
surface tessellation and the GPU's bounded 1.5 m near-water patch consume these queries.

## Initial behavior and cost (historical, before continuity revision)

The figures below describe the initial one-second/64-piece version. Current resource,
behavior and timing evidence is owned by the [continuity follow-up](WAKE_OBJECT_SMOOTHING.md).

Development Mac / M1 Max, macOS 26.6.2, Apple clang, optimized `RelWithDebInfo`, native
320x200 GPU, no AA. Other desktop activity was present; these observations are not an
exclusive-device or worst-case guarantee.

- An actual hull crosses a seeded stern-span wake, compared with a calm-world twin
  using the same wave-aware contact path. It reaches **7.152 m above the twin**, a
  **10.28 m/s** upward launch and **316 dry-stern ticks** during the five-second
  encounter. It remains finite and recovers in neutral after the wake expires.
- The actual single-player loop reaches **5.625 m hull height**, with **521 dry-stern
  ticks** over twenty seconds. Actual stern tracking emits seven live pieces in the
  time-9 capture; the two-boat time-12 capture has sixteen live pieces.
- Two orthogonal single-piece crests add to more than 3 m. Independent height,
  gradient and vertical-velocity sums agree; opposite signed pieces cancel. On the same
  actual diagonal hull approach, one wake launches **5.723 m** above calm while the
  crossing launches **14.72 m**, confirming the larger jump rather than height addition alone.
- A three-minute, four-emitter tight-loop stress at origins separated by 100 km reaches
  exactly **64 live objects**, expires **652 pieces**, and retains the same 2728-byte
  world. This is a query/storage test, not a four-boat complete-render benchmark.
- Production VS and FS recipes agree with CPU height/gradient at relative L2 below
  **6.4e-7 per component**, including oblique pieces, overlap, and a full 64-entry
  uniform array with the two local pieces in its last slots. A looped helper expression
  initially failed overlap comparison; the final inline polynomial recipe passes.

The optimized GPU diagnostic renders all warmup frames, then measures the last 120
frames after eighteen simulated seconds. Every measured frame includes two actual
boat/world/source ticks, all hull/particle queries, complete scene construction and
rendering, and `glFinish`. It excludes window presentation, audio and frame limiting.

| One-boat measured workload | Median | p95 | Maximum |
| --- | ---: | ---: | ---: |
| Two complete physics/source ticks | .1333 ms | .1388 ms | — |
| Complete renderer | 1.4556 ms | — | — |
| Combined | **1.5911 ms** | **1.9526 ms** | **2.1502 ms** |

The separately measured startup first draw is **4.7839 ms**. The optimized CPU-only
diagnostic measures .126 ms median for two physics/source ticks. Renderer
cost includes baseline scenery and water: these numbers do not isolate an incremental
wake-only cost, establish a hard 2 ms bound, or cover a fully overlapping four-boat
render load. The visible diagnostic uses frame limiting and is not a performance sample.

## Initial implementation verification (historical)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/review/wake_objects_review
./build/review/wake_objects_review --gpu
./build/review/wake_objects_review --desktop
./build/review/wake_velocity --wake-objects --renderer cpu --time 9 --screenshot build/wake-objects-cpu-9.png
./build/review/wake_velocity --wake-objects-crossing --time 12 --screenshot build/wake-objects-crossing-gpu.png
```

2026-10-22 on the development Mac:

- Debug configure/build and optimized builds pass. All **97 CTest cases pass after
  correcting and rerunning the initially failing object GPU comparison**; the suite
  includes **23 GPU checks**. The final five object-mode checks also pass together.
- Portable-backend ASan/UBSan contracts, lifetime/capacity stress, actual hull crossing,
  neutral recovery and self-loop checks pass. The only compile warning observed during
  the first optimized dependency rebuild was an existing raylib `stb_vorbis` warning;
  final project-source incremental builds are warning-clean.
- Basin CPU/GPU time-9 and two-boat time-12 captures, river captures and the short
  240-tick lifetime variant were generated and inspected. The tall ridges agree in
  location across CPU/GPU basin captures; CPU geometry is visibly more faceted. The
  river scene retains its clipped water surface and short lifetime removes old pieces.
- The visible scripted diagnostic completes 1200 rendered frames / 2400 fixed ticks,
  including jumping and landing. This is scripted animation, not agent hands-on driving,
  listening, human artistic acceptance or user approval.

Logs and images are under `build/wake-objects-*`: `ctest.log`, `ctest-rerun.log`,
`ctest-final.log`, `review.log`, `gpu-review.log`, `visible.log`, `sanitized.log`,
`gpu-measured.log`, `cpu-9.png`, `gpu-9.png`, `crossing-cpu.png`, `crossing-gpu.png`, `river-cpu.png`,
`river-gpu.png` and `short-life.png`.

The Emscripten target also builds successfully (`build/wake-objects-web-build.log`);
this is build verification, not new browser gameplay or shader execution evidence.
Formatting, changed local documentation links and `git diff --check` pass.

Historical limits at this checkpoint: no shoreline reflection/occlusion, hard collision with
the scripted review craft, or per-boat user tuning. The subsequent
[arcade implementation](WAKE_ARCADE_IMPLEMENTATION.md) adds mutual contact with the basin craft
and a finer GPU near patch; the remaining statements below describe this earlier checkpoint. The player and optional review craft emit object wakes;
mapped scenery traffic does not yet emit into this pool. Four fully overlapping boats can
create much larger waves; there is intentionally no combined-height clamp. The 1.5 m
surface mesh can facet steep crests, especially in the CPU reference. New interactive
reset/key handling, browser gameplay and Windows performance remain unverified.
