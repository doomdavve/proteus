# Skerry Reach procedural archipelago

**State** — A0–A10 accepted in PR #232; reference-led red-house rebuild implemented locally.
**Open** — GPU/motion and user review of the shore fixes; broader platform/night qualification.
**Done when** — Shore fixes pass full checks and native/user review; A0–A10 remains complete.
**Next** — Review the rebuilt houses, home rock shelf and villager poses in a native window.

## Additional art direction — 2026-11-25

The user supplied four photographic references for the next visual iterations. These are
art direction, not evidence of implemented features or acceptance of recipe 3. Local copies
preserve the references; they are not game textures. Original source URLs and reuse rights
were not supplied; the fishing-boat image visibly credits `www.klasnilssonsfotosida.se`.

- **[Fishing boats](procedural/references/fishing-boat.png):** use the character of a
  traditional Swedish wooden working boat: warm ochre/yellow planked hull, pronounced bow,
  dark rubbing strakes, repeated hanging black fenders, a compact aft wheelhouse, mast,
  rigging and Swedish flag. Prioritize silhouette, proportions and readable fittings at the
  game's resolution. The current generic workboat is not the finished visual target.
- **[Exposed sea](procedural/references/exposed-sea.png):** aim for wind, weight and movement:
  blue-green uneven water, bright broken highlights, foam and spray where waves strike low,
  rounded granite. Contrast exposed passages with sheltered harbour water. This is an
  aspirational reference; full breaking surf and spray are beyond the current implementation.
- **[Sunset](procedural/references/sunset.png):** enable a day/night cycle so exploration can
  reach low orange sunlight, pink/lilac sky and water, a narrow reflected sun path and dark
  distant skerries. Preserve the quiet sheltered-water mood and broad, smooth granite with
  shallow pools and sparse vegetation. Coordinate cycle transitions and controls with the
  [sunset plan](SUNSET_PLAN.md); recording this request does not implement the cycle.
- **[Marinas](procedural/references/marina.png):** marinas are a high priority for the place's
  identity. Compose inhabited rocky coves with clusters of red timber boathouses, orange/red
  pitched roofs, pale trim, jetties and small footbridges, moored boats and sailboat masts.
  Fit buildings into the rock and sparse green pockets; keep navigable entrances legible.
  Use the [marina authoring work](MARINA_AUTHORING_PLAN.md) where applicable. Shore houses
  and passing craft alone do not fulfill this direction.

**Next art task:** use these references in recipe 3's native review, then scope the fishing
boat and inhabited-marina pass alongside the requested day/night enablement. Keep the existing
open-loop owners and deferred AI/jobs/physical wakes separate. No new runtime behavior or
visual verification is claimed by this documentation update; reference files and local links
were checked on 2026-11-25. Implementation, build/full CTest and visual review remain for the
chosen follow-up step.

## Art-direction follow-up — 2026-11-25

Continue the existing local branch with no browser work or additional agents.

- A7: traditional ochre wooden fishing workboat, aft wheelhouse, planking, dark fenders,
  working mast/rigging and Swedish flag; keep generated meshes and contact bounds in agreement.
- A8: sheltered Home Harbour marina with moored small craft, sailboat masts and connected
  timber jetties against the red boathouse frontage. Preserve the launch and traffic corridors;
  share boat poses and contacts across native renderers and simulation.
- A9: enable the existing 900-simulation-second day/night lighting cycle for solo archipelago
  exploration; start at noon, pause with simulation and seek deterministically. Explicit fixed
  lighting overrides it. Keep other locations and multiplayer ownership unchanged.
- A10: full native build/CTest, deterministic cycle/berth clearance checks, representative
  CPU/GPU captures and windowed motion check; update the current world guide and evidence.

Breaking surf/spray remains inspiration, deferred with the existing physical-wake, reactive-AI,
job/economy and browser work. Human acceptance remains separate from automated qualification.

## The first boat trip

Launch at a sheltered jetty among net sheds and fishing houses. Frame the harbour mouth and a
recognisable navigation aid from the default helm. Through the mouth, a larger island divides
the water. The inner channel winds between inhabited shores. The wider passage takes the boat
past low skerries into an open reach, with an outer light visible on the headland. Both routes
reach the same coast and offer a way home. A landing, a beacon and the light each give the
player a reason to turn off course. Show a place name on first approach during the session.
The sense of exposure comes from the distance to shore, skyline and amount of open water.

## Native life follow-up — 2026-11-25

The user found recipe 2 better and requested finer shores and a sense of archipelago life.
Continue on the same local milestone branch, with no browser work or additional agents.

- A5: finer shores and local life. Halve shore/terrain spacing from 32 m to 16 m; keep clear
  offshore water at 32 m and stitch transitions so collision has no false shore edges.
  Revalidate all three seeds. Reuse native traffic routes and gull evaluation for a harbour launch, fishing circuit
  and outer-coast pleasure craft. Add small shore residents at inhabited sites. Evaluate all
  life from explicit simulation time, share CPU/GPU poses and moving hull contacts, and keep
  Gothenburg's actors absent. Traffic trails are cosmetic in this step, as for authored routes.
- A6: full native build/tests, route clearance and collision checks, seek/pause consistency,
  representative finer-shore/life captures, windowed demo and current spec/guide updates.

This continues to defer jobs/economy, interactive docking, reactive traffic AI, persistent
discovery, physical traffic wakes, runtime seeds and streaming. Other open-loop owners in the
index remain separate; the paused Gothenburg track and browser qualification are not resumed.

### A5 implementation — 2026-11-25

Recipe 3 uses a 16 m land/shore lattice and stitched 32 m clear offshore water. The default
bake has 70,992 triangles (36,192 water), up from recipe 2's 38,568 total. The scenery pack
is 483,805 bytes and the collision cache is 2,629,096 bytes. Terrain detail increases without
refining the entire open sea to the same spacing.

Native life is four independent scheduled craft (harbour launch, workboat, outer sailboat and
lagoon runabout), 26 gulls and seven shore residents on the default seed. Their shared-time
evaluation supplies CPU/GPU poses; moving hull contacts use the same native route data.
People stay out of water and building footprints, and a smooth conservative terrain envelope
keeps the gulls above the granite. Foam trails are cosmetic; the boats do not yield or berth.

Focused native validation passes route structure, model-sized clearance over two periods
including negative time, moving/stopped phases, hull/height contacts, shore-foot paths,
terrain clearance and repeatable seeks. The explorer completes 8,462.1 m in 909.22 simulated
seconds with maximum route departure 32.11 m. Native CPU/GPU frames and the 1,200-frame
windowed idle-helm check have been inspected with audio disabled. A6 closes native qualification
below; the current contract is in [the world guide](PROCEDURAL_WORLD.md).

### A6 qualification and closing — 2026-11-25

Apple Silicon/macOS 26.6.2, Debug, `PROTEUS_GPU_TESTS=ON`:

- Complete native build passes without compiler warnings in the final build log.
- `ctest --preset full` passes **653/653** in 119.13 s, including 36 GPU-labelled checks.
  This includes the private-local archipelago parity and existing basin/Gothenburg checks.
- The three-seed generator validator passes stitched topology, 12 m navigation clearance,
  8 m sampled rounded traffic-path clearance, resident water/building exclusion and local
  repeatability/shipped geometry. The previous recipe's coarse water budget is retained
  offshore; no streaming system or new map loader is introduced.
- The native C validator passes all ten navigation routes and four timetables, model-sized
  hull clearance over negative and positive time, height-aware contacts, moving/stopped phases,
  deterministic seeking, land-foot paths and bird terrain clearance. Its direct explorer
  drive returns home after 8.46 km in 909.22 simulated seconds with traffic present.
- CPU/GPU review captures and a 1,200-frame native `--demo-idle --demo` windowed check complete
  with `--no-audio`. The [recipe 3 images](procedural/README.md#recipe-3--finer-shores-and-local-life-history)
  preserve the launch waiting/departing and the finer passage beside the old recipe 2 view.
  These establish scripted operation and agent inspection, not human acceptance.

A5–A6 are complete and the runtime/behavior contract is in the owning world and playing guides.
The next task is a human native drive. No new browser, audio, Release, packaged-app,
Windows/Linux or physical-device qualification is claimed. Traffic is scheduled and does not
yield, foam is cosmetic, shore activity has no jobs/interactions, and seeds retain one regional
grammar. These limitations stay open rather than blocking this first native life layer.

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
persistent discovery saves, work jobs, reactive sea traffic and multiplayer map distribution. The next
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

## Recipe 2 native verification — 2026-11-25 (history)

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

At this checkpoint no second user acceptance, audio listening, Release, packaged-app execution, Windows/Linux,
physical-device or browser-play qualification is claimed. Browser staging/build work already
performed is left local, but browser qualification stopped at the user's request. No work was
pushed or published. Jobs, dynamic traffic/life, persistence, tides, streaming and runtime seed
selection remain explicitly deferred; a sparse repeatable route grammar is still a limitation.

### A7 result — 2026-11-25

The local fishing circuit now resolves an 11 m ochre wooden boat with a raised bow, aft
wheelhouse, plank seams, twelve dark hanging fenders, mast/stays and a Swedish ensign.
Its generated mesh uses 730 triangles within the existing 1,024-triangle storage; the route
spec supplies matching contact bounds. The registered generic workboat remains unchanged,
including its authored color/name budget. Route mesh caching distinguishes the local variant.

Native Debug build, existing harbor appearance/geometry checks and the full 653-test suite
passed on Apple Silicon/macOS 26.6.2 with GPU tests enabled. The local validator checks the
fishing mesh against its declared bounds; the close GPU fishing view was inspected. These are
automated/agent checks, not human acceptance. A8 is next; breaking surf stays deferred.

### A8 result — 2026-11-25

Recipe 4 gives Home Harbour two moored runabouts and two seven-metre sailboats, eight
jetty-to-hull mooring lines and red boathouse frontages. The generated water geometry and
central departure route are retained. Berths sit clear of both shore and solid pier footprints;
local hull-only contacts share the rendered specs/poses without importing Gothenburg fixtures.
CPU/GPU rendering shares the native harbor hull cache, including reflection and shadow poses.

The three-seed generator validator and native berth/route/seek checks pass; the native Debug
build and 653-test full suite passed with GPU tests enabled. Default-view CPU/GPU and a closer
GPU marina capture were inspected. Four boats are a first small marina composition; interactive
docking, occupancy services and harbour night lamps remain deferred. Human review is open.

### A9 result — 2026-11-25

Solo archipelago views now supply the existing shared lighting renderer with noon plus
24 hours per 900 displayed simulation seconds. There is no accumulated render clock: pause,
reset, scaled time and backwards seeks follow the session's simulation time. The title-default
DAY starts the cycle; the other saved choices and explicit CLI light stay fixed. Working
Harbour and other locations retain their existing ownership. Replay/room clock formats remain
unchanged. Skerry Reach screenshot time now accepts 0..900 s; other mode limits are retained.

Virtual-time checks cover noon/evening/midnight/dawn/noon, negative/backwards seeks and the
analytic glitter clock. Native CPU/GPU captures at 225 s (18:00) and 450 s (midnight) were
inspected; an explicit fixed 18:00 title-start capture and the 1,800-frame windowed neutral-helm
8× run exited successfully with audio disabled. The latter advanced 14,556 ticks (121.3
simulated seconds); it is a motion smoke check, not a measured full-cycle speed qualification.
Night remains deliberately dark with no new marina lamps. A10 records the final suite.

### A10 qualification and closing — 2026-11-26

A7–A10 close this native implementation milestone. The current behavior, cycle controls,
marina contacts, reproduction commands and remaining limitations now live in
[the world guide](PROCEDURAL_WORLD.md); dated results stay here. The
[recipe 4 gallery](procedural/README.md#recipe-4--fishing-boat-marina-and-daynight)
contains the inspected fishing/marina GPU frames, GPU sunset/midnight and CPU sunset reference.

Apple Silicon/macOS 26.6.2, native Debug, `PROTEUS_GPU_TESTS=ON`:

- Complete native build passed without warnings in the final build log.
- Final `ctest --preset full`: **655/655 passed in 118.70 s**, including 36 GPU checks.
  This includes the three-seed generator validator, local-session parity, hull/height and
  marina-water clearance, cycle seek/glitter tests and the two capture-limit regressions.
- The native route drive still completes 8,462.1 m in 909.22 simulated seconds, with maximum
  route departure 32.11 m. New moorings leave a 12 m central departure corridor clear.
- CPU/GPU home views, a close GPU fishing/marina view, matched sunset/midnight cycle
  captures and the explicit 18:00 title-start override were inspected. Captures at 225/450 s
  each ran through the private local session (27,000/54,000 ticks); the frame light came from
  the same displayed simulation time. All graphics checks used `--no-audio`.
- The 1,800-frame native windowed 8× neutral-helm run exited 0 and its frame was inspected;
  it advanced 121.3 simulated seconds. No full-cycle real-time performance claim is made.
- All 353 local Markdown links/anchors checked across the changed documentation resolve;
  `git diff --check` passes. Evidence logs and extra captures are in ignored `build/a10-*`.

The first sandbox full run was interrupted after socket failures and a stalled GPU lane;
complete native reruns with socket/graphics access passed. No test failures are outstanding.
Human input/gameplay, audio listening, full live-cycle timing, night acceptance, browser,
Windows/Linux and Release qualification remain open. The small marina has no dedicated night
lamps or interactive berth services. Breaking surf/spray, reactive AI, jobs/economy, physical
traffic wakes, runtime seeds and streaming stay deferred. No PR, push or publication is made.

## User acceptance and PR integration — 2026-11-26

The user accepted the result ("I like it") and explicitly requested merging through a PR.
This accepts the current native art/life milestone; it does not establish unperformed audio,
platform or full-cycle timing checks. Current main is integrated before PR qualification,
including the newer building windows and RESOLUTION/EDGES title controls. The playing-guide
conflict preserves those controls alongside the Skerry Reach cycle instructions.

Integrated native verification passed on Apple Silicon/macOS 26.6.2: warning-free Debug build
and **655/655 full CTests in 118.51 s**, including 36 GPU checks. An integrated title-start
archipelago capture at 225 s was inspected: the cycle reaches sunset with the newer title
resolution controls intact. Graphics checks used `--no-audio`. Broader interactive/platform
limits above remain unchanged. [PR #232](https://github.com/doomdavve/proteus/pull/232) owns
remote CI results and merge status; no release/deployment is part of this integration.

## Shore feedback fixes — 2026-11-27

The user reported outstretched, zombie-like arms and gaps in the houses. Residents now
have shoulder-pivoted upper arms, slightly bent forearms, visible hands and a smaller
walking swing. House roofs include a bottom soffit across the overhang, closing the opening
between the extended gables and the wall box. Corner trim uses four protruding posts
instead of nearly flush panels. The default scenery pack and manifest were regenerated.

This follow-up defers the existing open-loop board items, reactive life and new jobs.
The existing Debug build completed on macOS; the dependency's stb_vorbis warning remains.
CPU Home Harbour stills at 4 s were inspected (`build/shore-close.png` and
`build/shore-detail.png`, `--no-audio --renderer cpu --demo-idle`, start `-28,-24,-90`).
An angled house view (`build/house-angle.png`) uses the same options at 1 s and
start `-28,-24,-55`. These game-scale views do not replace a close native GPU review.
The windowed no-audio 180-frame check was attempted, but GLFW could not initialize the
platform/monitor in this session, including outside the sandbox. GPU appearance, motion
and human acceptance remain unverified. The first sandboxed full CTest run passed
602/619 tests; 17 networking/process tests failed while local sockets were denied.
The full run outside the sandbox passed all 619 tests (65.90 s, macOS Debug;
GPU qualification tests were not enabled).
`clang-format --dry-run --Werror` and `git diff --check` pass.

## Red-house rebuild — 2026-11-27

The user rejected the capped-box appearance and supplied a
[red timber house](procedural/references/red-timber-house.png) and
[quayside house cluster](procedural/references/red-quay-houses.png), then authorized rebuilding
the recipe. These user-provided photographs are art references, not textures; original
source URLs and reuse rights were not supplied. This supersedes the initial roof-cap fix
above. Existing open-loop owners, reactive life, jobs, multiplayer and broader platform
qualification remain deferred.

Implemented: lower walls, warm terracotta pitched roofs and subtle tile courses, white
verge/eave boards and corner posts, larger divided windows, framed doors, attic windows,
low entrance steps and sparse timber battens. Small huts omit chimneys; the existing home
shore groups vary in depth as well as width. Stone skirts follow the ground. Home Harbour's
land is a low rock shelf blended back into the hills so the foundations no longer require
tall podiums. Its water contour and navigation remain unchanged.

Gables now share the actual wall footprint; sloping roof slabs have thickness and closed
undersides, and white verge boards stand clear of the roof surfaces. A new geometric
regression probes both wall/gable joins around the eave height and all four overhang
undersides on three building sizes. Isolated front/back house and hut views use the actual
CPU renderer at 320×200 via a temporary review harness in ignored `build/`.
[Review images](procedural/README.md#red-house-rebuild--2026-11-27) distinguish these
fixtures from in-world captures.

Verification on macOS Debug: the final build passes without new warnings. CPU Home Harbour
(1 s, `--start -28,-24,-55`) and Net Sheds (0 s, `--start -683.287,-650.992,-90`)
captures were inspected with `--no-audio --renderer cpu --demo-idle`; the home houses sit
on low foundations and both sites show the new roof/trim silhouettes. The geometric seam
regression, changed documentation links and `git diff --check` pass.
`ctest --preset full -j 2` completed 616/619 successfully; three tests timed out amid
large elapsed-time jumps (8,316.45 s total). An earlier eight-job run also timed out.
`caffeinate -i ctest --preset full --rerun-failed -j 1` passed the remaining three in
78.70 s: plugin authoring 29.85 s, plugin captures 14.47 s, multi-seed generation 34.37 s.
All 619 checks therefore have successful final results, with timeout retries recorded;
GPU qualification tests were not enabled. No further code changes followed these checks.
The no-audio 180-frame GPU demo was attempted again, but GLFW could not initialize a
monitor/platform. Native GPU appearance, motion and user acceptance remain open.
