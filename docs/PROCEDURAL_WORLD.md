# Skerry Reach

Skerry Reach (`archipelago`) is a fictional coastal map for solo free boating. It uses the
same boat handling, wakes, lighting, controls and CPU/GPU rendering paths as the other
locations. Its geography is generated offline, independently of the real-source Gothenburg
map. A normal build uses seed **41717** and needs no generation tools at startup.

## Exploring

Choose SINGLE PLAYER, PLACE SKERRY REACH, or launch from the repository root:

```sh
./build/wake_velocity --location archipelago
./build/wake_velocity --package proteus.archipelago-free
```

The boat starts at Home Harbour, facing the harbour mouth. The inner channels and more open
eastern passage connect the landings and outer light, with alternate routes home. Press `N`
for the chart. Place names appear on first approach during the current trip; they do not
create a persistent save or a timed objective. Normal free-boating reset returns to the
location's start. The bounded map ends at a collision boundary.

Needle Pass runs between steep granite shoulders. Hidden Lagoon is surrounded by lower,
wooded island lobes; its small shore terrace contrasts with the workshops and lifting derrick
at Net Sheds. Bell Rock has a timber bell house, while the outer headland carries the tall
lighthouse and keeper's house. These are static places to explore, without interaction jobs.

This location supports a plain free package and static baked scenery. Room hosting,
authored life/work scenes, moving sea traffic and runtime seed selection are unavailable.
New terrain does not imply new water physics: the open coast's exposure is expressed by
the geography and skyline. The current generator supplies no depth or tidal model.

[Native review images](procedural/README.md) show the departure, passage, shore sites and
lighthouse using the game's normal camera and lighting.

## Reproducing and comparing worlds

The generator uses Python's standard library and the existing scenery-pack writer:

```sh
# Reproduce the shipped world. This replaces its generated assets.
python3 tools/generate_archipelago.py

# Keep experiments separate from the compiled default.
python3 tools/generate_archipelago.py --seed 41718 --output build/skerry-41718
python3 tools/generate_archipelago.py --seed 41719 --output build/skerry-41719
python3 tools/generate_archipelago.py --self-test
```

Each output contains `scenery.bin`, `water.h`, `places.h`, `routes.h`, `manifest.json`,
`preview.svg` and `preview.png`. The SVG shows the coast, place names and proposed routes;
the PNG provides a compact coast-and-route preview. The manifest records the recipe
version, seed, bounds, mesh counts, hashes, approach positions and route samples. Changing
an experimental output directory does not change the running world. To play another seed,
bake it into `assets/geometry/archipelago/` and rebuild so collision and presentation agree;
restore the default bake afterwards. Keep comparison bakes under ignored `build/`.

The generator begins with connected passages and destinations, varies island contours and
relief, then places shore structures and vegetation. The seeds vary within a shared route
grammar; they do not yet generate entirely different regional layouts. Mesh positions are
metres, with simulation X reflected from mesh east: `simulation x = -mesh x`, Z unchanged.
The spawn is simulation `(0,0)`, heading zero towards decreasing Z.

## Resource contract

The core owns immutable water triangles and approach points compiled from the generated
headers. Collision caches remain caller-owned. The scenery loader reads the matching pack
and checks its leading water against the compiled mesh, just as it does for Gothenburg.
Rendering and file loading remain outside the core. Archipelago map identity suppresses
the Gothenburg kit, ferries, traffic, street actors and permanent chart landmarks.

The native and browser asset staging includes the new scenery where the location is offered.
The catalog entry `proteus.archipelago-free` supplies its free-ride definition. A different
seed is a different build-time world, not a downloaded room or a Lua scene replacement.
The map contains synthetic geometry and requires no geographic-source attribution.
Compressed bytes can vary between zlib versions; repeatability checks compare two local
bakes byte for byte and compare the shipped pack's decoded geometry independently.

## Comparing it with Gothenburg

Use the existing `outer` location as the real-source reference, keeping boat, renderer,
picture settings and lighting equal. Compare what happens at boating speed: whether the
next turn is tempting, the branches feel different, the landmarks help with orientation,
and the route home becomes recognizable. Coastline length and polygon counts describe
geometry, but cannot establish that one world is more enjoyable.

Review the default and at least two alternate seed previews before generalizing about the
generator. Inspect views at the helm as well as charts: narrow water, relief, shore access
and a recognizable destination matter more than an attractive overhead outline.
The [implementation plan](PROCEDURAL_WORLD_PLAN.md) records actual qualification evidence,
remaining limits and the next human playtest; this guide describes the supported behavior.
