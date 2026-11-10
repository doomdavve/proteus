# Softer edges on the original cloud deck

Status, 2026-11-10: user prefers the softer deck; deeper-blue Day preview rendered.
Open: user visual/motion acceptance, browser and other-platform review.
Done when: the original shallow deck looks less blocky in Day, its shared CPU/GPU
rendering passes verification, and the user accepts the result.
Next: user review of the deeper-blue colour preview; motion review remains open.

## Scope and decision

The user cancelled the rounded cumulus experiment after seeing it in the scene and
requested the original Minecraft-inspired clouds with less blocky edges. This branch
starts from the original implementation; none of the cumulus intersection, shading,
world-field or renderer changes is included. Keep the existing lighting, apparent deck
thickness, perspective and integer-scaled presentation. Day is the review setting.

Checked the [Open loops board](README.md#open-loops). This small track defers the lighting
clock/night/cargo acceptance, water-glitter attenuation, graphics Batch C, paused harbour
gameplay, plugin P5 and other existing loops. No new weather model, cloud controls,
wind simulation, sprites or volumetric cumulus renderer is scheduled.

## Steps

1. **Soften the original deck and verify locally.** Implemented: continuously sample the
   original broad noise scale, replace per-tile random notches with weaker interpolated
   detail, and retain the existing shared underside/rim mask. Native verification recorded below.
   The implementation and current contract are in [3D.md](3D.md#scene-lighting).
2. **User review and closing.** Pending: play in Day and judge whether the outlines keep
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

The existing sky recipe/cache,
CPU/GPU lighting/target and texture-lifecycle tests apply unchanged. This is a shared
mask generation change, with no per-frame work, new textures or shader changes.
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


### Deeper-blue preview, 2026-11-10

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
