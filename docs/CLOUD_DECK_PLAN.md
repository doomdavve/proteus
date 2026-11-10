# Softer edges on the original cloud deck

Status, 2026-11-10: grouped-cloud drift implemented and natively verified; user review pending.
Open items: user visual review of grouped clouds, drift speed/direction and reflections.
Done when: grouped clouds drift deterministically with matching sky/reflections and seek checks.
Next: review the local candidate for PR #179; publication remains pending.

## Scope and decision

The user cancelled the rounded cumulus experiment after seeing it in the scene and
requested the original Minecraft-inspired clouds with less blocky edges. This branch
starts from the original implementation; none of the cumulus intersection, shading,
world-field or renderer changes is included. Keep the existing lighting, apparent deck
thickness, perspective and integer-scaled presentation. Day is the review setting; its
background palette now matches the deeper-blue preview requested for in-game review.

Checked the [Open loops board](README.md#open-loops). This small track defers the lighting
clock/night/cargo acceptance, water-glitter attenuation, graphics Batch C, paused harbour
gameplay, plugin P5 and other existing loops. No new weather model, cloud controls,
wind simulation, sprites or volumetric cumulus renderer is scheduled.

## Steps

1. **Soften the original deck and verify locally.** Implemented: continuously sample the
   original broad noise scale, replace per-tile random notches with weaker interpolated
   detail, and retain the existing shared underside/rim mask. Native verification recorded below.
   The implementation and current contract are in [3D.md](3D.md#scene-lighting).
2. **Apply deeper-blue Day for play review.** Implemented locally using the shared Day
   recipe, including its fixed-hour noon anchor and GPU reflections. Preserve the existing
   morning/afternoon gradient spans. Native verification passed; see below.
3. **Reduce coverage and layer the reflected shading.** Implemented locally following
   user play feedback: fewer patches and three broad offset tones in the shared mask,
   CPU evaluator and GPU shader. Native evidence is recorded below.
4. **User review and closing.** Done, 2026-11-10: the user accepted the final treatment
   ("Yes, we nailed it finally!"). Confirmed the current contract in
   [3D.md](3D.md#scene-lighting) and checks in [TESTING.md](TESTING.md#gpu-checks).
   No implementation leftovers remain. Browser/Windows/Linux qualification and the
   deferred weather/lighting work remain outside this completed track.

## Review commands

From `/private/tmp/proteus-cloud-deck`:

```sh
./build/wake_velocity --location outer --daylight day
./build/wake_velocity --location outer --daylight day --camera-height 4 \
  --camera-distance 48 --fixed-fov --resolution high --hdr off \
  --screenshot build/layers-review.png --time 3
```

The low, wide camera and high resolution are capture options, not changed defaults.

## Verification and limits

The reflection follow-up below supersedes the two-tone cloud shading. The deeper-blue Day recipe is implemented after the preview below. The existing sky recipe/cache,
CPU/GPU lighting/target and texture-lifecycle tests apply unchanged. The cloud-edge work changes shared mask generation; the deeper-blue background changes
the shared lighting recipe. The reflection follow-up uses the existing mask texture
with one additional encoded channel and matching CPU/GPU colour selection; it adds
no texture allocation or per-frame upload.
The retained deck is anchored to its panorama, as before: this adjustment does not add
translation parallax or closed world-space cloud geometry. The user preferred the
original spatial impression; this track preserves that renderer rather than claiming
new volumetric behaviour.


### Native review, 2026-11-10

Apple Silicon (M1 Max), macOS, Debug, `PROTEUS_GPU_TESTS=ON`, pinned dependencies
reused locally. Configure/build passed; only the existing upstream `stb_vorbis.c`
warning was observed. CPU before/after Day captures at the same outer pose and a
1280×800 GPU Day capture were inspected. The new deck keeps broad flat forms and
its thin rim, with curved rather than coarse square outlines. A visible 360-frame
GPU Day demo exited 0. This was an automated windowed smoke, not human motion,
physical input or audio acceptance.

The independent `gpu_fixture --city --check` passed sky target/CPU parity,
lighting, seeking, scene lifecycle, reflection clipping and zero sky texture
writes after setup. The full preset ran all 510 enabled tests (38 GPU-labelled; 15 retired checks disabled)
in 112.02 s: 504 passed initially and six failed while GLFW could not initialize a
monitor/window. After graphics access recovered, `ctest --preset full --rerun-failed`
passed those six in 21.03 s. All enabled tests therefore passed across the full run
and targeted retry. Formatting,
`git diff --check`, the new plan's two local links/anchors and the index target
were checked. User acceptance, browser, Windows and Linux were not exercised.

Local ignored evidence: `build/cloud-before.png`, `build/cloud-after.png`,
`build/cloud-after-high.png`, `build/cloud-build.log`, `build/cloud-full.log`,
`build/cloud-rerun.log`, `build/cloud-gpu-retry.log` and `build/cloud-demo.log`.


### Deeper-blue preview, 2026-11-10 (superseded by in-game candidate)

The user preferred the softer deck ("I like this a lot better!") and requested a
preview of a deeper blue inspired by a supplied sky photograph. This is appearance
feedback, not a claimed motion or platform acceptance.

Rendered the same outer GPU scene/camera at 1280×800, HDR off, with temporary
sky-only colour values: horizon RGB (160, 213, 242), zenith (22, 91, 190), gradient
span 0.65. Cloud colours and geometry, fog and scene light retain the candidate values.
The actual game's reflection pass also sees the temporary sky recipe. The preview
uses the existing shader evaluator with those background inputs substituted; it is
not an image edit. Inspected `build/deeper-blue-preview.png` and restored the packaged
shader afterward; source and shader files are unchanged by the preview. This does not
implement a new Day palette or change the full-hour evaluator. No new build, suite or
motion check was needed for this temporary colour comparison. The native verification
above still applies to the unchanged implementation. Palette acceptance is pending.


### In-game deeper-blue candidate, 2026-11-10

At the user's request to try the preview in-game, applied its exact horizon/zenith/gradient
values to `waterfront_lighting()`'s Day recipe. Both renderers and GPU reflections use
the normal lighting uniforms; the temporary shader substitution is removed. Fixed-hour
noon uses Day and blends to its neighbours. Morning/afternoon explicitly retain their
0.85 gradient spans, matching their previous anchor recipes. Scene lighting, cloud colours,
fog, water tint and fixed sunset/dusk anchors keep their values.

Native Debug rebuild and `validate_waterfront_lighting` passed. Inspected the normal CPU
320×200 Day capture (`build/blue-cpu.png`) and GPU 1280×800 capture
(`build/blue-in-game.png`); the latter is byte-identical to the requested preview
(`build/deeper-blue-preview.png`, SHA-1 `40884a1dacb4a2ad497602b44fdf6f7983d32748`).
`ctest --preset full` passed all 510 enabled tests (38 GPU-labelled, 15 retired checks
disabled) in 119.73 s, with no retries needed. Build output was warning-clean. This makes
the palette available for
play review; user colour/motion acceptance and other-platform qualification remain open.


Launched the normal GPU game in Day for the user's requested play review, from the
implementation worktree, using `./build/wake_velocity --location outer --daylight day`
(`build/blue-play.log`). This is launch evidence, not a human motion/audio verdict.
Changed documentation links, formatting and `git diff --check` passed. No browser,
Windows or Linux qualification is claimed for the new palette.


### Reflection-focused layers, 2026-11-10

User play feedback found excessive cloud coverage and bland reflected shapes. The user
requested two or three layers while keeping the shallow deck. The candidate raises the
footprint threshold from 0.60 to 0.68 and encodes three broad shading terraces in the
existing mask's B channel. Offset density samples avoid concentric rings. CPU and GPU
use identical shadow/middle/light colour formulas from the current lighting palette,
and the reflection pass receives the same shader recipe. The new mask remains static,
independent of light, and uploaded only at scene setup. The [3D guide](3D.md#scene-lighting)
owns the current contract. Full weather, wind and volumetric cumulus remain deferred.

Review evidence uses actual GPU reflections, including the normal low-resolution camera. This feedback
reopens cloud coverage/shading acceptance; earlier preference for the softer outlines
and the deeper-blue in-game trial is retained without implying acceptance of this revision.


Native evidence (Apple Silicon/macOS, Debug, GPU tests enabled): warning-clean build
and the focused `validate_waterfront_lighting` check passed. Inspected the final GPU
`build/layers-review.png` (1280×800, low/wide review camera, HDR off) and
`build/layers-gameplay.png` (320×200, normal camera), plus the CPU
`build/layers-cpu.png`. The three tonal regions remain visible in the reflected
footprints at native gameplay resolution. A visible 120-frame GPU Day demo exited 0
(`build/layers-demo.log`); this is automated motion smoke, not human play acceptance.
Mask checks require all three valid B-channel terrace bands and zero B outside the
underside footprint. Full CPU/GPU comparison, lighting/cache and upload checks run
through the full preset. Formatting, changed plan links/anchors and
`git diff --check` passed. Browser, Windows, Linux and human audio review were not run.
`ctest --preset full` passed all 510 enabled tests (38 GPU-labelled, 15 retired
checks disabled) in 118.08 s without retries (`build/layers-full.log`).
The existing play window must be restarted to load the revised static mask and shader.


### Closing and user acceptance, 2026-11-10

The user accepted the final sparser, three-tone reflected clouds with the deeper-blue Day
sky: "Yes, we nailed it finally!" This closes the visual treatment requested in this
conversation. No additional platform, audio or lighting-clock acceptance is inferred.
The specification and test guide already describe the final implementation. The final
implementation's native full suite passed all 510 enabled tests as recorded above; this
closing update changes documentation only. Broader platform checks, cloud wind/geometry,
water-glitter attenuation and other lighting-plan acceptance remain explicitly deferred.
Visual acceptance alone did not authorize publication; the user separately requested a PR
on 2026-11-10. The branch is now authorized for push and PR creation.


### PR preparation against current main, 2026-11-10

The user explicitly requested a PR. Integrated `origin/main` at `3080e4c` without
conflicts, reconfigured and rebuilt Debug with GPU tests enabled, warning-clean.
`ctest --preset full` passed all 544 enabled tests (15 retired checks disabled)
in 113.66 s, without retries (`build/pr-full.log`). The updated GPU capture
`build/pr-cloud-review.png` was inspected and is byte-identical to the accepted
`build/layers-review.png`; SHA-1 `d93463081c9b47952db1568696f1577926deac2b`.
`git diff --check origin/main...HEAD` passed. The new suite count includes main's
architecture work, not extra cloud features. No additional user acceptance is inferred.

### Grouped-cloud follow-up, 2026-11-10

The user requested less evenly distributed clouds after PR #179 opened. Added a slower
density field (3.6 versus 0.8 patch scale), blended at 22%, retaining local patch detail
and the existing mask, terraces and renderer paths. This reopens distribution acceptance;
earlier outline/palette acceptance remains historical. Movement was not implemented in
this grouping-only step; the scrolling follow-up below supersedes that limit.

A future motion step can use a shared scrolling world-plane mask plus a simulation-time
offset in CPU/GPU sampling, including reflections. This needs cache invalidation, wrapping
and deterministic seek tests; lighting hour alone should not drive wind. A simple panorama
rotation is cheaper but looks like rotation around the observer rather than translation.

Native candidate evidence (2026-11-10, Apple Silicon/macOS, Debug): warning-clean
build; inspected `build/groups-cpu.png` and actual GPU sky/reflections in
`build/groups-review-access.png`. A 120-frame windowed GPU demo exited 0
(`build/groups-demo.log`); automated smoke only, no human input/audio acceptance.
The sandboxed full suite encountered macOS graphics initialization timeouts; a full
run with graphics access passed all 544 enabled tests (15 retired checks disabled);
see `build/groups-full-access.log`. `git diff --check` passed. Other platforms
and user distribution acceptance remain unverified.

### Scrolling field, 2026-11-10

The user requested the scrolling solution next, superseding the earlier motion deferral.
Replaced the panorama cloud flags with a shared repeating world-plane texture, generated
once and uploaded only at scene creation. CPU and GPU project the quantized sky rays onto
the underside/rim planes and sample with the same bounded simulation-time wind offset.
Main and reflection passes use the same frame time. The CPU visible cache now invalidates
on time as well as light; it still evaluates each visible texel at most once per generation.
The time-zero compatibility wrappers remain for static callers and tests. Current behaviour
is specified in [3D.md](3D.md#scene-lighting). No new control or lighting-hour coupling.

The texture has finite spatial resolution, so outlines advance in small point-sampled steps,
consistent with the pixel presentation. The repeating field introduces a spatial edge blend
and can eventually repeat; this is slow drift, not evolving weather or volumetric geometry.
Wind speed/direction and the grouped distribution await user review. Deferred loops listed
above remain deferred.

Native verification (2026-11-10, Apple Silicon/macOS, Debug with GPU tests enabled):
warning-clean build and standalone lighting/sky validators passed. The full CTest preset
passed all 544 enabled tests (15 retired checks disabled) in 105.41 seconds without retries
(`build/scroll-full.log`). CPU reference/draw checks cover fixed-time cache reuse, changed
clouds at fixed light, forward/backward seeks, the period and long-session clocks. GPU
parity includes nonzero/negative/long-session times, and GPU seeks restore identical bytes.
Inspected `build/scroll-cpu.png` and actual GPU sky/reflections in `build/scroll-3.png` and
`build/scroll-60.png`. Capture poses differ because the scripted boat also moves; these
images are appearance checks, while the fixed-camera tests prove drift. The 600-frame
windowed GPU demo at 8x simulation speed exited 0 (`build/scroll-demo.log`); this is
automated smoke, not human motion/input/audio acceptance. Other platforms are unverified.
The final upload-spy extension then advanced wind across the bounded period during its
241-sample virtual cycle; rebuilt warning-clean and reran `gpu_surface_materials`, which
passed with zero sky texture writes (`build/scroll-upload-check.log`). Changed local
links/anchors and command options were checked; `git diff --check` passed.
