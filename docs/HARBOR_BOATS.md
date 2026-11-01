# Generated boats and dressed berths

Status, 2026-10-18: living-city step 04h adds a parameterized boat model and two authored
berths. The generator now also supplies [travelling craft and a ferry](HARBOR_TRAFFIC.md).
Six leisure boats share a floating pontoon along the west dock bank; a workboat
lies beside equipment on the east quay. They appear in the river boating location (and appeared
in the demo, storyboards and Eriksberg location removed on 2026-10-25). The basin course is unchanged.
Placements and vessel designs are artistic additions, not surveyed marina facilities.
Since 2026-10-27 the same module also docks a Stena-style ferry ("VINGA LINE") across the river from the crane
([A3.5e](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a35e-result-2026-10-27)): one static model, posed
and moored like the fleet, with its own contact box.

## Boat recipes

`src/boat_model.[ch]` owns the renderer-independent visual generator. A `BoatModelSpec`
defines a low-poly hull, raised gunwale, inset deck/cockpit, seats, optional cabin and
windows, outboards, mast/furled sail, work-deck equipment, cleats, and optional fenders.
The named presets provide starting points rather than fixed assets:

| Preset | Length × beam | Character |
| --- | --- | --- |
| `BOAT_MODEL_SKIFF` | 4.5 × 1.9 m | Open, simple hull and benches; no outboard |
| `BOAT_MODEL_RUNABOUT` | 5.8 × 2.15 m | Low windshield/cabin, one outboard |
| `BOAT_MODEL_CRUISER` | 7.6 × 2.8 m | Broad cream hull, taller cabin, one outboard |
| `BOAT_MODEL_SAILBOAT` | 8.4 × 2.65 m | Narrower stern, low cabin, mast and furled sail |
| `BOAT_MODEL_WORKBOAT` | 11 × 3.7 m | Fuller bow, wheelhouse, aft equipment and lifting boom |
| `BOAT_MODEL_SPEEDBOAT` | 8.1 × 2.25 m | Slender pointed bow, low raked windshield, twin outboards |
| `BOAT_MODEL_FERRY` | 24 × 6.2 m | Passenger cabin, upper pilot house, funnel, rails and open aft deck |

The second moored sailboat varies length, beam, mast, cabin length, and colors from the
same recipe. All moored boats enable fenders; base presets leave them off.

| Parameter | Effect / supported range |
| --- | --- |
| `length`, `beam` | Overall hull dimensions: 3–40 m long, 1–10 m wide |
| `draft`, `freeboard` | Hull below/above its rest waterline: 0.15–1.5 m / 0.35–3 m |
| `bow_width`, `stern_width` | End width as a fraction of beam: 0.02–0.45 / 0.5–1 |
| `cabin_length`, `cabin_height`, `cabin_offset` | Cabin dimensions and fore/aft offset in metres; zero height omits the cabin |
| `windshield_rake` | Aft displacement per metre of cabin height, 0–0.6 |
| `mast_height` | Mast top above rest waterline, up to 12 m; zero omits rigging |
| `engines` | Zero, one, or two visible outboards |
| `passenger_deck` | Segmented passenger windows, upper bridge, funnel, rails and benches |
| `work_deck`, `fenders` | Equipment boom/winch and moored-boat fenders |
| `hull`, `deck`, `trim` | Flat color palette |

For example, a future visual variant can start with a preset and change a few fields:

```c
static BoatModel model;
BoatModelSpec spec = boat_model_preset(BOAT_MODEL_SPEEDBOAT);
spec.length = 7.8f;
spec.hull = (Color) { 38, 91, 132, 255 };
if (boat_model_generate(&model, spec)) {
    // model.mesh can be drawn by the software renderer or uploaded by the GPU path.
}
```

Generation is deterministic and allocates no memory. The caller owns fixed storage with
room for 1,024 triangles. The mesh view points into that storage: keep the model alive and
do not copy/move it after generation. Invalid/nonfinite dimensions, inverted cabin profiles,
and unsupported options return false; failed generation exposes no partial boat mesh.
Coordinates are metres, Y up, bow toward local -Z, and rest waterline at Y=0.
Generate/upload when a recipe changes, not on every frame.

## Shared berth evaluation

`src/waterfront_harbor.[ch]` owns placements, cached meshes, pontoon/quay equipment, and
`WaterfrontHarborFrame`. Static models upload once. Each snapshot evaluates the moored
hull poses, floating pontoon, sloping gangway and handrails, and two sagging lines per boat.
CPU, GPU color, reflection, and shadow passes consume the same transforms.

Calm water keeps the boats still. With `--waves`, four water-height samples provide a
bounded rigid heave/pitch/roll approximation from the existing `boat_water_query` surface.
Each boat's centre and heading remain fixed at its berth. Rope ends follow the posed
cleats and pontoon or fixed quay bollards; the mooring model has no rope solver, free
drift, or additional wave source. Travelling vessels are evaluated separately. Far GPU
water retains the existing displacement-to-flat transition; close berth views use the
fully displaced patch. Reflections remain planar.

L2.1 (2026-10-30) added caller-owned berth inputs without changing the compiled default.
`WaterfrontBerth` now carries a stable ID, a registered recipe, and a mooring rule;
`waterfront_harbor_evaluate_scene`, `..._contact_height_scene` and `..._camera_clear_scene`
select either the compiled seven or an authored list. Mesh generation is per recipe, so a
moved/removed authored berth cannot leave its compiled hull visible, contacted or
camera-blocking. The existing NULL-scene wrappers and frame fields are unchanged for current
callers. Authored rope anchors still use the compiled pontoon's cleat region until L2.3 adds
registered dock modules; authored quay mooring is not exposed.

L2.2 (2026-10-30) wires the accepted scene into `BoatWorld` and `BoatViewState`, so the same
berth list drives hull contacts, the CPU/GPU harbor pass and the shared scene frame. The first
`plugins/harbor-life` package exposes two berths; `scene.marina.berths` is validated against
water, pairwise clearance, the entrance and restart shelters. L2.3 adds registered dock modules:
a 14 m `pontoon_small`, the existing long pontoon and local quay recipe, and a gangway with a
land anchor and named pontoon attachment. Authored berth ropes use the nearest authored pontoon;
a present empty module list disables the compiled dock furniture. The ferry and bridge remain
independent compiled actors.

Eriksberg marina layout (2026-11-01): `plugins/harbor-life` now fills all seven authored berths
with a mostly white/cream leisure fleet in the finger slips of one 37 m pontoon, and adds shore
equipment at `(38, 173)`. The entrance moved to `(4, 140)` so the approach lane clears the longer
row; the passing craft's registered loop stays north of the pontoon. A fourth module slot remains
free. The package README records what the scene API still cannot express, including the compiled
crane/buildings, a floating quay platform, per-boat appearance, berth occupancy schedules and
gameplay lighting.

Player contacts reuse the shoreline's forgiving contact response with stationary, padded
oriented envelopes for moored hulls, main pontoon, fingers, and gangway. Envelopes include
fenders/outboards and allowance for gentle wave attitude; they do not simulate two moving
rigid bodies or exact hull-to-hull contact. The central channel, spawn, and existing review
routes remain open. Camera-route clearance includes conservative berth envelopes; arbitrary
player follow-camera occlusion near shore remains unresolved.

## Inspect and verify

Run from the repository root after building:

```sh
./build/wake_velocity --location river --gpu-aa --waves
./build/harbor_review --view marina --waves
./build/harbor_review --view workboat --waves
./build/harbor_review --view overview
mkdir -p build/living-city/harbor
./build/harbor_review --view marina --waves --time 2 \
  --screenshot build/living-city/harbor/marina.png
./build/harbor_review --renderer cpu --view workboat \
  --screenshot build/living-city/harbor/workboat-cpu.png
./build/harbor_review --view marina --waves --frames 300
```

The review tool defaults to GPU with the existing 4×4 supersampling/downsample shader.
CPU screenshots need no window; GPU capture and playback require desktop graphics access.
`--resolution` accepts 320x180, 320x200, and 320x240; `--time` selects a deterministic pose
from 0 seconds to 30 days (2,592,000 s), parsed as double world time for long-session probes. `--frames` bounds windowed playback at 60 Hz; Esc exits. Timing output
reports CPU rendering or GPU CPU-submission cost, including downsampling, not GPU execution.

`harbor_geometry` checks seven presets and 35 parameter variants, finite/nondegenerate
bounded geometry, repeatable generation, invalid recipes, water placement in both map crops,
central-channel clearance, direct-seek consistency, wave bounds, rope attachment continuity,
modeled pontoon anchors, and real player contact/reverse recovery. CTest also captures CPU marina and opt-in GPU
wave marina views. The existing route validator checks all three camera paths, and course
recording/replay tests retain the unchanged basin physics baseline.

## Future playable boats

This step establishes visual recipes and shared rendering. The player still uses the existing
hull and handling, with its own [wind-driven Swedish ensign](BOAT_FLAG.md). There are no prices, unlocks, save migrations, selectable craft, or speed tiers.
The skiff-to-speedboat visual range can support that later design without tying appearance
to an arbitrary speed multiplier now.

Before making recipes playable, separate steerable propulsion attachments from the static mesh,
introduce separately tuned handling data, derive collision and buoyancy support dimensions from
the selected hull, validate balance through driving, and bump
recording physics revisions when handling changes. Propulsion, mass, planing, and rough-water
comfort should provide useful differences. The upgrade/progression system remains deferred in
[the boating design](BOATING_PLAN.md); new interacting city actors still start with step 05 in
[the living-city plan](LIVING_CITY_PLAN.md).

## Arcade contact follow-up — 2026-10-22

Player contacts now require vertical overlap with the transformed conservative berth
hull/cabin/mast bounds, or the pontoon/quay height interval. Horizontal envelopes stay
forgiving. Shoreline and basin limits remain intentional boundaries at any flight height.
The boats are still kinematic; [arcade implementation](WAKE_ARCADE_IMPLEMENTATION.md)
owns the dynamic basin rival, contact tests and limitations.

## Player cosmetics

The playable craft now uses a separate [speedboat skin](BOAT_SKINS.md) presentation recipe.
Its four liveries share geometry and the existing handling/contact class; this does not adopt
the harbor presets as playable performance classes or change the moored fleet.
