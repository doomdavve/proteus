# Boat-level Eriksberg water review

Execution step 04e of [the living city plan](LIVING_CITY_PLAN.md), 2026-10-17.
Status, reconciled 2026-10-18: audit 04e, surface pass 04f, and selected waterline pass
04g are complete. The 04e audit did not change rendering or handling. Its original findings,
geometry costs, crop limits, and implementation brief below are historical baseline evidence;
the 04f/04g records later in this document describe the changes that followed.

This review used the original Eriksberg location. On 2026-10-25 that location, its route
and capture assembler (`tools/boat_water_review.py`) and the `--river`/`--default-view` flags
were removed; `boat_water_review` now drives only the river route, documented in the
[river expansion record](river/README.md). The commands and results below are a historical
record of the Eriksberg route; read the removed tooling in the git history.
See [the living-city plan](LIVING_CITY_PLAN.md) for the next visual task, step 05.

## Review route and reproduction

`boat_water_review` drives the existing 120 Hz boat simulation with a small waypoint
controller. It starts at the normal Eriksberg spawn, leaves the basin, crosses toward
the southern bank, turns toward the exposed southwest view, approaches the northern
quay, follows it northeast, and returns to the basin mouth. This is a review circuit,
not a new gameplay mode or an assertion about real navigation channels.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/boat_water_review --check
./build/boat_water_review --check --waves
python3 tools/boat_water_review.py --sequence --movies
```

The Python tool needs only the standard library; `--movies` additionally needs ffmpeg.
GPU runs require a desktop graphics context. Omit `--movies` to retain PNG sequences
without an encoder. `--assemble --movies` rebuilds sheets, the route map, and videos
from existing captures without rerunning the simulation. For a windowed route pass:

```sh
./build/boat_water_review --output build/living-city/water-review/windowed-waves --waves --visible
```

Artifacts live under `build/living-city/water-review/`:

- `route.svg`: north-up map of the exact imported water triangles and both driven paths;
  the grid is 100 metres. Orange is flat, white is waves.
- `flat/route.csv`, `waves/route.csv`: simulation tick, nominal tick time, position,
  heading, smoothed camera heading, speed, heave/pitch/roll, sampled distance to the
  nearest water boundary, and next waypoint. Headings are radians; zero faces -Z.
  Simulation X is the negative of map east. CSV time is tick / 120. Since foundations C1/C2,
  wave evaluation also uses tick-derived double time, reduced to one float phase per wave.
- `compare-<stop>.png`: annotated comparisons, GPU flat / GPU waves / CPU flat;
  close camera above, default camera below. The originals remain separate PNGs.
- `overview.png`, `motion-<preset>-<second>.png`: all key views and consecutive 0.1 s
  filmstrips around the mouth, crossing, exposed reach, and quay section.
- `flat.mp4`, `waves.mp4`: complete close-camera 10 Hz review sequences, when requested.
- `cpu-flat/`, `cpu-waves/`: geometry and hull/surface checks, including both cameras.
  CPU does not have scene reflections and is not the material reference.
- `<preset>.log`, `summary.json`: named arrivals, timing and geometry baseline, and
  sampled clearance/speed summaries.

The default camera is 20 units behind / 14 high; the close view is 12 behind / 6 high.
Both look five units ahead with the existing smoothed heading and stable horizon.
Images are 320x200; GPU uses the existing 1280x800 supersampling/downsample path.

The controller uses the same waypoint targets, steering rule, and thrust targets for
both water presets, allowing their physical responses to differ. These are matching
situations, not pixel-aligned A/B images: wave mode arrives up to 0.25 seconds later,
with slightly different positions, headings, and wave phase. Reproduce with the same
preset and build for before/after comparisons. Do not infer material differences from
individual wake pixels between the two presets.

| Stop | Flat time | Waves time | Flat simulation X / Z | Flat heading | Purpose |
| --- | ---: | ---: | --- | ---: | --- |
| dock | 0.000 | 0.000 | -12.000 / 60.000 | 0.000 | Idle and reflected quay/crane |
| mouth | 13.050 | 13.042 | -12.000 / -113.181 | -0.000 | Basin-to-river transition |
| crossing | 25.867 | 25.958 | -160.129 / -243.523 | -0.989 | Water-dominated view across the river |
| south-bank | 32.208 | 32.325 | -166.207 / -324.902 | 0.447 | Turn toward the exposed southwest reach |
| exposed | 48.658 | 48.775 | 69.206 / -450.203 | 1.153 | Broad water surface and scene extent |
| quay-approach | 62.450 | 62.617 | 166.280 / -291.150 | 2.819 | Return toward the working waterfront |
| quay-alongside | 72.733 | 72.958 | 69.782 / -181.617 | -2.173 | Contact, scale, and reflection continuity |
| return | 77.975 | 78.225 | -2.164 / -131.838 | -2.178 | Basin-mouth landmarks on return |

Stops are recorded when the boat enters a 12-unit waypoint radius; they are not
teleports to the targets. At every simulation tick, an enclosing 5 x 2.4-unit rectangle
with 0.5-unit shore clearance must remain navigable. This is more conservative than
the playable hull footprint. Both presets complete the circuit without violating it.
This checks shoreline/crop containment, not overhead bridge or crane collisions.

## Findings, in order of impact

1. **The river surface is now foreground scenery.** From crossing through exposed,
   water occupies most of the close view. Flat mode reads largely as bright reflected
   sky/cloud patches; wave mode is darker and breaks the reflections more convincingly.
   The moving hull and wake are readable, but the dock, river, and exposed reach share
   the same material and global waves. Improving this large continuous surface will
   affect more of a boat trip than adding small distant actors. Evidence:
   `compare-crossing.png`, `compare-exposed.png`, and the corresponding filmstrips.

2. **Wave geometry is much denser than the image needs at distance.** Imported water
   has 266 triangles; wave mode subdivides the whole crop to 1,069,001 triangles
   (3,207,003 non-indexed vertices). Position/normal/colour payload alone is
   89,796,084 bytes, about 85.6 MiB, on the GPU, with additional retained CPU arrays,
   surface storage, and other scene resources. This is calculated buffer payload,
   not measured driver memory. CPU wave rendering is expensive. An all-map dense
   mesh is a poor foundation for adding more visible water; reduce distant geometry
   while retaining shoreline coverage and the same near-hull physical surface.

3. **The exposed view is not a modelled river mouth or an ocean route.** The bundled
   crop is map east [-700, 950], north [-600, 800] metres. Water at its outer edges
   stops at an artificial collision boundary. At the exposed stop, the southern
   crop edge is only about 150 metres away in Z. Sparse bank/context geometry and
   the nearly empty horizon make the view appear ocean-like; they do not establish
   real open-water coverage. The existing circuit is reachable. A continuous trip
   toward the sea or archipelago needs a separate map/context decision. Evidence:
   `route.svg`, `compare-south-bank.png`, `compare-exposed.png`.

4. **Reflections anchor the dock; preserve them while changing the surface.** Crane
   supports and buildings have useful reflections near the quay. The wave treatment
   makes them less mirror-like, but the source remains a low-resolution planar
   reflection projected onto displaced water. Broad cloud patches and pale highlights
   can dominate the exposed view. Inspect normal strength and distance filtering
   before adding more ripple frequencies or brighter glints. The sampled sequences
   show continuous wake trails and moving reflection breakup; they do not establish
   freedom from 60 Hz shimmer or planar-projection sliding in every viewing angle.
   Evidence: `compare-dock.png`, `compare-quay-alongside.png`, `motion-waves-72.png`.

5. **Waterline detail is a separate pass after surface readability.** The quay wall
   remains attached in the reviewed views; no obvious open seam appears in the
   inspected samples. Alongside, long bare wall stretches could gain fenders, ladders,
   moorings, and stronger contact cues. Current wake/spray already communicate speed
   and turns; retain their clear hull contrast. These are step 04g findings, not a
   reason to scatter more foam over the whole river. Evidence:
   `compare-quay-approach.png`, `compare-quay-alongside.png`, CPU wave originals.

## Bounded implementation brief: 04f

Start with the existing playable crop and preserve current handling, flat-water mode,
the three physical wave components, camera, music, and wake/spray behaviour.

1. Replace uniform all-map wave tessellation with a bounded near-field/far-field
   representation, or a simpler measured alternative. Keep the imported shoreline
   mask and avoid cracks or popping as the boat moves. Near the hull, visible crests
   must still agree with `boat_water_query`; distant normals can retain analytic
   shading on coarser geometry. Record actual triangle and buffer reductions and
   compare the complete route before extending water coverage.
2. Add one smooth, authored **visual exposure field**: sheltered basin through river
   mouth to exposed river. Use it to tune shading-scale ripple strength, colour, and
   reflection breakup. Keep the water-body palette related across the transition,
   with recognizable reflected buildings near shore and restrained highlights in
   the crossing. Small detail must fade with projected pixel footprint. This first
   pass changes surface presentation; it does not attenuate physical wave heights
   or introduce current forces.
3. Review matching same-preset captures and both full circuits. Judge sheltered/exposed
   identity in stills, continuity through the mouth, hull/foam attachment, quay seams,
   and motion stability at the two camera heights. Use the existing quiet physical
   baseline. If physical shelter variation is later warranted, scope it separately
   with matching height/normal/velocity derivatives and buoyancy validation.

Acceptance: a measurable reduction in wave geometry/payload, preserved navigability and
near-hull alignment, a readable dock-to-river change, and no new obvious shoreline gaps,
transition seams, or high-frequency glitter in the reviewed motion. Report CPU submission
and GPU execution separately; do not treat one as the other. Do not add ferry AI,
currents, crossable traffic wakes, new map downloads, or archipelago geography in 04f.

## Verification and limits

Debug baseline on this machine, with the complete route and existing scene passes:

| Renderer / preset | Samples / discarded warm-up | Mean ms | p95 ms |
| --- | --- | ---: | ---: |
| GPU + AA, flat | 781 / 120 | 5.221 | 5.605 |
| GPU + AA, waves | 784 / 120 | 5.341 | 5.818 |
| CPU, flat | 17 / 2 | 18.074 | 20.346 |
| CPU, waves | 17 / 2 | 869.671 | 907.149 |

GPU numbers measure CPU-side draw submission, including any driver waits, with the batch
flushed but no GPU completion query. CPU numbers include complete software rendering;
they sample five-second intervals plus the finish because tessellating/evaluating the
whole wave surface is expensive. Named captures and image export are outside the timed
regions. These small/noisy Debug samples are a reproducible starting point, not GPU
execution measurements or a frame-rate guarantee. Do not compare them with old fly-through
profiles, which use a different camera, render size, and workload. No Instruments trace
or measured driver-memory result was collected; use those for deeper performance work.

Both 120 Hz route checks passed. At 10 Hz, sampled minimum boat-centre distance to the
water boundary was 17.70 units flat / 17.64 waves; the conservative hull check runs at
every tick. These route clearances do not establish safe travel beyond the crop.

Debug configure/build and all 42 CTest checks passed, including 11 GPU checks. The two
new route checks also passed after adding preset metadata to the tool's logs. Python
compilation, artifact assembly, optional ffmpeg encoding, and `git diff --check` passed.
Inspected all eight annotated key views at both camera heights, selected CPU wave views,
and adjacent-frame filmstrips at the mouth, crossing, exposed turn, and quay.
The complete GPU+AA wave circuit also finished in a windowed playback run:
`windowed-waves.log` and `windowed-waves/` retain its metadata and captures.

The saved movies contain the complete route at 10 Hz. Sampled image review cannot certify
60 Hz temporal stability, all possible player cameras, night-time lighting, or exact
GPU direct-seek parity with history-dependent light selection. No new visual effect or
handling change is claimed by this audit. Surface implementation belongs to 04f.

## 04f implementation: boat-level surface and bounded geometry

The GPU now keeps the original 266-triangle Eriksberg water footprint as a flat far
surface and draws a clipped, moving near patch separately. The patch is a 192-metre
square on a fixed 1.5-metre world grid. It recentres in 12-metre steps, replacing only
cells outside the rendered disk; overlapping triangles retain their world coordinates.
It clips against the original water triangles, including shoreline and map-crop edges.
There is no rectangular overlay extending onto land and no new mask texture.

The existing three waves displace the patch fully within 48 metres of the boat, then
fade smoothly to zero at 80 metres. The fragment passes meet at that flat 80-metre
boundary. Both use the same boating albedo; the brighter legacy timeline water remains
separate. Analytic wave normals continue into the far field. Near-hull interpolation
retains the existing 3.5 cm error bound. This is a rendering approximation at distance;
physical waves, buoyancy, foam/spray poses, and handling are unchanged.

An authored shader field shelters the narrow dock and opens gradually across its
mouth, from approximately world Z -55 to -190. It follows the dock axis and leaves
the rest of the river exposed. It controls subtle blue/green body-colour variation,
reflection strength and breakup, and small wind-aligned shading ripples. Dock reflections
are steadier, river reflections more broken, and the old broad bright glint is restrained.
Small normal detail fades with projected native-pixel footprint, accounting for 4x4 AA.
The field changes neither physical wave height nor current velocity. It is artistic
staging, not measured local wind, shelter, salinity, or current data.

The patch uses a reusable position-only GPU buffer. Geometry is clipped one row at a
time and uploaded on recentering; the vertex shader displaces it from explicit scene
time. This adds one main-view water submission; reflection
and shadow passes still exclude the water surface. The CPU renderer retains its original
full tessellation and appearance as a reference. No new game controls or assets.

Reproduce the captures and before/after sheets with:

```sh
python3 tools/boat_water_review.py --output build/living-city/water-surface \
  --sequence --movies --compare build/living-city/water-review
./build/boat_water_review --check-gpu
./build/boat_water_review --output build/living-city/water-surface/windowed-waves --waves --visible
./build/boat_water_review --output build/living-city/water-surface/default-waves --waves --default-view --sequence
```

`--visible` now plays the complete route at 60 Hz; hidden sequences remain 10 Hz.
`--default-view` selects the higher 20/14 follow camera for sequences and playback;
the usual sequence uses 12/6. Named captures always include both cameras.
Before/after sheets place 04e on the left and 04f on the right, with close/default views
above/below. Useful examples are `before-after-waves-dock.png`,
`before-after-waves-exposed.png`, and `before-after-waves-quay-alongside.png`.

| Wave-mode geometry / buffer | 04e | 04f |
| --- | ---: | ---: |
| Peak dense triangles on the review circuit | 1,069,001 | 36,316 |
| Separate flat far triangles | 0 | 266 |
| Calculated allocated GPU vertex payload | 89,796,084 bytes | 2,359,296 bytes |
| Main-view water submissions | 1 | 2 |

That is about 96.6% fewer submitted water triangles at peak and 97.4% less allocated
wave-vertex payload. The new number includes unused capacity in the reusable buffer;
it excludes the small existing far mesh and driver overhead. Neither number is a
measurement of driver memory. The route rebuilt the patch 126 times.

At 10 Hz with the same Debug capture harness, flat GPU submission mean/p95 was
5.319/5.706 ms; waves was 5.815/8.472 ms, versus 04e's 5.341/5.818 ms for waves.
The geometry reduction trades a small average submission increase and occasional
patch-update cost for much lower resident geometry. It is not a measured GPU speedup.
At 60 Hz, the completed wave playback had 4,695 frames (120 discarded), submission
mean/p95 5.388/5.922 ms, and maximum 9.268 ms, including 128 patch updates. The different
sampling rate and warm-up duration make this a separate observation, not an A/B speedup.

Xcode Time Profiler completed a 9.84-second deterministic wave-route recording:
`water.trace`, `water-time-profile.xml`, and `instruments-summary.txt`. Main-thread
sample weight included about 276 ms in `boat_water_patch` (roughly 3% of the 9,025 ms
sampled main-thread total). This diagnostic includes startup, CPU route-clearance
checks, and captures; it is not an interactive game benchmark or GPU execution timing.
To repeat, choose a fresh output trace path:

```sh
xcrun xctrace record --template 'Time Profiler' \
  --output build/living-city/water-surface/water-next.trace --time-limit 10s \
  --launch -- ./build/boat_water_review \
  --output build/living-city/water-surface/instruments-next --waves
```

Verification covers both complete driven circuits, default/close stills, adjacent-frame
motion strips, CPU near-surface interpolation and sampled coverage against the imported
water, and invariant overlapping geometry across a cache recenter. A new GPU probe checks
actual colour agreement inside/outside the patch and a smooth shelter-to-river colour
transition with reflections/shadows disabled. All four 04e pose CSVs and all 32 CPU key
images compare exactly unchanged. Full build/test and windowed playback results follow.

Final verification: Debug configure/build and all 43 CTest checks passed, including
12 GPU checks and the new `gpu_water_surface` probe. Both complete GPU water presets
have before/after stills and 10 Hz movies; an additional higher-camera wave sequence
and `default-waves.mp4` cover its motion. Inspected the matching dock, mouth, crossing,
exposed reach, quay, and return views plus adjacent-frame strips at both heights.
Also inspected a native 320x200 wave capture without AA (`native-waves.png`).
The complete close-view wave route finished in a 60 Hz windowed pass with the submission
figures above. Python compilation, artifact assembly, movie encoding, Instruments
record/export, and `git diff --check` passed. Step 04f was complete at this checkpoint; the subsequent 04g record follows below.

Remaining limitations: planar reflection projection, the finite geographic crop, and
the expensive full CPU wave reference remain. Distant GPU wave heights now deliberately
flatten, so CPU/GPU surface geometry differs beyond the near field. GPU execution cost,
driver memory, and shimmer across arbitrary player views are not certified by this pass.
Waterline dressing belongs to 04g; currents and physical crossing wakes remain physics D.


## 04g implementation: selected waterline edges

Map-validated quay sections now carry equipment stations about every 16 metres,
with access ladders about every 48 metres. The station cadence follows accepted
sections, so gaps and corners interrupt it. Yellowed metal ladders extend below
water and return above the deck at existing rail openings. Other stations have
paired rubber rubbing blocks suspended by ropes from small cleats. Dark wet toes
and muted tide stains sit on these selected wall stretches; there is no white surf
strip. These are authored harbour fittings, not surveyed local equipment.

All 1,168 added boxes stay close to the original shore. Below-quay vertices are
validated within 0.5 m of a retained quay segment; the largest outboard projection
is 0.42 m. Existing navigability and camera-clearance checks pass. No craft or new
collision behaviour is introduced. Vessel mooring lines and moored craft remain
with later harbour work; the new ropes suspend fenders. Surface shading and the
physical wave model remain the completed 04f implementation.

GPU colour, reflection, and shadow passes share the existing resident opaque kit,
adding 14,016 triangles and no new mesh submissions. Position, normal, UV, and
colour buffer payload grows by 1,513,728 bytes (about 1.44 MiB), also retained on the
CPU by raylib. This is calculated geometry payload, not measured driver memory.
The software reference draws the same wall fittings; other street-kit details
remain GPU-only. CPU water retains its existing simpler appearance.

Reproduce the route captures with the existing commands, changing output directories:

```sh
./build/boat_water_review --output build/living-city/waterline/gpu-flat
./build/boat_water_review --output build/living-city/waterline/gpu-waves --waves
./build/boat_water_review --output build/living-city/waterline/cpu-flat --renderer cpu
./build/boat_water_review --output build/living-city/waterline/windowed-waves --waves --visible
```

Inspected the close station view from world (34, 3.3, 48) towards (44, 1.2, 58),
55-degree vertical FOV: `detail-gpu.png` and `detail-cpu.png` under that directory.
The temporary diagnostic source is retained as `detail_capture.c`. Ladders, fender
suspension, rail openings, and reflections read clearly here; they are deliberately
subtle from the normal mid-channel chase camera. `framing.png` collects 22 CPU/GPU
opening-timeline captures at the supported aspect ratios. Existing route captures
include both camera heights and approaches to shore. The windowed wave circuit
completed; static attachment tests and all 43 CTest checks pass. See the living-city
progress log for timing samples and CPU checksum review.

Limits: no manual interactive input/audio assessment, no new Instruments trace or
GPU execution measurement, and no certification of arbitrary shore-hugging cameras.
Existing wave crests can cover the lower ladder rungs and wet bands. The rest of
the wall stays clear; no extra animated foam is warranted for fixed sheltered fittings.
