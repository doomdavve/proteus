# Living river city plan

Status, reconciled 2026-10-31: execution steps 01–04 and follow-ups 04b–04d complete
(2026-09-19); boat-view audit 04e, surface pass 04f, and selected waterline pass 04g
complete (2026-10-17). Generated moored fleet and dressed berths 04h implemented
(2026-10-18); see [the generator/berth guide](HARBOR_BOATS.md). Shared actor evaluation
05 and the user-requested travelling fleet are now implemented, including a compact
passenger ferry brought forward from 09. See [harbor traffic](HARBOR_TRAFFIC.md).
People/cyclists 06 are the next numbered city task.
Flying gulls from step 11 were brought forward for the four-minute demo; the boat study
now adds 32 reactive harbor gulls in four groups (see [harbor gulls](HARBOR_GULLS.md)).
On 2026-10-25 the demo, its storyboard routes, the Eriksberg boating location and the baseline
audit were removed; this plan now serves the river boating scene. Entries below that mention
them are history.
Perched birds remain pending. The [river expansion](river/README.md) is a separate scenery
track, not completion of actor/ferry steps. [All plans](README.md).

The proposed [plugin location-life track](PLUGIN_PLAN.md#location-life-track-proposed-2026-10-30)
now specifies how marina layouts/population, traffic and ferry schedules, trams, wildlife and
building/street lighting can become scriptable. This plan owns native actor/artistic work;
the plugin plan owns bindings, composition and live-edit policy. Those bindings, trams and
seals are not implemented; this does not change step 06 as the next numbered city task.

## Distant shadow follow-up — 2026-10-31

Implemented: preserve the detailed gameplay shadow map and add a wider map for buildings
approaching along the river, with near/far blending and a fade at the outer boundary.
[Renderer contract](3D.md#gameplay-shadow-coverage) and
[performance evidence](BOATING_PERFORMANCE.md#distant-shadows--2026-10-31) own the details.
The production city shader now has controlled-depth probes for far coverage, near-map
precedence, both transitions and the disabled-far fallback.

Verification, 2026-10-31, Apple Silicon/macOS: native Debug build and all 364 enabled
CTests passed with GPU tests on (15 retired experiments disabled; 76.40 s). River,
outer-map and basin GPU frames were inspected; a 360-frame windowed river demo exited
successfully. The flat-water river comparison measured about +0.58 ms CPU submission
and +0.10 ms GPU time at the median on M1 Max; the performance guide records full
figures and limits. Next for this follow-up: user play/review, especially the coarse
far silhouettes and transition during motion. Human acceptance, other platforms, rough
water/AA timing and release performance qualification remain unverified. People/cyclists
06 remains the next numbered city task.

## Direction

The original artistic brief is an inhabited waterfront at blue hour: apartments above active
ground floors, connected streets, people with destinations, and a working harbour beyond.
Preserve the crisp, integer-scaled image and the contrast between warm rooms and cool outdoor space.
Prioritize depth and readable activity at 320x200; inspect 320x240 and 320x180 too.
The current demo and boat scenery use the sunny daytime lighting added on 2026-10-17;
blue hour remains the earlier design reference, not the current default lighting.

The city should feel inhabited even in a still frame. Movement then supplies small stories:
a cyclist passes a waiting bus, someone walks home, a room lights up, a ferry slides behind
the waterfront. Most of the scene remains calm. Avoid making every light or object animate.

## Working and committing one step at a time

The user has requested incremental commits. Complete the requested step, verify it,
update this document's progress log, and create a focused local commit. Commit creation
is already authorized; do not ask again at each step. Pushing is not part of this workflow.
If a range is requested, make a separate commit for each completed step and continue
through the range. Otherwise stop after the assigned step.

The numbered steps below are the execution units; the six stages later in this document
describe their artistic goals. Make routine implementation choices autonomously. Preserve
unrelated user changes and stage only files belonging to the step. Keep captures and
profiling artifacts under `build/living-city/`, outside version control. Do not stage the
three existing root-level PNG captures.

Before each commit, run the build and CTest, plus the visual, motion, and profiling checks
relevant to the change. Record commands, results, inspected artifact paths, and limitations
in the progress log. If a required check fails or cannot run, investigate and record the
specific blocker; do not describe the step as verified or complete. Keep useful work and
report what remains. A purely documentary step does not require a new manual demo run.

| Step | Deliverable and completion evidence | Status |
| --- | --- | --- |
| 01 | Capture and inspect the existing short/long routes; audit map coverage, heights, empty parcels, and camera views. Record a prioritized list of exact areas to change, with still-frame evidence and a performance baseline. | Complete |
| 02 | Repair nearby street enclosure and give exposed ground areas an identity, using step 01 findings. Compare matching before/after stills and check camera clearance. | Complete |
| 03 | Extend surrounding blocks and the distant skyline where needed. Inspect the whole route for visible map edges, new gaps, and depth continuity. | Complete |
| 04 | Finish one visible street corner: kerbs, entrances, shop/cafe, planting, furniture, and parked bikes/vehicles. It must read as inhabited in a still frame. | Complete |
| 04b | Bring the street-treatment expansion forward: distribute entrances, shopfronts, pavements, planted forecourts, parked vehicles, and quay furniture across the visible neighbourhood. Compare both whole routes, not just the cafe shot. | Complete |
| 04c | Separate asphalt, paving, quay concrete, and planting finishes; correct real-world scale and filter subpixel detail. Verify and commit independently of facade work. | Complete |
| 04d | Rework facade materials around the existing window geometry, with building-aligned brick, plaster, and panel variation. Inspect near and distant views without changing the low-resolution style. | Complete |
| 04e | Audit water from the playable boat: dock, basin mouth, river crossing, quay approach, and view toward the exposed reach. Record matching flat/wave stills, motion, shoreline and map-boundary issues, and a performance baseline. | Complete; see WATER_REVIEW.md |
| 04f | Reduce unnecessary distant wave geometry and give sheltered dock/exposed river water distinct visual surface character. Preserve current physical waves and handling; use the bounded brief in WATER_REVIEW.md. | Complete |
| 04g | Finish selected waterline edges: quay contact, fenders, ladders, moorings, and restrained local disturbance. Inspect alongside the boat and approaching shore. | Complete; selected wall fittings, see WATER_REVIEW.md |
| 04h | Dress two berths with a varied generated leisure fleet and a workboat; share wave poses, mooring attachments, reflection/shadow geometry, and forgiving player contacts. Establish reusable visual recipes for later boat variants. | Complete; see HARBOR_BOATS.md and progress log |
| 05 | Consolidate the actor evaluation needed for city life into the shared scene layer. Preserve existing behaviour and verify direct seeks before adding actors. | Complete; explicit-time shared snapshots and legacy street checks |
| 06 | Distribute a small cast of pedestrians and cyclists between street destinations and both quays, with readable silhouettes and deterministic paths. Avoid concentrating all life at the cafe. Inspect motion and occlusion. | Pending |
| 07 | Add varied vehicles and one authored interaction, such as a bus stop or yielding car. Verify spacing, stops, and hidden route wraps. | Pending |
| 08 | Add sparse room activity, coordinated lamp flicker, and selective vehicle lighting. Verify stills, motion, and direct-seek consistency. | Pending |
| 09 | Stage a ferry proxy in a visible river shot and validate scale, route clearance, and camera composition before detailed modelling. Preserve the music slot. | Compact passenger ferry implemented by user request; original large Stena proposal remains deferred |
| 09b | Add travelling small craft, continuous routes, visual wakes, shared poses, and forgiving moving player contacts. | Complete; see HARBOR_TRAFFIC.md |
| 10 | Finish the ferry silhouette, livery, deck lights, wake, and reflection, plus selected quay details. Review the complete harbour shot. | Daytime compact-ferry silhouette, livery, windows, navigation lights, wake and reflection implemented; night/deck lighting and final artistic review remain |
| 11 | Add gulls: a few perched birds and a small number of circling/gliding silhouettes. Keep flight paths deterministic and readable against the sky at 320x200. | Demo flock and 32 reactive boating gulls implemented; perched birds pending |
| 12 | Refine remaining weak areas after the city-wide coverage pass and add restrained atmosphere. Review both routes, all resolutions, seeking, and final performance. | Pending |

Steps 02–04 establish the city in a frozen frame before the new moving cast arrives.
User review of step 04 exposed a contrast problem: one rich corner makes the undressed
surroundings feel even emptier. Step 04b therefore brings the broad static-detail pass
forward from step 12. Coverage is the baseline; hero corners add variety on top of it.
Assess street approaches, both dock banks, foreground plazas, and the long-route overview.
Small-prop counts alone are not visual acceptance: planting and shared courts must read
as purposeful groups at the native resolution. Keep circulation and genuine open space.
After reviewing 04b, the user prioritized street, ground, and house materials over more
furniture. Steps 04c–04d address that before actor work; further density work can wait.
Gulls are a planned deliverable, with gliding and occasional wingbeats rather than a dense
particle flock. During step 01, note possible ferry framing; detailed proxy work belongs
to step 09 unless explicitly brought forward.

To start, use this prompt in the current or a fresh session:

```text
Read AGENTS.md, docs/LIVING_CITY_PLAN.md, and docs/WATER_REVIEW.md.
Complete execution step 06 only: add a small cast of pedestrians and cyclists.
Use the existing shared scene evaluation, GPU renderer, and retained geographic data.
Inspect deterministic motion, route clearance, and occlusion.
Follow the plan's verification requirements, record findings and artifact paths in its
progress log, and commit the completed step. Preserve unrelated changes. Do not implement
later steps. Report the commit, findings, and any remaining verification limitations.
```

For subsequent work, replace `06` and the task-specific sentence with the next step's
deliverable, or request a range such as `02–04, with one commit per step`.

### Progress log

Dated records preserve each revision's findings and verification. Their local “next” tasks,
old geometry counts, and limitations may be superseded by later entries; the status table
above owns the current execution order.

- 2026-10-18, **11 boating gulls**: added 32 birds in four groups for Eriksberg/river
  boating, including cruiser followers, low feeding passes, delayed shared alarm cues,
  personal space, individual flap/glide cycles, banking and pitch, and speed-sensitive
  player/vessel avoidance. Both renderers evaluate the same explicit-time paths; fixed
  the GPU player branch's early return so birds enter color/reflection/shadow passes.
  Lowered the nearby flock after inspecting the default boat camera. The basin remains
  clear and the four-minute demo retains its five birds and soundtrack camera.
  See [harbor gulls](HARBOR_GULLS.md) for implementation and reproducible commands.
  On the development Mac, 2026-10-18: Debug configure/build passed without project
  compiler warnings; all 52 CTests passed, including 14 GPU checks
  (`PROTEUS_GPU_TESTS=ON`, final suite 110.45 s). Extended `harbor_traffic` with
  120 s of path/seek/spacing checks,
  a moving-player crossing, proximity/speed reactions, wing attachment, and boat-frame
  integration. Inspected final 320x200 CPU/GPU opening captures (t=0), CPU t=3, and a
  GPU+AA low-camera wave capture (t=6). Artifacts and build/test logs are under
  `build/living-city/gulls/`. Bounded 360-frame GPU+AA demo smoke runs completed for
  Eriksberg and river; the final river run uses the final flight revision. These are
  automated windowed runs, not human playback/input assessment.
  Limits: authored visual responses, no persistent bird AI, terrain avoidance, perching,
  bird audio, or physical contacts. Human motion/handling assessment and artistic acceptance
  remain open; the next numbered city task is still 06 people/cyclists.

- 2026-10-18, **05 shared actors / 09–09b travelling fleet**: brought water traffic
  forward at the user's request. Replaced the old one-way box launch with a yellow
  runabout on a closed dock circuit, a river cruiser, a motoring sailboat, and a compact
  24 m passenger ferry on an outer river corridor. Extended the existing generator with
  passenger windows, upper bridge, funnel, rails, and deck seating. This is an invented
  local passenger vessel; the original large Stena proposal remains deferred. Shared
  explicit-time snapshots now evaluate street actors, berths, vessels, navigation lights,
  and 64 historical-path wake segments. CPU/GPU, shadows, and reflections reuse poses.
  Four-sample wave attitude is kinematic; only player contacts use the 120 Hz simulation.
  Moving-hull response uses obstacle-relative velocity; the basin/course/replay baseline
  and main-demo music/camera timing remain unchanged. See [traffic guide](HARBOR_TRAFFIC.md).
  Verification: Debug configure/build and all 52 CTests pass (38 non-GPU plus 14 GPU;
  GPU subset also reruns its recording fixture). Complete routes checked at 12 Hz in both
  map crops; 4,160 s pairwise schedule, analytical motion/lap seams, seeks/wakes, full-hull
  player impacts and recovery pass. Seven generator presets / 35 variants pass. Existing
  street equations, four-minute gull framing, course recordings and handling remain green.
  Reviewed CPU opening and ferry, GPU launch at 0/32/158.16 s, ferry at 40/160/320 s,
  river overview, main-demo frames at 60/120/180 s, both full storyboard sweeps and
  matching 200/240 comparisons plus 180 framing. Captures/logs: `build/living-city/traffic/`, route comparisons under `routes/`.
  Opening CPU reference changes from `2445a29318b46e7b` to `80d44611b75ac3fb` after
  inspecting the distant traffic change. Windowed GPU-AA wave ferry and boating each ran
  600 frames; short/long storyboard playback completed. No runtime errors were reported.
  Main GPU profile (923 frames, 120 warm-up, no AA): CPU submission mean/p95
  2.168/2.259 ms versus previous 1.837/1.931 ms. Ferry wave review at 4× AA:
  mean/max 2.601/7.448 ms submission including downsample and near-water patch updates.
  These are CPU submission timings, not GPU execution. Four resident traffic meshes total
  1,264 triangles, with 213,120 bytes fixed CPU generator storage; lights reuse the cube,
  and wakes reuse the plane. The fleet submits four hulls per pass, twelve light boxes in
  color/reflection, and 64 foam planes in color, replacing the old box launch/wake draws.
  Limits: traffic remains on authored routes without avoidance or ferry stops; contacts
  move only the player. Foam adds no physical wave source (D1/D2 remain pending).
  Night/deck lighting and human artistic/handling acceptance remain open; no Windows or
  new Instruments run. Next numbered city task: 06 people/cyclists; additional ferry
  polish in 10 remains optional user-directed work.

- 2026-10-18, **04h generated moored fleet and dressed berths**: brought the harbor pass
  forward at the user's request. Added `boat_model.[ch]` visual recipes (skiff, runabout,
  cruiser, sailboat, workboat, speedboat), plus `waterfront_harbor.[ch]` with six leisure
  boats on a west-bank pontoon and a workboat alongside the east quay. Individual hull
  dimensions, end widths, cabin/rake, engines, mast, equipment, fenders, and colors vary
  without separate hand-authored meshes. The reusable generator is visual only; the
  player craft, basin handling, racing progression, and physics D1/D2 remain separate.
  CPU/GPU share boat/pontoon wave poses, gangway, and attached mooring lines; GPU color,
  reflection, and shadow passes reuse nine uploaded meshes plus bounded cube transforms.
  Four water samples provide a stateless rigid pose, not a second boat physics solver.
  Stationary padded hull/pontoon/finger/gangway envelopes reuse forgiving player contacts.
  Both map crops, spawn, and the central launch/review channel retain clearance.
  Added `harbor_review` close/overview captures and bounded 60 Hz playback; the
  [generator/berth guide](HARBOR_BOATS.md) owns its commands and parameter contract.
  Verification: Debug configure/build, 49 CTests including 13 GPU-context checks,
  six presets plus 30 parameter variants, invalid recipes, bounded geometry,
  direct-seek/wave/rope continuity, modeled pontoon anchors, and contact/reverse recovery.
  Storyboard/flock camera paths retain 2 m sampled clearance. The inspected opening
  CPU reference changes from `283e8c24b6393cac` to `2445a29318b46e7b`; CMake records it.
  Artifacts: `build/living-city/harbor/` contains final marina/workboat/overview images,
  `final-cpu-profile.log`, `final-ctest.log`, and windowed smoke logs. `review/` contains
  CPU/GPU flat/wave close views and `routes/` captures for both complete storyboard
  routes, 320x200/240 sheets, 320x180 framing, and one-second sweeps. Reviewed the full
  sweep sheets and close views; final berth fittings were checked again in close captures.
  Both windowed storyboard routes, marina/workboat wave passes, and an Eriksberg
  GPU+AA wave/demo smoke run completed. The 923-frame main GPU profile (120 warm-up)
  measured mean/p95 CPU submission 1.837/1.931 ms at 320x200 without AA. The 300-frame
  4x marina wave pass measured 1.256 ms mean, 5.665 ms maximum, including first-frame
  work; these are development-machine submission samples, not GPU execution/FPS claims.
  Limits: no human handling/input/audio assessment, Windows playthrough, or new Instruments
  trace. Thin rigging is less readable in the unsupersampled CPU reference. Berths are
  fixed envelopes rather than moving rigid-body contacts; distant water and reflections
  retain existing approximations. User artistic acceptance remains open; next actor task is 05.

- 2026-10-17, **04g selected waterline edges**: added 80 wall equipment stations across
  the existing map-validated quay sections: 26 access ladders and 54 paired fender/cleat
  groups with suspension ropes, plus localized wet/toe bands. Moved posts clear of ladder
  openings. Fittings project at most 0.42 m into the water and add no channel-spanning
  geometry. GPU batches them into the existing opaque mesh for colour, shadow, and
  reflection; CPU draws the same wall fittings. Added 1,168 boxes / 14,016 triangles,
  with no additional GPU mesh submissions. Water material, waves, handling, and music
  are unchanged. Static fittings need no time/history state.
  Debug configure/build and all 43 CTest checks passed (12 GPU). Attachment validation
  checks every below-quay fitting vertex against actual retained quay segments within
  0.5 m. Route clearance and repeated/direct CPU seeks pass. Inspected matching close
  CPU/GPU views, both boat-route water presets, and 22 timeline framing captures at
  0/2.5/5/10/15 s in 320x200 and 320x240, plus 320x180. The opening CPU image changes
  44 quay pixels; reviewed and updated its checksum to `283e8c24b6393cac`. Both legacy
  GPU storyboard routes completed windowed passes (`storyboard-motion.log`).
  Artifacts: `build/living-city/waterline/`, including `detail-{cpu,gpu}.png`, the
  diagnostic `detail_capture.c`, `framing.png`, `gpu-flat/`, `gpu-waves/`, `cpu-flat/`,
  and `windowed-waves/`. GPU+AA flat submission mean/p95: 5.365/5.867 ms; wave:
  5.879/8.625 ms. Windowed wave circuit completed at 60 Hz: 5.497/6.067 ms submission.
  These are CPU submission samples, not GPU execution measurements or speedup claims.
  No Instruments trace was warranted for this small static geometry pass. Arbitrary
  cameras and interactive input/audio were not manually assessed. Moored craft and
  vessel mooring lines remain for the later harbour actor pass; this step uses ropes
  supporting wall fenders. No new foam is justified on these sheltered stationary walls.

- Step 04f (2026-10-17): replaced the GPU's all-map dense wave mesh with an exact
  shoreline-clipped near patch and the existing 266-triangle flat far surface. A fixed
  1.5 m world grid recentres in 12 m steps; wave displacement is full within 48 m and
  fades to zero at 80 m. Both passes share boating albedo and analytic wave normals.
  The position-only dynamic buffer retains capacity across updates. Added smooth,
  shader-only shelter/exposure variation, filtered directional ripples, restrained
  highlights, and native-pixel filtering that accounts for AA. Physics, wave presets,
  boat/replay poses, CPU reference images, timeline water, and music remain unchanged.
  Full implementation details and reproduction are in [WATER_REVIEW.md](WATER_REVIEW.md#04f-implementation-boat-level-surface-and-bounded-geometry).
  Review-route peak: 36,316 near triangles plus 266 far, versus 1,069,001; allocated
  wave vertex payload 2,359,296 bytes (2.25 MiB), versus 89,796,084 bytes. This is about
  96.6% fewer triangles and 97.4% less vertex payload, with one extra main-view water
  submission. It is not a driver-memory measurement or a GPU speedup claim.
  Debug configure/build and all 43 CTest checks passed (12 GPU). New checks cover patch
  coverage, bounded height interpolation, invariant overlap at recentering, matching
  near/far GPU colour, and smooth exposure. All four pose CSVs and 32 CPU keyframes
  match 04e exactly. Captured both full water presets and both camera heights, inspected
  before/after stills and motion strips, and completed the 60 Hz windowed wave route.
  Artifacts: `build/living-city/water-surface/`, including three movies, comparisons,
  `default-waves/`, `windowed-waves/`, and the Xcode `water.trace` plus XML/summary.
  At 10 Hz, wave submission mean/p95 was 5.815/8.472 ms versus 04e's 5.341/5.818:
  dynamic clipping/upload adds occasional CPU cost. At 60 Hz it was 5.388/5.922 ms,
  max 9.268 ms (4,695 frames, 120 warm-up), with 128 patch updates. Those different
  sampling rates are separate observations. Time Profiler sampled about 276 ms in
  patch generation across the diagnostic run; see the audit for context and limits.
  Python compilation/assembly, ffmpeg encoding, and `git diff --check` passed.
  Remaining: planar reflections, crop coverage, the expensive CPU wave reference,
  measured GPU execution/driver memory, and arbitrary-camera shimmer review. Step 04g
  is next; physical currents/crossing wakes remain separate physics work.

- Step 04e (2026-10-17): completed the boat-level audit in
  [WATER_REVIEW.md](WATER_REVIEW.md). Added `boat_water_review` and its Python capture/
  assembly wrapper: a driven 78-second dock/river/quay circuit, eight named stops,
  default/close cameras, flat/wave presets, CSV poses/clearances, and timing/geometry
  baselines. Conservative hull clearance is checked at every 120 Hz tick; both routes
  pass. Artifacts: `build/living-city/water-review/`, including 64 named GPU/CPU images,
  annotated comparisons, a route SVG, adjacent-frame filmstrips, and two 10 Hz movies.
  Reproduce with `python3 tools/boat_water_review.py --sequence --movies` (ffmpeg optional).
  Inspected all eight comparison sheets and selected wave/adjacent-frame views; the
  complete windowed GPU+AA wave circuit also finished successfully.
  Debug configure/build and all 42 CTest checks passed (11 GPU), including two new route
  guards. Python compilation, assembly, movie encoding, and `git diff --check` passed.
  GPU submission mean/p95: flat 5.221/5.605 ms, waves 5.341/5.818 ms. CPU: flat
  18.074/20.346 ms, waves 869.671/907.149 ms, with fewer CPU samples; see the audit's
  measurement limits. Water grows from 266 to 1,069,001 triangles in wave mode, with
  about 85.6 MiB of calculated GPU vertex payload alone. No renderer/handling changes.
  Next 04f: reduce distant wave geometry and add smooth visual shelter/exposure variation.
  Map/crop context and waterline dressing are separate findings. GPU execution timing,
  driver memory, 60 Hz shimmer across arbitrary player views, and exact GPU seek parity
  remain unverified; the review artifacts sample at 10 Hz.

- Water-plan review (2026-10-17): brought boat-level water coverage ahead of street actors.
  The current boating prototype already has shared analytic waves, buoyancy, wake and
  spray; currents and crossing wakes remain physics Phase D proposals. Added 04e–04g
  and the water sequence below. Initial low-camera GPU flat/wave captures and a CPU
  reference are under `build/living-city/water-review/`. These are dock samples, not a
  completed river-route audit; 04e remains next. No renderer or handling changes.
  Inspected `dock-flat.png`, `dock-waves.png`, and `dock-cpu.png`: GPU reflections
  establish the quay/crane relationship, and waves plus player foam already provide
  surface motion. The flat CPU reference has no scene reflections. The two GPU views
  use different times (0 and 2.9 s), so they are examples, not a matched comparison.
  Debug configure/build, all 40 CTest checks (including 11 GPU checks), and
  `git diff --check` passed. Full river coverage and live motion review remain for 04e.

- Step 04d (2026-09-19): stopped loading the old facade bitmap with its second painted
  window grid. Existing window geometry, positions, and room colours are unchanged.
  Added red/buff brick, warm plaster, and cool panel finishes, chosen consistently from
  the retained per-building palette (artistic assignments, not surveyed materials).
  UVs now measure metres along each wall and height above the quay; paired triangles
  share an origin, avoiding world-axis stretching on angled facades. Brick courses are
  26 x 9 cm, panel modules 1 m wide, and floor bands follow the window rows' 3.2 m spacing.
  Small joints/grain fade with pixel footprint; broad palette changes and a low masonry
  base carry the distant appearance. No new windows, geometry, draw calls, or textures.
  Vertex RGB holds the finish colour and alpha holds an opaque material ID. Main and
  reflection passes explicitly enable/reset facade mode; shadows retain their geometry.
  Ground materials, roofs, lighting, camera paths, music, actors, and CPU images are unchanged.
  Extended the GPU probe (renamed `gpu_surface_materials`) to verify all facade families,
  metre scale on angled faces, shared UVs across wall triangles, material consistency,
  opaque output, distinct responses, and near-detail/far-shift behaviour.
  Debug configure/build and all 15 CTest checks passed. The facade batch contains 2,806
  brick, 1,704 plaster, and 1,424 panel triangles; isolated probes retain distinct near
  detail and show at most one 8-bit brightness-level change for the 2 cm far-camera shift.
  Inspected matching 320x200/240 captures and full one-second sweeps on both routes,
  plus 320x180 framing checks. Both windowed GPU route smoke runs completed.
  Artifacts: `build/living-city/facade-materials/`; comparisons put ground-material-pass
  baseline on the left and current facades on the right. Useful pairs include
  `compare-short-0.png`, `compare-short-2.5.png`, and `compare-short-5.png`. Reproduce with
  `python3 tools/living_city_review.py --output build/living-city/facade-materials --compare build/living-city/ground-materials --sweep`.
  CPU submission mean 1.300 ms / p95 1.380 ms (923 frames, 120 warm-up), versus 04c's
  1.341 / 1.428 ms; these noisy Debug timings do not establish a speedup or GPU execution
  cost. Live human motion observation and exact whole-scene GPU seek parity with the
  existing history-dependent light selection remain unverified. Material evaluation has
  no time/history state, and the CPU reference checksum remains unchanged.
  Remaining: roofs still use the old repeating texture; windows remain simple flat glazing,
  with no modeled recesses, frames, or balconies. Fine masonry intentionally disappears
  at this resolution; broader architecture and future activity must supply further detail.

- Step 04c (2026-09-19): replaced the ground's shared 25 m bitmap repetition with four
  world-space procedural finishes: asphalt, 80 x 40 cm pavers, 3 m quay concrete panels,
  and irregular planted beds. Small grain and colour variation fade with projected pixel
  footprint; thin seams use integrated pixel coverage and converge to their average at
  distance. The original large patchwork no longer crosses streets and open ground.
  Material identity is assigned from the source/static-kit palettes, ground height, and
  upward normals. Raised pavement and planted court tops move into the existing ground
  batch; they are not duplicated. Its UV channel carries constant material IDs, while
  facade and roof UVs remain unchanged. Both colour passes use identical material logic;
  the shadow pass retains the same triangles. No added mesh submissions or textures.
  GPU `quay.png` loading is removed, but the legacy asset/generator are retained.
  Lighting, geometry, camera paths, music, actors, and the CPU visual reference are unchanged.
  The new GPU material test checks class coverage and isolated near/far shader behaviour,
  including a 2 cm far-camera shift. It reports 11,160 asphalt, 1,778 paving, 266 concrete,
  and 753 planting triangles; all four retain near detail, with far-shift pixel changes
  at most one 8-bit level in the probe. This is not a proof of zero shimmer in every shot.
  Debug configure/build and all 15 CTest checks passed, including GPU review and
  `gpu_ground_materials`. Inspected both whole-route one-second sweeps, matching captures
  at 320x200/240, and 320x180 framing checks. Both windowed GPU routes completed.
  Artifacts: `build/living-city/ground-materials/`, with baseline-left/current-right
  comparisons against `step04b`; `compare-short-0.png`, `compare-short-5.png`, and
  `compare-short-15.png` isolate useful ground views. Reproduce with
  `python3 tools/living_city_review.py --output build/living-city/ground-materials --compare build/living-city/step04b --sweep`.
  CPU submission mean 1.341 ms / p95 1.428 ms (923 frames, 120 warm-up), versus 04b's
  1.274 / 1.340 ms. These are noisy Debug CPU timings, not GPU execution measurements.
  Live human motion observation and exact whole-scene GPU seek parity with the existing
  history-dependent light selection remain unverified. Ground patterns themselves have
  no time/history state. Their filtering deliberately makes broad distant surfaces quiet;
  tiny grain and joints should not substitute for future land-use/composition detail.
  Remaining material work: the facades still contain a second painted-window pattern and
  use a shared world-aligned texture scale; roofs remain repetitive. Mapped footpaths and
  roads share one importer colour and currently both use asphalt; authored raised walks
  have the separate paving finish. Broad open parcels still need future use/composition work.

- Step 04b (2026-09-18): expanded the cafe kit into map-constrained static dressing.
  Added 318 exterior entrances and 15 shopfronts beyond the cafe, 210 three-metre
  pavement sections, 320 quay sections with kerbs/bollards and intermittent rails,
  39 shared planted or parking forecourts, 25 parked cars, and 83 additional trees.
  Totals including the cafe: 101 benches and 28 bicycles. These are artistic land uses,
  not new geographic facts. Footprint samples reject roads, water, roofs, and existing
  planting; crane supports and the original cafe are reserved. Shared courts also avoid
  previously placed furniture. The surface-query cache is discarded after construction.
  The first small-prop-only review still looked sparse, so this pass includes larger
  planted courts and seating groups that survive wider views.
  All 7,104 boxes/beams still use the same two resident GPU batches: five submissions
  across main/reflection/shadow passes, unchanged from step 04. The non-indexed vertex
  payload is about 8.78 MiB (255,744 vertices at 36 bytes), plus its CPU-side mesh copy;
  this is a calculated payload, not measured driver memory. No new local lights, actor
  behaviour, camera changes, music changes, or CPU image changes.
  Added a display-free coverage test for west/east/north/south distribution, finite bounded
  geometry, emissive frontage coverage, stable initialization, and clearance queries.
  Both routes retain 2 m clearance at 120 Hz and the CPU direct-seek checks pass.
  Debug configure/build and all 14 CTest checks passed, including GPU review and the new
  city-coverage guard. Inspected matching before/after captures at 320x200/240, 320x180
  framing checks, and one-second sweeps for both complete routes. Both windowed GPU
  passes completed successfully. Captures and the profile log are in
  `build/living-city/step04b/`; `compare-short-5.png`, `compare-short-15.png`, and
  `compare-long-7.68.png` show baseline left/current right. Reproduce with
  `python3 tools/living_city_review.py --output build/living-city/step04b --compare build/living-city/step04 --sweep`.
  CPU submission mean 1.274 ms / p95 1.340 ms (923 frames, 120 warm-up), versus
  step 04's 1.317 / 1.421 ms; this noisy measurement is not evidence of a speedup and
  does not measure GPU execution. The standalone coverage check takes about 1 second,
  including one-time dressing construction. Live human motion observation, measured GPU
  memory/execution time, and exact GPU seek parity with history-dependent light selection
  remain unverified. Static dressing itself is initialized once and has no time/history state.
  Remaining: the wide concrete plazas still need stronger local identity; dark shadows
  hide small furniture, building facades/roofs remain repetitive, and there are no new
  people, moving cyclists, buses, gulls, or major harbour activity yet. This establishes
  a broader static baseline, not a claim that every visible parcel is finished.

- Step 04 (2026-09-18): added an authored cafe at the northwest corner of mapped way
  282574767 (local origin X -71.82 / Z 105.90). Includes raised pavement and kerb,
  striped canvas awning, readable KAFFE sign, warm display glazing and a north entrance,
  three tables/six chairs, three planters, bench, bin, menu board, three cycle stands,
  and two parked bicycles. The existing north garden and the traffic lanes remain clear.
  The destination reads during the opening approach, especially t=1–2.5 on the short
  route. Camera position, music, actor motion, and generated geographic assets are unchanged.
  Static scene-owned box/beam data lives in `src/waterfront_corner.[ch]`; the GPU uploads
  two batches and reuses them for colour, reflection, and opaque shadows (five additional
  mesh submissions with all passes enabled). One reserved shop light fits the existing
  32-light limit, keeping the street lamps and at least three headlight slots.
  The CPU reference remains visually unchanged; its checksum tests pass without updates.
  Camera clearance now includes all new props, sampled at 120 Hz for both routes.
  Debug configure/build and CTest passed 13/13, including GPU review. Matching captures
  and one-second sweeps for both routes were inspected at 320x200/240, with 320x180
  framing checks. Windowed short and long GPU passes completed. Final artifacts are in
  `build/living-city/step04/`; `compare-corner-2.png` pairs step 03 and step 04 at t=2.
  CPU submission mean 1.317 ms / p95 1.421 ms (923 frames after 120 warm-up frames).
  This does not measure GPU execution time. As before, live human motion observation
  and exact GPU seek parity with history-dependent light selection remain unverified.
  Small furniture naturally becomes a few pixels on the fly-through; the awning, sign,
  doorway and warm pavement carry the main visual identity. People and moving cyclists
  remain work for later steps.

- Step 03: enlarged the retained-data crop from X -400..550 / Z -400..500 to
  X -700..950 / Z -600..800 m. Building features increase from 204 to 523 without a new
  download or authored building footprints. The river assembly still verifies that its
  artificial closing edges lie outside the crop. Geometry is now 20,434 vertices /
  25,512 triangles; regeneration into `build/living-city/step03/repro` matches the bundled
  geometry and manifest byte-for-byte. The new context uses existing colour, shadow,
  reflection, and atmosphere passes without new per-object draw calls.
  Inspected matching before/after captures at all supported resolutions and one-second
  sweeps of both full routes (17 short / 32 long frames including endpoints). Added
  `--sweep` and compact paired views to the review script. No exposed rectangular crop
  edge was apparent in the reviewed sweep; real open water and park space remain.
  CPU frames at t=0/15 were reviewed before updating the reference checksum to
  `5ebe525b6d2f6f83`. Build and all 13 tests pass, including GPU review, CPU seek checks,
  and 2 m route clearance. Both windowed GPU routes completed.
  The denser t=5 skyline leaves only the top sky band exposed in both renderers; the
  semantic review now requires that top band and ordered exposed sky rather than two
  bands, retaining coverage limits and the independent orientation/depth fixture.
  Final artifacts: `build/living-city/step03/`, with baseline-left/current-right comparison
  sheets and `compare-short-0.png`, `compare-short-10.png`, `compare-long-30.72.png`.
  CPU submission mean 1.328 ms / p95 1.517 ms versus baseline 1.317 / 1.405 ms (Debug;
  noisy submission measurements, not GPU execution timings). Live human motion review,
  GPU execution timing/memory measurement, and exact GPU seeking with the existing
  history-dependent light selection are not established by these checks.
  Remaining sparseness: broad undressed quay/paving, simple flat ground, repetitive
  facades, and no human destinations or activity. Step 04 should concentrate on a readable
  street corner, entrances, kerbs, parked bikes, and cafe/stop furniture.

- Step 02: restored 43 non-bridge mapped building parts, including 28 stepped apartment
  sections beside Monsungatan. Taller overlapping parts take precedence within parent
  footprints; uncovered parent areas remain. Added 13 authored planted zones and 33 coarse
  park canopies, clipped away from roads, buildings, and water. Generated geometry now has
  10,249 vertices / 13,015 triangles. Converter self-checks cover height parsing, overlap
  partitioning, and triangulation; regeneration into `build/living-city/step02/repro`
  matches both bundled outputs byte-for-byte.
  Inspected before/after sheets for both routes at 320x200/240 and framing at 320x180;
  inspected CPU frames at t=0/5 before deliberately updating the CPU reference checksum
  to `0b17e8f1198856c6`. Shared geometry also changes the existing triangle-seeded window
  patterns. Both routes retain 2 m clearance and deterministic CPU seeks. Windowed GPU
  routes completed. Build and 13-test CTest verification includes GPU review.
  Captures/comparisons: `build/living-city/step02/`; CPU submission mean 1.301 ms,
  p95 1.366 ms (same Debug configuration as baseline; no measured GPU execution time).
  Near facades now have stronger enclosure and stepped roofs; background continuity and
  large undressed quay surfaces remain visibly sparse. Step 03 addresses the background.

- Step 01: coverage audit and verification (`docs/LIVING_CITY_AUDIT.md`, removed 2026-10-25) records exact missing
  building parts, priority areas, baseline captures, and profiling. Added GPU storyboard
  playback/capture for both routes and a repeatable comparison script. Corrected the
  GPU review's stale sky-band assumption. Build and all 13 tests passed, including GPU.
  Both windowed routes completed; live human motion observation and true GPU timing
  remain limitations noted in the audit. Scene output is unchanged.

- Planning: inspected the existing scene code, geometry manifest, and two saved captures;
  documented the artistic stages and commit workflow. `cmake --build build -j` and
  `ctest --test-dir build --output-on-failure` passed (12 tests). GPU tests were disabled
  in that configuration; fresh GPU captures, live route review, and performance baseline
  remain work for step 01. No runtime changes.

## Original baseline findings (2026-09-16)

This section records the pre-step-02 audit, not the current scene. Steps 02–04g repaired
heights, expanded the Eriksberg context to 523 building features over 1,650 × 1,400 m,
and added dressing/materials/waterline details. See the progress log and
[renderer guide](3D.md) for implemented behavior. The separate river crop is larger still.

The current GPU scene already has static apartment windows, 20 moving cars on two street
paths, street lamps, a small boat and wake, shadows, atmosphere, and water reflections.
The generated manifest contains 204 building features across a 950 by 900 metre crop.
That count includes small structures; it does not establish convincing street enclosure.

The three repository captures were available for review; the early and late captures
`proteus-cube-00047746.png` and `proteus-cube-00049029.png` were inspected. They show
large undifferentiated ground areas and a thin distant skyline around detailed foreground
facades. This supports a composition and continuity problem; it does not yet prove which
buildings are missing from the source or importer.

The importer already includes the surrounding context crop. First compare the retained
map, generated mesh, and camera views. Simply enabling an assumed unused context layer
will not solve this. Check omitted building parts, height assumptions, ground treatment,
and coverage beyond the crop before choosing extensions.

## Boat-view water sequence (added 2026-10-17)

Extend the inhabited-city direction out onto the water: leaving a sheltered dock,
passing the working waterfront, and reaching a more exposed river view should each
have a distinct character. Use Gothenburg's river-mouth setting as the artistic brief.
Do not apply ocean swell uniformly inside the basin or require an archipelago expansion
before improving the existing playable area. Exposure zones and flow are authored
approximations until supported by references, not measured local conditions.

### 04e — establish the water route and baseline (complete)

Results and the bounded 04f brief are in [the boat-level water review](WATER_REVIEW.md).

Review from the normal follow camera and the closer `--camera-distance 12
--camera-height 6` view. Cover five situations: resting in the dock, leaving its mouth,
crossing the river, running parallel to a quay, and looking toward open water. Check
which are actually reachable within the current imported water/collision boundary;
identify missing coverage before promising a longer playable route. The existing
scripted screenshot manoeuvre turns and reverses; it does not establish a river transect.
Use a repeatable review route and record positions, headings, simulation times, camera
settings, and water preset so the same views can be compared after each change.

Capture flat and gentle-wave views at native 320x200, with GPU as the visual reference
and CPU as a geometry/handling check. Inspect motion for shimmering highlights,
reflection sliding, repeated wave bands, foam ordering, and water/quay gaps. Record
frame cost and water geometry size before adding detail. Rank findings by how much
screen space and play time they affect. Deliver an annotated capture set and a bounded
04f implementation brief; this audit does not require new traffic or fluid simulation.

### 04f — make the surface describe the place (complete)

- Keep the dock visually sheltered; give the exposed reach stronger directional
  surface movement, with a gradual transition at the mouth. The perfectly flat preset
  remains available. First improve colour, reflection breakup, and surface readability;
  add physical exposure variation only if the review justifies it.
- Balance cool water body colour, reflected sky, and broken building/light reflections.
  Preserve dark contact at the quay and readable hull/wake contrast. Small ripples must
  filter away with distance instead of turning the river into sparkling pixel noise.
- Reuse `boat_water.[ch]` and the existing water shaders. Any change to displaced waves
  must provide matching height, normal, and velocity queries for buoyancy and effects;
  blending wave heights also requires consistent derivatives through the transition.
  Fine visual ripples can remain shading-only. Retain the documented planar-reflection
  approximation unless the low boat view shows a concrete need to replace it.

Acceptance: the dock and river read differently from the boat, the hull stays attached
to the visible surface, and the transition has no visible seam or abrupt handling change.
Inspect idle, acceleration, turns, and parallel-quay travel in flat and wave modes.

### 04g — connect water, shore, and use (selected fittings complete)

The implemented scope is selected ladders, fenders, and wall contact treatment, documented
in [the 04g record](WATER_REVIEW.md#04g-implementation-selected-waterline-edges). The original
brief below also suggested mooring lines, moored craft, and disturbance. Moored craft and
attached lines were subsequently added in 04h; additional disturbance remains a proposal.
The original 04g scope did not require those additions before step 05.

Dress a few clearly visible stretches with ladders, fenders, bollards, mooring lines,
and selected moored craft. Ground them at the waterline and preserve navigable space.
Use restrained contact darkening and local foam/disturbance where justified; avoid a
continuous white surf strip around harbour walls. Quays should show scale and purpose
from alongside the hull, as well as from the old aerial routes.

### Then add relationships on the water

This brief is now implemented as a compact passenger ferry and three small craft; see
[harbor traffic](HARBOR_TRAFFIC.md). The following preserves the original rationale.

After shared actor evaluation in 05, bring the existing ferry proxy task 09 forward if
the boat route would benefit from a moving landmark. Review its route from a freely
moving player as well as from the demo camera. Give it a clear, separate channel and
decide player contact behaviour before putting it into playable traffic. Start with one
legible encounter; defer a general harbour traffic system. Finish its silhouette,
lights, reflection, and visible wake in 10.

Coordinate physical current and crossable wakes with
[physics tasks D1–D2](BOAT_PHYSICS_PLAN.md#phase-d--currents-and-crossing-wakes).
Use one shared wake source for rendering and the water query if the wake affects the
boat. Surface streaks, foam, or buoys should make authored flow understandable before
it alters handling. Retain calm practice water. Currents, ferry traffic, and exposed
water are separate deliverables, so each can be judged independently.

Later, tie sparse gulls, flags, and distant harbour activity to the same sense of wind
and direction. Additional water audio remains a separate design decision.

## Implementation sequence

### 1. Make the city continue beyond the camera

- Review the complete short route and the long storyboard. Mark exposed map edges,
  empty foreground parcels, weak street walls, and missing background layers in captures.
- Establish three spatial layers: detailed street frontage, simpler surrounding blocks,
  and a low-detail distant city/industrial silhouette. Use real retained footprints where
  available; label authored infill and inferred heights as artistic assumptions.
- Add nearby buildings where the coverage audit supports them. Preserve legitimate
  courtyards, streets, squares, water, and views of the crane. Give open land an identity:
  planted courtyard, parking area, promenade, service yard, or small park.
- Extend context only where visible camera cones need it. If the retained snapshot is
  insufficient, choose a documented larger extract or authored distant masses. Keep
  source attribution and reproducible geometry generation.
- Vary roof heights, setbacks, facade widths, and roof silhouettes. Add balconies and
  entrances to the few buildings that pass closest to the camera.

Acceptance: representative stills read as a connected neighbourhood; map boundaries
are concealed by continuous geometry and distance treatment. Camera clearance remains valid.

### 2. Give streets places and purposes

- Build readable kerbs, pavements, crossings, planted strips, parking bays, and quay edges.
- Author three small activity centres along visible parts of the route: a lit corner
  shop/cafe, a bus stop, and a quay seating or fishing spot. Placement is artistic until
  checked against local references.
- Dress these with a restrained kit: benches, bicycle racks and parked bikes, bins,
  bollards, bus shelter, signs, trees, planters, railings, and mooring equipment.
- Add ground-floor shop windows, apartment entrance lights, door recesses, and a few
  awnings. Break the identical window-grid treatment between shops and apartments.
- Use parked cars and a delivery van to connect buildings to roads. Keep walking and
  driving routes clear. Group props by purpose instead of scattering them uniformly.

Acceptance: the viewer can identify a pavement, an entrance, and a destination in the
opening shot; open spaces look intentionally used even with animation frozen.

### 3. Put people and varied traffic into those places

- Start with a few clearly visible pedestrians: one pair walking, someone at the stop,
  a dog walker, and a person sitting or fishing. Expand only where they remain readable.
- Use simple low-poly silhouettes with modest limb motion and varied clothing. Position
  some against lit shopfronts so they register at the virtual resolution.
- Add cyclists, a bus, and a delivery vehicle. Vary traffic spacing and speeds; the
  existing 20 cars mean vehicle count alone is not the main deficiency.
- Author short route schedules: approach, stop, wait, depart. A bus stopping near waiting
  people and a car yielding at a crossing create relationships between moving objects.
- Use separated paths and timed reservations for the few interactions on screen. Hide
  route wraps beyond the shot or behind buildings. No general traffic simulation is needed.

Acceptance: people stay on usable surfaces, vehicles respect authored lanes and stops,
and no actor visibly teleports or intersects another during either camera route.

### 4. Let buildings and lights show activity

- Give apartments stable room states: dark, warm interior, cooler television light,
  partly closed curtains. Group related windows into apartments.
- Animate only a small visible subset: one room turns on, a silhouette crosses a window,
  a television changes softly, a stairwell light briefly comes on.
- Make one identifiable lamp intermittently faulty, with irregular short dimming and long
  stable periods. Change both its visible bulb and the illumination it casts together.
- Add selective brake lights, indicators, and the bus destination panel. Most lamps and
  windows remain steady; reserve actual local lights for useful nearby illumination.

Acceptance: activity is noticeable in motion without a twinkling-building effect;
window changes and lamp behaviour reproduce at a directly selected scene time.

### 5. Give the harbour a major presence

The current pass uses a compact passenger ferry and three small craft, with selected
berth details already implemented. The larger ship brief below is a deferred alternative;
its scale acceptance is not a requirement for the compact ferry.

- Block out a large Stena Line ferry on the river outside the dock basin. Start with
  hull, layered decks, bridge, funnel, and a broad recognizable colour/livery treatment;
  inspect its silhouette before spending effort on windows or lettering.
- Treat its exact vessel, dimensions, route, and berth as unresolved reference work.
  This proposal is artistic staging, not a claim about current Stena operations.
- Test visibility immediately with a cheap proxy. The existing camera looks back towards
  the neighbourhood during the water section, so a river ferry may be behind the viewer.
  If necessary, propose a small gaze adjustment that includes the ferry and buildings
  together, with camera-clearance checks. A longer shot would be a separate timing decision.
- Let the ferry move slowly relative to cars. Add a restrained wake, deck lights,
  navigation lights, and its reflection. Ensure river width, depth appearance, quay
  clearance, reflection clipping, and render bounds support its scale.
- Keep the existing small boat as a scale cue. Add moored craft, quay ladders, fenders,
  bollards, and a few distant industrial lights around the harbour.

Acceptance: the ferry is visible and unmistakably large in at least one composed shot,
without hiding the neighbourhood or appearing to fit inside the small dock basin.

### 6. Add atmosphere and finish the shot

Choose a few extras after the previous passes are legible:

| Detail | What it contributes |
| --- | --- |
| Gulls circling or perched on quay furniture | Harbour identity and motion above street level |
| Tree movement and fluttering flags | A shared sense of wind |
| Rooftop vents and faint drifting steam | Activity above otherwise plain roofs |
| Damp pavement and a few puddles | Selective light reflections at street level |
| Roadworks with a barrier and warning beacon | A reason for an irregular street layout |
| Cafe chairs, a closing shop, someone on a balcony | Different uses and rhythms within the same block |
| Distant industrial roofs, masts, and scattered lights | Depth beyond the residential frontage |
| A clock or simple illuminated shop sign | A recognizable landmark at human scale |

Treat rain, complex particles, detailed interiors, and extra environmental audio as later
options. The existing tracker music and its generated assets stay unchanged in this plan.

Acceptance: additional effects support the main silhouettes and stories; they do not
turn small details into distracting pixel noise.

## Technical boundaries and checks

- City activity runs at plausible independent speeds. Do not make every window, walk cycle,
  or lamp pulse with the music.
- Keep authored paths, stable IDs, schedules, and evaluated object states in the shared
  scene layer. `waterfront_scene_evaluate_actors` now consolidates cars, lamps, berths,
  and travelling vessels; extend that explicit-time snapshot when adding actors.
- Make actor and window states functions of explicit time and stable IDs. Seek directly
  into waiting, walking, braking, and light-change intervals without simulation warm-up.
  Current GPU light selection favours previously selected lights; review that history
  dependence when adding lights and test direct capture versus continuous playback.
- Preserve the CPU reference as a useful working mode. New visual embellishments may be
  GPU-only; shared geometry changes must have reviewed CPU baseline changes, not blind
  checksum updates. Do not require complete feature parity.
- Batch static props and reuse actor meshes. Introduce instancing only where profiling
  supports it. Keep window geometry resident and update compact state for active rooms.
- The present local-light cap is 32. Thousands of windows and ferry portholes should be
  emissive surfaces; select only the nearby lights that need to illuminate other objects.
- Use the same evaluated transforms in colour, shadow, and reflection passes. Measure
  the added work in all passes; collect a baseline before expanding scene complexity.
- At each implemented stage, build and run CTest. Capture CPU and GPU river frames from the
  water (`wake_velocity --screenshot`), the fixed `gpu_fixture --city` street view and
  event-specific frames, and inspect a windowed river drive in motion. GPU checks need a
  working graphics context.
- Compare fresh direct seeks with sequential playback and backward/forward seeks. Check
  light selection and shadow/reflection coherence.
  Report CPU submission and GPU timing separately where available, plus draw counts and
  memory growth; establish a performance target from measurements on the target machine.

Current implementation priority: people/cyclists in 06 follow the completed shared
actor foundation. The user brought travelling boats and a compact passenger ferry
forward; the original large Stena ferry is a deferred alternative, not the current
vessel. Keep physical currents/crossing wakes on the separate D1/D2 track.

## Soundtrack and flock follow pass (2026-10-11, historical; removed 2026-10-25)

The demo now consists solely of `cube`, using the working Below the surface XM
(120 BPM, speed 6, four minutes). Five gulls provide a subject for a new camera
circuit: street departure, river crossing, dock return, and a held ending. The camera
trails the flock with a stable horizon; wingbeats and formation drift are staggered.
The shared frame evaluation drives CPU, GPU, reflection, and shadow geometry and
supports deterministic direct seeking. Perched gulls remain pending.

The earlier short/long storyboard routes remain diagnostic references. Main-demo
scene switching and the starscroll, fire, and plasma implementations were removed.
The starter XM remains for the independent dance study and sample-generation workflow.
