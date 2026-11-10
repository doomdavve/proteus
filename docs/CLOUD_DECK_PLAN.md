# Softer edges on the original cloud deck

Status, 2026-11-10: sparser three-tone reflection candidate implemented; user review open.
Open: user visual/motion acceptance, browser and other-platform review.
Done when: the original shallow deck looks less blocky in Day, its shared CPU/GPU
rendering passes verification, and the user accepts the result.
Next: user review of cloud coverage and three-tone shading in the water reflection.

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
4. **User review and closing.** Pending: play in Day and judge whether the outlines keep
   enough of the original character. Adjust only if needed; after acceptance confirm the
   spec, retain explicit limits and close this plan. Automated checks do not imply acceptance.

## Review commands

From `/private/tmp/proteus-cloud-deck`:

```sh
./build/wake_velocity --location outer --daylight day
./build/wake_velocity --location outer --daylight day --camera-height 4 \
  --camera-distance 48 --fixed-fov --resolution high --hdr off \
  --screenshot build/cloud-after-high.png --time 3
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
