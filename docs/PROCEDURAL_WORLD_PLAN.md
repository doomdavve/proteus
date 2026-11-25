# Skerry Reach procedural archipelago

**State** — A0–A4 native implementation and qualification complete locally, 2026-11-25; recipe 2 ready.
**Open** — The user found recipe 1 bland. Recipe 2's experience and place character await human review.
**Done when** — A player can leave a furnished home harbour, choose between two channels, reach a recognisable outer light and return. Two other seeds pass the same navigability checks and visibly differ.
**Next** — Drive the revised harbour, rock passage and enclosed lagoon; choose the next native content pass.

## The first boat trip

Launch at a sheltered jetty among net sheds and fishing houses. Frame the harbour mouth and a
recognisable navigation aid from the default helm. Through the mouth, a larger island divides
the water. The inner channel winds between inhabited shores. The wider passage takes the boat
past low skerries into an open reach, with an outer light visible on the headland. Both routes
reach the same coast and offer a way home. A landing, a beacon and the light each give the
player a reason to turn off course. Show a place name on first approach during the session.
The sense of exposure comes from the distance to shore, skyline and amount of open water.

Use a bounded coast a few kilometres across, small enough to explore in one sitting.
Generate geography before dressing. Protect a launch basin, fork and reunion with hull clearance.
Use the seed to vary island shapes, coast outlines and channel geometry. Site settlements on
buildable ground near landings, seamarks beside navigable passages and the light on a prominent
outer island. Low-poly roofs, rock, scrub and a few tall structures must read from the existing
low-resolution camera. Keep water, handling, lighting and controls on the existing game paths.

## Bake and runtime agreement

- The compiled location is `archipelago`, titled `SKERRY REACH`. The first shipped seed is
  **41717**. Reserve **41718** and **41719** for comparison. Alternative bakes go under
  `build/` or a temporary directory. Normal builds consume the checked-in default bake.
- Spawn and fleet origin are simulation `(0,0)` with heading zero facing decreasing Z. Reserve
  at least 60 m of clear forward run from the start. Mesh coordinates are east/up/north.
  Simulation coordinates are `x = -east`, `z = north`. Express all place approach points and
  capture starts in simulation coordinates. Put each approach in water beside its visible site.
- Reuse [map_pack.write](../tools/map_pack.py) with prefix `archipelago` for matched
  `assets/geometry/archipelago/scenery.bin` and `water.h`. Emit an indexed, consistently wound
  leading water surface with shared shoreline coordinates. Terrain and dressing follow the water.
  Set the bridge split after terrain and before the flat-coloured authored dressing; this
  legacy pack field separates material treatment and does not imply an actual bridge.
  Shape the shoreline around solid quays
  and piers wherever the boat should stop. Keep boats clear of decorative overhangs.
- The same bake emits `places.h` containing `static const BoatArchipelagoPlace archipelago_places[]`.
  Each initializer holds a short uppercase name, a simulation-space approach `Vector2` and a radius
  in metres. Entry zero is HOME HARBOUR. Keep the list under 16 entries. The manifest records
  seed, generator revision, mesh counts, approach points and useful review headings. C code
  obtains the list from `boat_archipelago_places(size_t *count)` in `core/boat_archipelago.h`.
- Add `BOAT_LOCATION_ARCHIPELAGO` after the existing IDs and export `boat_archipelago_map`. Keep
  river scenery, bridge overlays, default actors, contacts and traffic wakes tied to their
  existing locations. Provide the island chart with its own place names. A plain bundled
  `proteus.archipelago-free` package selects the existing local-authority free ride.

## Implementation and ownership

| Step | Files and work | Acceptance before closing |
| --- | --- | --- |
| A0 · plan | Coordinator owns this plan and the index. Keep commits on one local worktree branch. | Agents have the same map ID, spawn, coordinate convention, generated data contract and verification criteria. |
| A1 · seeded coast | Bake owner takes the generator, `assets/geometry/archipelago/` and its Python validator. Generate and dress the default seed. Give the comparison command a seed and an output directory. Write a reusable top-down preview for comparing coastlines and routes. | Rebake the default twice and compare shipped outputs. For all three seeds, test hull-clear water from home to sites and the two distinct branches around a separating island. Check widths along the approaches, meaningful seed differences and generated bounds. Record mesh sizes. |
| A2 · mapped world | Runtime owner takes `src/core/`, `src/render/`, `src/scenery/` and their C validators. Add the map, public place accessor, chart and location registration. Distinguish mapped water from Gothenburg fixtures in collision and both renderers. Preserve the Eriksberg test fixture. | Load the shipped pack through the real loader. Sample home, route approaches, island ground and outer edge with `boat_map_water`. Exercise mapped physics and reset. Check chart and frames for leaked river landmarks, ferry, streets, wakes or contacts. |
| A3 · player entry and discoveries | Game owner takes `apps/wake_velocity/`, `plugins/`, `src/plugins/`, room/net selection, catalog/schema tools, package staging and relevant tests. Expose the location from solo title and CLI, wire its default free package and show first-arrival names from A1 on the local ride. Clear visits on a fresh trip. Keep room hosting choices at their supported locations. | Start from CLI and title. Verify normal local free boating, return/reset and approach notices. Keep existing basin trial selection, saved title choice, explicit packages and outer voyage functional. Stage the small pack wherever builds expose the new location. |
| A4 · qualify and close | Coordinator owns integration fixes, test registration across owners, captures, plan and guide reconciliation. Keep each implementation step in its own focused commit. | Build and run full CTest, inspect home/channel/outer-coast views, run the native windowed demo and compare the alternate seeds. Move controls and entry contract to [playing](PLAYING.md) and [command line](COMMAND_LINE.md), bake contract to the asset guide and package rules to the [plugin spec](PLUGIN_API.md). Record evidence, limits and the next playtest in this plan. |

## Integration checks

The default solo path resolves a location-specific package. The plugin manifest currently limits
free locations, and the title normally cycles host locations through the same enum as solo. Audit
both alongside the saved basin-trial sentinel, CLI help, catalog tooling and native/browser
asset staging. The net content digest hashes every registered location. Preserve existing
multiplayer compatibility with the supported room worlds where possible, and record any observed
compatibility change. Keep arbitrary map transfer and room discovery out of this milestone.

The existing mapped chart draws Gothenburg landmarks without a location check. Mapped hull and
moving contact queries likewise dispatch to default harbour traffic when a scene is absent.
Inspect the simulation, wake emitters, CPU and GPU scene frames independently. Blank scene data
alone does not disable every permanent ferry or street ornament. Free-package objective marks
belong to room authorities, so first-arrival names use the local free-ride presentation.

From the worktree root, configure Debug, build, and run `ctest --preset full`. Record platform
and GPU-test setting. Capture the home departure with the default spawn and at least one channel
and one outer approach with `--start X,Z,DEGREES` from the manifest. Use
`--no-audio --renderer cpu --demo-idle --screenshot build/NAME.png --time 0` and inspect the
images at game scale. Exercise `--no-audio --location archipelago --demo --frames 180` in a
window. Record displayed images, scripted motion and human play as distinct evidence.

## Scope after this milestone

The [open loops board](README.md#open-loops) continues to own wake handling research, the sandbox
fun review, Vinga source acquisition, paused Gothenburg work, plugin loaded maps and multiplayer
qualification. This track defers new handling or weather physics, runtime seed entry, streaming,
persistent discovery saves, work jobs, sea traffic and multiplayer map distribution. The next
design decision follows a playable drive and a comparison of the three seed previews.

## First playable and user direction — 2026-11-25

Astra produced the plan; three implementation agents delivered the generator, isolated mapped
world and player entry. The first playable has six named approaches, ten route polylines,
solid jetty footprints and a free-ride package. An actual virtual-clock explorer drive completed
both sides of the loop and returned home: 8,457.9 m in 904.72 simulated seconds. This is a
navigation check, not human enjoyment or artistic acceptance.

The user tried the world and found it bland. That supersedes any implication that a connected
map and populated harbour satisfy the experience. The remaining weakness is place character:
similar low rounded islands, sparse settlements and little contrast between destinations.
The user requested native focus and reduced usage; browser qualification and further agent
calls are stopped. The existing web staging changes remain, with no browser-play claim.

A4's native refinement raises steep bare granite shoulders at Needle Pass, surrounds Hidden
Lagoon with wooded island lobes, gives Net Sheds a working waterfront and distinguishes the
bell house from the outer lighthouse. It preserves the route grammar and boat handling.
The shore settlements have low terrain terraces instead of long exposed foundations. Recipe 2
is implemented locally; its C navigation validator completes the same 8.46 km loop, and six
GPU destination captures plus a 180-frame native windowed demo were inspected with audio off.
These checks establish operation and agent visual review, not user acceptance of the revision.
The current contract is in [the world guide](PROCEDURAL_WORLD.md), the launch/controls guides
and the [plugin spec](PLUGIN_API.md#free-schema). The native implementation milestone is closed;
the experience review remains open rather than being inferred from the test results.

## Native verification — 2026-11-25

Apple Silicon (arm64), macOS 26.6.2, AppleClang 21, Debug, `PROTEUS_GPU_TESTS=ON`.

- Configure and complete native build passed. The final incremental build emitted no compiler
  warnings. Pinned raylib and ENet source directories were reused from the main checkout.
- `ctest --preset full` passed **653/653** in 119.94 s, including 36 GPU-labelled checks.
  The initial run exposed a clock validator that tested every mapped boat against river water;
  it now queries the selected world's map. A sandbox GPU check lacked the common GPU resource
  lock; it now takes that lock after initial parallel GLFW monitor-initialization failures.
  The final full run passed with desktop and loopback-socket access.
- `boat_archipelago` samples all ten generated routes in C (2,366 positions at at most 4 m
  spacing), verifies independent scenery/chart ownership and absence of river actors, then
  drives the explorer through 28 waypoints using ordinary helm/throttle inputs. Result:
  8,457.9 m, 904.72 simulated seconds, maximum route departure 32.10 m, no capsize, continuous
  3 m centre clearance and return within 18 m of home. The test advances a virtual clock.
- The generator validator checks seeds 41717, 41718 and 41719: a connected emitted water mesh,
  closed shoreline topology, 12 m sampled route/capture clearance, solid pier footprints,
  different outputs and identical repeated bakes on this host. Shipped geometry is checked
  independently of compressed bytes. Both alternate-seed PNG previews were inspected; their
  coastlines vary but share the same regional grammar.
- CPU reference frames and all six GPU destination frames were inspected. A native
  `--no-audio --location archipelago --demo --frames 180` run completed; its captured HUD
  shows the Home Harbour discovery notice. [Review images](procedural/README.md) preserve
  the revised departure, passage, fishing shore, bell house, lagoon and outer light.
- The public-room digest remains `d8edae7493480844`, matching the unchanged baseline.
  Private-local archipelago parity, public-room rejection, title persistence, package policy
  and virtual-clock discovery behavior pass. Sixteen added/changed documentation targets
  resolved, and `git diff --check` passed.

Recipe 2 has 48 island lobes, 337 trees, 20,640 water triangles and 38,568 full scenery
triangles. Its scenery pack is 267,644 bytes; the collision cache is about 1.9 MB. These are
resource counts, not a frame-time benchmark. The real-source `outer` pack has 648,229 total
triangles and is 5,899,814 bytes; density alone does not explain or establish enjoyment.
The generator has not been calibrated against real-source channel/settlement statistics.

No second user acceptance, audio listening, Release, packaged-app execution, Windows/Linux,
physical-device or browser-play qualification is claimed. Browser staging/build work already
performed is left local, but browser qualification stopped at the user's request. No work was
pushed or published. Jobs, dynamic traffic/life, persistence, tides, streaming and runtime seed
selection remain explicitly deferred; a sparse repeatable route grammar is still a limitation.
