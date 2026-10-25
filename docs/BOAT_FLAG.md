# Nordic ensigns and apparent wind

Implemented 2026-10-20 for the player boat in `boat_study`, including course playback
and its ghost. The [boating plan](BOATING_PLAN.md#swedish-ensign--2026-10-20) owns task
status and acceptance for the original Swedish ensign. The [speedboat skin record](BOATING_PLAN.md#speedboat-skins--2026-10-24)
owns the 2026-10-24 addition of Finnish, Danish and Norwegian artwork. Harbor/traffic recipes
retain their existing models.

## Wind and cloth

`src/boat_flag.[ch]` owns a 54-particle cloth sheet, 0.64 × 0.40 m, on a wooden
0.75 m starboard stern staff raked about 11° aft. Cross boundaries are mesh edges at the Swedish
flag's 5:2:9 horizontal and 4:2:4 vertical divisions. Both faces show the selected skin's
Nordic-cross pattern: Sweden blue/yellow, Finland white/blue, Denmark red/white, or Norway
red with a white-bordered navy cross. All countries use the accepted common fabric size and
cloth grid; their artwork is adapted to it, not a simulation of different flag proportions.
The hoist is fixed along the staff; the remaining fabric responds to forces.

World-space air has a prevailing 0.35 m/s X and 0.10 m/s Z component, with gentle,
continuous gusts and small spatial eddies. This is independent of the water current.
Each particle feels the air velocity minus its own world velocity: translation,
rotation, heave, pitch, and roll enter through the moving hoist and cloth inertia.
At rest, gravity dominates and the free fabric hangs almost straight down in loose folds.
At speed, apparent wind stretches the flag aft. Reversing and turning change the flow
without snapping the fly to the hull heading.

The 120 Hz update uses Verlet integration, gravity, normal air pressure, weak skin drag,
and eight iterations of structural/shear constraints with softer bend resistance.
Very soft bend resistance and compliant shear permit hanging folds at rest rather than
holding the fly out in the weak breeze. Stretch-only hoist tethers limit cumulative
extension. A small convected pressure perturbation models unresolved vortex shedding; its frequency and strength increase
with apparent air speed. This acts on the fabric as force, rather than positioning
vertices on an animated sine wave. The constraint approach follows
[Müller et al., Position Based Dynamics](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf).

This is a small visual cloth approximation, with bounded acceleration for contact jolts.
It does not solve fluid flow, cloth self-collision, or contact against the cabin/hull.
The staff placement keeps the fabric clear in the inspected idle/cruise/reverse/turn
fixtures; unusual wind-relative headings may still intersect the boat. Cloth forces
never affect handling, course timing, or the recording physics revision.

## Rendering and playback

`BoatPresentation` owns the live cloth history. `WaterfrontPlayerState` copies world-space vertices
into the frame snapshot, which owns its data and has no pointers into the simulation.
CPU and GPU use the same two-sided triangle topology. `boat_flag_skin_mesh` adds two render
columns and rows inside the cross bands (88 render vertices, 280 two-sided triangles),
interpolating the unchanged 54-particle cloth to resolve Norway's white border. Its output
uses caller-owned `BoatFlagMesh` scratch; the legacy `boat_flag_mesh` accessor still returns
the original 160-triangle Swedish mesh. GPU vertex/normal/color buffers update
once per scene frame and are reused for color, reflection, and shadow passes. The
stippled ghost has its own dynamic mesh and retains the existing ghost pass rules.
No image textures, additional shaders, or external assets are needed.

Recordings keep their existing format. `boat_replay_sample` still samples recorded hull
poses. `boat_replay_animate_flag` maintains separate caller-owned histories for playback
and ghosts, advances on fixed ticks, and holds still when paused. A backward seek or a
jump over 24 ticks reconstructs up to one second of cloth history from recorded poses.
The hoist is aligned to the exact interpolated pose between ticks. Sought cloth is a
visual approximation; it need not reproduce the live run's entire pre-start wind history
or exactly match continuous playback. Identical fresh seeks are repeatable.

## Inspect

Run these commands from the repository root after building:

```sh
./build/boat_study --location river --gpu-aa --waves --demo --frames 300
./build/boat_flag_review --renderer gpu --scenario cruise --time 3 --frames 300
./build/boat_flag_review --scenario idle --time 5 --screenshot build/flag-idle.png
./build/boat_flag_review --scenario reverse --time 5 --screenshot build/flag-reverse.png
./build/boat_flag_review --renderer gpu --scenario turn --time 5 \
  --screenshot build/flag-turn-gpu.png
./build/boat_flag_review --scenario cruise --time 5 --low-resolution \
  --screenshot build/flag-cruise-320.png
```

The review tool supports `idle`, `cruise`, `turn`, and `reverse`, and defaults to CPU,
a close stern-quarter camera, and a diagnostic 960×600 image. `--low-resolution` selects
the gameplay's native 320×200 size. `--time` accepts 0–60 seconds; `--frames` runs a
bounded windowed animation from that time. CPU screenshots need no window; GPU images
and windowed playback need a desktop graphics context. Gameplay presentation retains
nearest-neighbor integer scaling. From directly aft, a flag streaming astern is naturally
edge-on and its cross may be hard to see at native resolution.

`boat_flag_cloth` checks finite, repeatable motion; pinned hoists and bounded extension;
wind alignment at rest, forward, reverse, and during attitude changes; ongoing flutter;
near-vertical idle drape and settling after a stop; teleport recovery; and cross topology. The replay validator additionally
checks fixed-tick equivalence at different playback frame rates, pause stability, seek
repeatability, and exact interpolated hoist attachment. `boat_flag_cpu` and opt-in
`boat_flag_gpu` capture the close inspection fixtures.

## Speedboat review

The [player speedboat skins](BOAT_SKINS.md) select their matching flag while retaining these attachments. The close
review tool additionally accepts `--skin sweden|finland|denmark|norway` and
`--angle front|side|rear`; the default remains Sweden viewed from the rear quarter.
