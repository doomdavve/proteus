# Lua gameplay plugins and live authoring

Status, 2026-10-30: **P0–P3 implemented for trusted native solo authoring.** Native solo `outer` sessions load and
live-edit the Långedrag voyage; the local river trial now loads race descriptors through the
same restricted evaluator and revision host. The compiled race remains the network default.
P3.1 provides atomic objective transactions and P3.2 connects lossless native voyage events
to an opt-in shelter-tour objective with a native HUD row. P3.3a adds deterministic tick timers
and their checkpoint/reload contract. P3.3b adds schema-aware native migration, explicit
resource retirement/composition and a complete compatible solo checkpoint/inspection harness.
P3.4 accounts retained/native storage, separates instruction budgets and measures synchronous
staging. P4.1 adds discovery and isolated native validation; P4.2 adds machine-readable schemas,
bounded isolated voyage/race previews and disposable-process live descriptor staging for trusted
native edits. P4.3 adds browser source bundles and isolated descriptor import/reload/export; P4.9 extends
that workflow to optional voyage objectives/timers; PD4 extends browser imports to solo free/course kinds and authored scenes while race and match stay room content. P4.4 adds pinned draft workspace
read/write, P4.5 pinned CPU frame capture with curated anchors/camera bookmarks, P4.6 a live tool
bridge to a running native game and P4.7 those tools in the macOS/Windows release packages.
P4.8 qualifies browser authoring on Chromium, Firefox and WebKit in CI, with tab freeze,
navigation and repeated-session cleanup, and records native costs on Linux and Windows. Full
browser qualification is now [manual-only](WEB_BUILD.md#browser-authoring-ci-lanes-2026-10-30);
relevant PRs retain Node contracts and Wasm compilation/packaging.
P4.9 adds a bounded objective wire protocol, reusable isolated Wasm evaluator and asynchronous
init/update/migration/Undo with host-owned in-session state. There is no in-game agent. The agreed direction
is embedded Lua for gameplay authoring, with C retaining physics, collisions, wake math, rendering and networking.
This document owns the boundary, workflow and staged implementation; [the plugin API](PLUGIN_API.md)
owns the current contract. [All plans](README.md).

Completed task: **P4.9 browser objective isolation**, following
[P4.8 automated target/browser qualification](#p48-result-2026-10-30); see the
[P4.9 result](#p49-result-2026-10-30). Prior results retain their dated verification evidence.
**Next task: the [plugins-by-default plan](PLUGIN_DEFAULT_PLAN.md)'s [PD1](PLUGIN_DEFAULT_PLAN.md#pd1--bundled-runtime-and-complete-solo-coverage) and [PC](PLUGIN_DEFAULT_PLAN.md#pc--live-console-and-edits-without-reset) are merged; the [Rooms package](PLUGIN_DEFAULT_PLAN.md#packages) is next.**
That plan owns making plugins always on, including the room part of P5: its first room scenario is
the river race, carried to clients as validated data rather than matching preinstalled source.
Authority callbacks, commands and session recordings remain P5 work after it. Physical-device and human qualification need the
user and remain separately tracked. Interactive preview controls were deferred on 2026-10-30
at the user's choice; they remain unspecified. Native startup, objective updates and Apply-time
init/migration still evaluate trusted Lua synchronously. Browser voyages evaluate all imported
Lua in workers; simulation waits at a callback boundary while rendering/UI remain responsive.
Browser race integration remains separate from the supported voyage subset.
Human P3 migration/Undo/recovery acceptance and broader platform timing remain separate work.
The [location-life track](#location-life-track-proposed-2026-10-30) now gives marina,
traffic/ferry, tram, wildlife and lighting authoring explicit implementation gates; L1.1's
scene contract/site fixture, the L1.1–L1.4 scene envelope path and L2.1–L2.3's native berth/module
parameterization plus the two-berth `plugins/harbor-life` package are complete. The first
playable L4.3 harbor gate is implemented and awaits human review. The milestones below are this plan's
P0–P6, independent of similarly named foundation tasks.

## Intended experience and scope

A player or an LLM should be able to change a destination, create a course or add a small
objective, save a short Lua file, and see the result in the running game. The game should
explain an invalid edit with its file and offending field, plus a source line where available,
while retaining the last working revision. A reload must not silently lose voyage progress or leave half a new course
in the world. There should be a short path from an idea to a repeatable headless test, a
screenshot and a live preview.

Start by expressing existing content in Lua. The first plugin authors the current Långedrag
route, its shelters, controlled-speed arrival and existing exposed-water recipe. C still
evaluates `BoatVoyage`. Then express the river sprint's course and rules through the same
loading/reload pipeline, with `BoatRace` still deciding crossings, placement and results.
Only after these small examples work should Lua callbacks introduce new objective logic.

The longer-term goal is an in-game creative workbench: “put a rescue stop beyond the bridge,”
“make a slalom through these wakes,” or “give this evening voyage a delivery objective.”
An assistant can inspect supported capabilities, author a plugin, test it, show the change
and apply it live. Claude with the user's API key is a possible provider; the tool protocol
must work with other providers and with an external coding agent. No model call belongs in
the simulation tick, and no plugin receives an API key.

| C owns | Lua may author through an explicit capability |
| --- | --- |
| Fixed-step boat motion, hull/shore contacts, safe placement, handling classes | Select supported rules/presets; request supported recovery or actor actions |
| Water queries, derivatives, wake emission, storm evaluation, CPU/GPU parity | Describe validated weather/source parameters and their scheduled changes |
| Course crossings, the existing voyage/race/match engines | Supply their definitions; later compose objectives and react to their events |
| Actor geometry, route evaluation, collision envelopes and resource lifetime | Select supported actor recipes and routes once a native adapter exists |
| Scene evaluation, draw submission, fonts, audio devices and asset decoding | Produce bounded HUD descriptions and request registered cues/palettes |
| Authority, transport, checkpointing, prediction and replay formats | Produce validated commands/state at host-controlled ticks |

This is a small gameplay layer over concrete owners. It does not require an ECS, arbitrary
native extensions, a general engine rewrite, a package dependency resolver, or moving every
existing game rule to Lua. It follows the concrete-ownership recommendation in the dated
[architecture review](ARCHITECTURE_REVIEW.md). A feature that needs a new physical primitive
first needs a C implementation and its usual verification; a script cannot manufacture that
capability by inventing an API name.

## Reviewed capability inventory

“Present” here means present in this checkout, not a claim that every package, platform or
historical worktree has shipped the same revision. The linked owning plans distinguish
implementation, automated checks, manual playback and user acceptance. Older entries retain
superseded proposals; the source interfaces below are the integration reference.

| Area and grounded reference | Present foundation | Suitable plugin work and remaining native work |
| --- | --- | --- |
| [Voyage/adventure plan](FREE_RIDING_ADVENTURE_PLAN.md), [A3.4 result and playtest](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a34-result-2026-10-26), [`boat_voyage.h`](../src/boat_voyage.h) and [implementation](../src/boat_voyage.c) | `outer` has the Långedrag destination, four shelters, controlled-speed arrival, recovery and a fixed exposed rough-water patch. The route is a compiled `BoatVoyageRoute`; `BoatSession` owns progress. Later terrain, dressing and the docked ferry are implemented, with qualification limits in the adventure ledger. | Best first slice: move route data to Lua while retaining C behavior. Optional errands, multiple destinations and weather progression can follow. Vinga coverage, regional exposure/weather transitions, further landmarks and bounded detailed-asset loading are separate unfinished adventure work. |
| [River race](RACE_PLAN.md), [`boat_race.h`](../src/boat_race.h), [`boat_race.c`](../src/boat_race.c) | Shortened river sprint, authored gates/grid, crossing ferry, moving storm, authoritative phases, recoveries, DNF, results and network race support. `BoatRaceCourse` and `BoatRaceRules` are already separate data types. | Second descriptor slice: edit gates, durations and supported ferry/storm parameters. Keep swept crossings, tie rules and recovery in C. The full downtown start, further race improvements and some pacing/platform review remain open. Do not confuse this course with the basin's distinct four-gate [`BoatCourse`](../src/boat_course.h). |
| [Living city](LIVING_CITY_PLAN.md), [`waterfront_scene.h`](../src/waterfront_scene.h) | Shared explicit-time scene snapshots, city dressing, cars/lamps, travelling vessels and reactive gulls; CPU/GPU consume shared evaluated actors. | Later schedule and orchestrate exposed actors. Pedestrians/cyclists, richer road interactions, building activity, perched gulls and final polish remain planned. There is no generic spawnable NPC registry, navigation system or conversation engine to bind today. |
| [Harbor boats](HARBOR_BOATS.md), [traffic](HARBOR_TRAFFIC.md), [`waterfront_harbor.h`](../src/waterfront_harbor.h), [`waterfront_traffic.h`](../src/waterfront_traffic.h) | Generated boat recipes, fixed berths, shared wave poses, mooring/contact envelopes, four river craft, upstream ferries/schooner with pier stops and coordinated bridge lift in [`waterfront_upstream.h`](../src/waterfront_upstream.h), and the docked large ferry. Ordinary traffic is kinematic; optional trial traffic wakes use a separate bounded pool. | The [location-life track](#location-life-track-proposed-2026-10-30) plans marina and route/timetable descriptors, keeping contacts, visible poses and wakes in agreement. Existing fixed functions are not a runtime actor editor. Do not allocate NPCs out of the four player slots or promise unlimited physical wake sources. |
| [Gameplay sunset](SUNSET_PLAN.md), [`waterfront_scene.c`](../src/waterfront_scene.c), [`renderer_gpu.c`](../src/renderer_gpu.c) | Daytime gameplay and a title-only dusk preview. The shared lighting frame and gameplay sunset remain proposed. | Expose fixed lighting presets/phase only after the sunset plan supplies and verifies the native shared frame/cache behavior. A Lua `sunset` name must not imply an implemented sun, window-light or day/night system. |
| [Deathmatch](DEATHMATCH_PLAN.md), [`boat_match.h`](../src/boat_match.h), [`boat_combat.h`](../src/boat_combat.h) | C match attribution, scores/rounds, water cannon, stern wave grenade, torpedoes, damage feedback and rearview presentation. Current protocol is v7. | Later author supported match parameters, objective/scoring variants and announcements. Projectile integration, hit/credit rules, pressure, capsizes and prediction remain C. New weapons need native work; balance and broader browser/platform qualification are unfinished. |
| [Multiplayer](MULTIPLAYER_PLAN.md), [protocol](MULTIPLAYER_PROTOCOL.md), [`boat_net_server.c`](../src/boat_net_server.c), [`boat_prediction.h`](../src/boat_prediction.h) | Four-player authority, ENet/native and optional browser transport, content checks, snapshots, input replay, wake reconciliation and remote views. The server currently initializes the compiled river course. | Authority can eventually own gameplay scripts, but script/config/state hashes and timed commands are not in today's wire contract. M5 impairment/resource/soak work and broader platform qualification remain. Scripted worlds must be explicitly rejected by existing clients until P5. |
| [Boating recordings](BOATING_PLAN.md#first-timed-course), [`boat_replay.h`](../src/boat_replay.h), [`boat_fleet.h`](../src/boat_fleet.h) | Basin runs record sampled poses and course splits for bests, ghosts and playback; fleet checkpoints support future-affecting C state and client input replay. | These are different systems. Neither is a general plugin-state/session recorder. Plugin provenance, commands, migrations and authored HUD playback need a deliberate new format/extension; keep existing recordings readable. |
| [Physics](BOAT_PHYSICS_PLAN.md), [wake rewrite](WAKE_REWRITE_PLAN.md), [arcade implementation](WAKE_ARCADE_IMPLEMENTATION.md), [`boat_wake_objects.h`](../src/boat_wake_objects.h) | Fixed-step physical handling and bounded arcade wake objects, jumps/landings/capsizes, native CPU/GPU water recipes. Physical-field research is retained as history and is disabled in the routine suite. | Expose supported presets and capped source recipes after validation. Do not put per-hull water sampling, wake math or a new physical-field solver in Lua. Preserve accepted feel; wake readability, tuning and platform/fleet costs retain their own gates. |
| [Foundations](BOATING_FOUNDATIONS_PLAN.md), [adventure ownership/tiling ledger](FREE_RIDING_ADVENTURE_FIRST_STEPS.md), [`boat_world.h`](../src/boat_world.h), [`boat_session.h`](../src/boat_session.h) | A 120 Hz integer clock, explicit physical-map resources, session/fleet ownership, view-owned caches and enforced core boundaries. A1/A2 provide important mapped-world work beyond the older foundation ledger. | Add a caller-owned plugin host around these owners. Never point a borrowed native definition at Lua memory. Full foundation workload/renderer qualification and larger-world measurements remain separate work. |
| [HUD](BOAT_HUD.md), [skins](BOAT_SKINS.md), [music](BOATING_PLAN.md#selectable-soundtracks--2026-10-24), [title](TITLE_SCREEN_PLAN.md) | Native marine instruments and shared bitmap UI, voyage/race/combat HUD, selectable XM tracks, Nordic skins and the explorer, title/settings/credits. Explorer and cabin selections also choose faster and slower handling classes, respectively, in solo free boating. | Start with existing voyage labels, then a small objective/notice/marker vocabulary rendered by C. Later select registered cues and appearance presets. Arbitrary meshes, shader code, audio decoders, title replacement, a palette editor and title music are not initial APIs. Keep skin presentation separate from explicitly authorized handling selection. |
| [Browser build](WEB_BUILD.md), [`cmake/Web.cmake`](../cmake/Web.cmake) | Emscripten/WebGL 2, packaged assets, browser persistence, shared title and an optional WebRTC client. Browser hosting is unavailable. | Compile the same Lua interpreter and descriptor checks; browser reload needs imported/staged bytes rather than desktop filesystem watching. Physical-device, browser, tab-suspension and networking qualification are still needed. A browser provider/API-key flow is a separate agent-platform decision. |

## Native integration and ownership

Keep the first addition small: a `proteus_plugins` static library with a private Lua dependency,
a host owner, a bounded source/manifest loader and typed descriptor decoders. Proposed file
names are `src/boat_plugin.[ch]`, `src/boat_plugin_lua.c` and
`src/boat_plugin_io.[ch]`; add state/command files only when P3 needs them. Those names describe
planned files, not existing interfaces.

`proteus_boat_core` and `proteus_boat_net` must continue to have no filesystem, socket, window,
input or audio access. They must not acquire a dependency on Lua or the plugin host. Extend
their existing plain C descriptor interfaces only as needed. The application/drive owner
loads bytes outside simulation, installs validated C values between ticks, and invokes the
existing C rules. A later room host follows the same pattern around the authority rather
than embedding a VM or file watcher in the protocol codec.

```text
editor / imported bundle / future agent
              | immutable candidate bytes; no live-world writes
       application plugin host ---- diagnostics / inspection / preview tools
              | stage, validate, choose commit tick
       owned C descriptors and later typed command/state transactions
              | boundary before the next fixed step
       BoatSession / BoatFleet + C voyage/race/match engines
              | committed snapshots and ordered events
       shared scene, native HUD/audio adapters, network/replay adapters
```

The first voyage needs a narrow ownership change. Today `BoatWorldDefinition.voyage` points
to process-lifetime data, `BoatVoyage` borrows that route, and free-boating water takes its
exposure from it. Add an explicit session override whose **native owner** stores the route
and copied names; do not mutate the global location record or replace the map owner merely
to change a destination. `BoatVoyage` must be rebound while its replacement owner is alive.
Keep the compiled route as the no-plugin reference through parity review. Replay/view
borrowers must not retain retired pointers. The plugin owner outlives all consumers, and
retired owners are released only after the tick/render boundary no longer borrows them.

The outer map's metre coordinates use simulation `x = -east`, `z = north`; heading is radians
with forward `(sin(heading), -cos(heading))`. Bindings should use named `x`/`z` fields and
units in schema/help, not expose ambiguous `Vector2.y` or renderer transforms. Use source
coordinates in the first example. A few explicitly authored landmark IDs can come later;
there is no current named-place catalog or semantic map index.

## Proposed package and API surface

This section records the proposal as planned. Since P0 (2026-10-28), the manifest and voyage
descriptor below are implemented, with the additions and exact rules in
[the plugin API](PLUGIN_API.md). Later snippets in this proposal retain **proposed syntax**;
the API and milestone results identify the currently implemented native loading and callbacks. P0 pinned Lua 5.4.9 after native and Emscripten feasibility
checks. Use the portable interpreter initially; no JIT or native
module ABI is needed. The [Lua manual](https://www.lua.org/manual/5.4/manual.html) is a
reference for the embedding interfaces, not a decision to pin that particular release.
Include the selected distribution's copyright/license in packages, following
[Lua's license](https://www.lua.org/license.html).

One directory, one inert bounded manifest, and one text entry point are enough initially:

```text
plugins/langedrag/
  plugin.json
  main.lua
  README.md
```

```json
{
  "id": "proteus.langedrag",
  "name": "Langedrag voyage",
  "api": 1,
  "kind": "voyage",
  "entry": "main.lua",
  "location": "outer",
  "capabilities": ["voyage.define"],
  "state_version": 1
}
```

Manifest capabilities request access; the host's policy grants it. A package cannot enlarge
its own limits or grant itself networking. API major, state schema and content revision are
separate: `api` selects the supported interface, `state_version` describes explicit future
script state, and the host computes a bundle hash over the normalized manifest and exact
source/assets. Include decoded gameplay-definition hashes for semantic comparisons. The
initial entry file list is fixed; later module/assets must be explicitly listed and bounded.
No dependency resolution or user-supplied bytecode is required.

The P1 `main.lua` can be just the existing data, with stable authored shelter IDs added for
diagnostics and later migration. (The implemented reference also carries the route's
`exposure.build` and `weather_range`, which this proposal omitted.) The adapter copies these values into a `BoatVoyageRoute`;
the IDs belong to its sidecar metadata, not a claim that the current C struct contains IDs.

```lua
return {
  voyage = {
    shelters = {
      { id = "dock", name = "DOCK", x = -12, z = 60, heading = 0, radius = 60 },
      { id = "bridge", name = "BRIDGE", x = 650, z = -850, heading = 1.33, radius = 150 },
      { id = "east_shore", name = "EAST SHORE", x = 3250, z = -2000,
        heading = 0.86, radius = 200 },
      { id = "langedrag", name = "LANGEDRAG", x = 3600, z = -3370,
        heading = 1.57, radius = 0 }
    },
    arrival = { x = 3595, z = -3370, radius = 75, max_speed = 6 },
    exposure = { x = 4300, z = -2650, radius = 700, height = 1.6 },
    exposed_fraction = 0.8
  }
}
```

Validate count, order, unique IDs, finite values, reasonable coordinate/radius/speed limits,
native capacity, string length/font support, and safe recovery placements against the
selected map and existing contact queries. The current last shelter is the destination;
the first is the initial recovery point. Do not silently clamp an invalid authored definition.
Return a field path such as `voyage.shelters[3].heading` plus the violated constraint.

After P3, the bounded vocabulary may grow as follows. Each row is a separate capability;
implement only the rows a milestone uses.

| Proposed interface | Contract |
| --- | --- |
| `voyage.define`, `race.define` | Typed data decoded once; existing C engines retain progress/rules |
| `observe` | Copied, read-only snapshots of approved player/objective fields and stable handles; no C pointers or whole-world arbitrary queries |
| `update(context, state)` | One deterministic transition with a batch of ordered events; returns replacement serializable state and typed commands |
| `migrate(context, old_version, state)` | Pure bounded conversion; return a validated new state or explicitly reject |
| `hud.set`, `notice.emit` | Named objective rows, counters, bearing/marker hints and finite-duration messages; C chooses typography/layout and reserves the native instruments |
| `weather.set`, `actor.route`, `audio.cue` | Later adapters for implemented native recipes only; validation, ownership and effective ticks required |

Avoid arbitrary `draw`, `raylib`, `spawn_anything`, `set_boat_memory`, filesystem, HTTP and
shell bindings. Use small descriptions, enums and stable IDs. Queries, timers, commands and
diagnostics must have documented units, capacities, ordering, failure codes and examples.
Generate human reference and a machine-readable capability/schema description from the
binding registry when that registry exists; do not maintain an LLM-only API in parallel.

## Hot reload is the normal development path

P1 must include the whole edit → stage → error/commit loop. Start with an explicit Reload
action; add native save detection with a small debounced poll outside the tick, driven by
injected time in tests. A watcher is convenience, not the correctness mechanism. Read a
consistent immutable bundle, coalesce rapid saves and retain the latest candidate. Avoid
combining a new entry file with an old manifest. A host/agent publish action may use a
temporary bundle plus atomic replacement; manual multi-file saves require a consistency
check before staging. Browser import produces the same immutable bytes.

1. **Acquire:** normalize paths, reject traversal/symlink escapes, enforce byte/file limits,
   and calculate the candidate revision. Retain the active revision independently of disk.
2. **Stage:** create a separate Lua state, compile text, evaluate the definition with no
   live-world mutators, copy it into native candidate storage, validate it and reserve every
   required native resource. The data-only VM can then be destroyed.
3. **Classify:** compare old/new semantic definitions, ownership and progress compatibility.
   Show what will be preserved and what requires an objective/session restart. A syntax,
   resource or validation failure leaves the active revision untouched.
4. **Commit:** at a declared fixed-tick boundary, verify that the base revision is still
   active, then atomically replace the native descriptor/bindings and any approved progress
   changes. No allocation or fallible asset creation belongs in the commit section.
5. **Retire:** release old native storage after its borrowers have stopped using it. Report
   the installed revision, effective tick and policy. Keep bounded prior source revisions for
   Undo; failed candidates remain inspectable without becoming active.

P1 needs explicit, small compatibility rules, not general state migration:

| Edit | Initial live policy |
| --- | --- |
| Display names only; identical ordered IDs and gameplay fields | Preserve boat, clock, water/wakes and all `BoatVoyage` progress; rebind names safely |
| Destination/shelter geometry, order, membership, arrival speed/radius or exposure-status threshold | Require the selected **restart voyage** policy: retain the boat/world when physical water is unchanged, reset native voyage progress/timing/recoveries to the new definition, and identify its validated initial recovery shelter |
| Physical exposure/water recipe | Stage successfully, then apply through an explicit **restart session** policy that resets the dependent native world/wake state together; preserving old physical history is not promised in P1 |
| Different location/map, unsupported capabilities, or a required restart while policy is preserve-only | Keep the current revision; show the required action or unsupported field |

Changing route geometry cannot universally preserve what “already reached” means. Do not
infer progress from an old array index, count a crossing retroactively, or claim that stable
IDs alone prove semantic compatibility. After a voyage restart, observations begin at the
new commit tick; normal C arrival rules may complete the new objective on a subsequent tick
if the boat is already inside its area at controlled speed. For a future race reload, hold
the candidate until the next round/start or explicitly restart the run; never move a gate
under a racing boat and reinterpret the preceding segment as a new crossing.

An author's selected automatic-reload policy may apply compatible changes immediately.
An incompatible save should not silently reset the session. Restart choices are concrete
gameplay actions in the authoring UI, not a general approval workflow for every edit.
P1's Undo reinstalls an older descriptor through these same rules; it does not rewind the
boat or erase elapsed time. Full state/checkpoint rollback belongs to later milestones.

## Stateful gameplay after the descriptor slices

P3 introduces a small transition model, not an arbitrary mutable VM attached to every actor.
Use one active gameplay plugin initially. A typical callback could count visits reported by
the native engine and update a native-rendered objective row:

```lua
-- Proposed P3 API; not part of the descriptor-only P1 loader.
return {
  init = function(context)
    return { visits = 0 }
  end,
  update = function(context, state)
    for _, event in ipairs(context.events) do
      if event.kind == "voyage.shelter_reached" then
        state.visits = state.visits + 1
      end
    end
    return state, {
      { kind = "hud.set", id = "visits", label = "STOPS", value = state.visits }
    }
  end
}
```

The committed state is host-owned serialized data: booleans, bounded strings, finite numeric
values, dense arrays and string-keyed records with canonical ordering. Reject cycles, shared
reference graphs, functions, threads, userdata, metatables, nonfinite numbers and excessive
depth/size. Define numeric representation and rounding at the binding boundary. IDs, event
sequence numbers, deadlines and RNG state use an explicitly supported integer representation;
do not lose wide ticks through a JavaScript/JSON number conversion. Native rule state remains
native and is checkpointed separately; scripts cannot edit a `BoatVoyage`/`BoatRace` struct.

**VM memory is not the save format.** For the initial stateful implementation, use a fresh
bounded execution instance per callback batch, loading the accepted immutable program and
decoding a copy of the committed explicit state. Top-level code may create helper closures,
but its globals, upvalues and module tables disappear after the call. Only the validated
returned data persists. This makes a failed callback's hidden mutations discardable without
pretending that a protected Lua call rolls back tables. Measure this simple approach at a
modest callback rate before considering pooled VMs; a persistent-VM optimization must prove
equivalent reset/isolation rather than rely on an author convention about upvalues.

The transition is atomic across **script state, RNG/timers, event acknowledgment and native
command intents**. Decode all output into native staging, validate every field, check owner
rights/conflicts/capacity, reserve resources, then commit the whole batch at its effective
tick. If any part fails, publish none of it and discard the execution instance. A command
that may partially fail while applying is not a supported transactional command until its
adapter has a staging/commit contract. External effects such as playing a sound are queued
after commit with unique effect IDs; an audio failure reports an outcome, not a rollback of
already simulated boats. Repeated delivery must not play the same cue twice.

On a runtime error or exhausted budget, quarantine that plugin's callbacks and show a bounded
diagnostic. Retain its last committed definition/state and let the existing C boat/rule
engine continue; do not restart the process, apply partial commands or automatically remove
physical obstacles. Mark script objectives as suspended. A future mode that cannot continue
without script decisions must define its native pause/end-round policy before shipping.
Keep the failing event batch for inspection. Bounded event backlog overflow requires an
explicit resync/restart policy rather than silent loss of objective events or endless retry.

Reload migration runs against a copy of the last committed state in the candidate execution
instance. Validate the new state, native progress mapping and resource diff before swapping
anything. Equal state schema versions permit copying only when the definition's compatibility
rules also permit it; changed meaning may require migration or reset even with equal IDs.
Candidate failure keeps the active program and native resources. Preserve a bounded previous
revision and pre-commit checkpoint for an immediate development rollback. After ticks have
advanced, “Undo code” reapplies old code to current state through migration/reset; “rewind
preview” restores a complete compatible checkpoint and replays recorded inputs/commands.
Do not restore old objective state beside a boat/world that has continued moving.

### Virtual time, events and commands

The authoritative clock remains [`BOAT_TICK_HZ = 120`](../src/boat_clock.h). Start script
updates at 10 Hz, every 12 simulation ticks, with an ordered batch of native events since
the last dispatch. No render-frame callbacks are necessary. At boundary T, build the input
snapshot from committed native state through T, run due transitions, and install accepted
commands for T+1 before stepping C physics. Native outcomes from that step become input to
a later script dispatch. This explicit one-way staging prevents callback recursion inside
collision/race processing; any future low-latency capability must specify its own phase.

Timers use due ticks, never wall time. A paused solo session does not advance them; room
time follows its authority. Order events by tick, native event sequence and stable entity
identity/life; order simultaneous plugin work by a recorded host order. Bound every queue
and define overflow as a visible failure. P3.2 adds a bounded `BoatVoyageEvents` output list
alongside the latest-event presentation field. The adapter consumes the complete output of
each native observation/recovery; the presentation field does not retain every transition
within one tick. Existing race/match output lists are useful models.

Supply a host-specified deterministic RNG stream per plugin, with seed/counter in the
transaction/checkpoint. Failed transitions must not consume its committed sequence. Disable
uncontrolled randomness, wall clocks, unordered iteration as an authority ordering source,
and access to render interpolation. Pin the interpreter configuration and API semantics;
make same-input tests compare canonical state/command hashes. Do not claim cross-platform
bit equality for arbitrary Lua floating-point/transcendental expressions. Keep authoritative
physics in C and replicate its accepted effects; any script prediction path needs separate
native/web equivalence evidence.

### Ownership and composition

Each created objective, timer, HUD item and future actor carries `(plugin_id, local_id)` and a
host generation. Use native validated handles, never addresses. Compatible reload can retain
logical resources by ID; removed resources are retired by the host even if plugin cleanup
throws or never runs. Stale generations fail visibly. C owns destruction and the safe removal
policy for physical resources; unloading is not a way to bypass contacts or collision safety.

Start with one gameplay owner of a session's voyage/course/rules. Reject a second claimant.
Later allow bounded additive HUD/cosmetic contributors in assigned slots. Global weather,
lighting and music require an explicit host-selected owner or documented composition rule;
load order must not decide who wins. Validate a resource/command diff for the whole plugin
transaction. No plugin may delete another owner's resources, claim player slots for traffic,
or silently override input/help/network status. Resource limits apply to the total host as
well as each plugin, including active, staged and retained rollback revisions.

## Restrictions, budgets and responsiveness

Construct an allowlisted environment rather than opening all standard libraries. No `io`,
`os`, `debug`, package/native loading, process execution, filesystem-based `require`, network,
dynamic `load`, user bytecode or unrestricted coroutine scheduler. A later module loader may
read only manifested text already acquired by the host. Keep traceback generation on the
host side and sanitize/truncate diagnostics. Protect native allocations and cleanup across
Lua errors; do not let interpreter error unwinding skip ownership cleanup.

Initial **proposed limits to validate in P0/P3**, not measured guarantees: one gameplay
plugin, 4 KiB manifest, 64 KiB total Lua text, 2 MiB per execution instance, 16 KiB canonical
state, depth 16, 32 output commands, 64 delivered events per callback and 8 HUD items with
short bounded text. Reuse stricter native capacities, such as four voyage shelters and eight
river gates. Set a host total ceiling that includes staging and old revisions. Choose and
record separate instruction/work budgets for module initialization, update and migration;
do not let a plugin increase them in its manifest.

Lua supplies allocator and instruction-hook interfaces, but instruction-count hooks cover
Lua execution, not arbitrary work in C calls. Protected calls report errors; they are not a
world transaction. See the primary references for
[allocation](https://www.lua.org/manual/5.4/manual.html#lua_newstate),
[hooks](https://www.lua.org/manual/5.4/manual.html#lua_sethook) and
[protected calls](https://www.lua.org/manual/5.4/manual.html#lua_pcall).
The host therefore must also bound compiler input/complexity, native API query work, decoder
depth, string/library operations, asset bytes, command count, resource allocation and total
callback work. Omit expensive library operations until their worst case has a bound.
An instruction hook alone does not make parsing, migration or a C binding preemptible.

P0/P1 may stage small trusted local edits in a visible authoring pause outside simulation;
measure compilation/validation cost and report that limit. Do not advertise uninterrupted
motion or hostile-code containment on this basis. Before opening arbitrary imported bundles
or background live authoring, establish cancellable/isolated staging or proven parser/work
bounds; native workers must never touch live game/graphics state, and a web Worker or a
bounded pause needs its own feasibility check. Runtime watchdogs are failure deadlines,
not gameplay clocks. Automated tests inject time and workload, with no sleeping. An in-process
allowlist protects engine capabilities but is not an OS security boundary against an
interpreter/native memory bug.

## Multiplayer, prediction and recording

The proposed [room-content loading and distribution plan](ROOM_CONTENT_PLAN.md) owns the
broader path from preinstalled packages to downloaded maps/scenarios. P5 supplies the shared
authority, prediction and revision contract; its initial matching-preinstalled-package scope
remains unchanged. Automatic distribution and downloaded-script trust are later gates.

P0–P4 are solo/local authoring. Existing rooms continue using their compiled content. Reject
plugin gameplay in connected/hosted modes until the protocol integration is implemented;
having Lua on both machines does not establish compatibility.

P5 must add a bounded content contract: API and interpreter/build identity, bundle and decoded
definition hashes, active revision/generation, effective tick, required native capabilities,
and serialized state/command checkpoint versions. Define exact encoding and size/page limits
in the [protocol owner](MULTIPLAYER_PROTOCOL.md), advance compatibility deliberately, and
test mismatched/missing content before any world is installed. A numeric API version alone
does not identify changed course geometry or gameplay source. Initially require installed,
trusted matching packages; receiving a room name must not execute downloaded code.

The authority runs gameplay callbacks and decides scores, objectives, spawns and weather.
Clients may render approved HUD/presentation data. Replicate accepted, tick-stamped native
configuration/commands and objective state rather than making clients trust local scripts
for authority decisions. Pure score/HUD updates need no speculative Lua. A command that
changes collision, water or held/released boats also affects prediction: schedule/distribute
it with sufficient lead time, store it with checkpoints and reapply it during input replay,
or hold prediction until it is known. Arbitrary surprise actor/weather changes are not safe
to add to today's predictor. Reconcile life/generation fences and duplicate/out-of-order
delivery along with the existing wake rules.

Begin room reload at a lobby/round boundary. Authority stages, verifies required client
readiness/content, announces a commit tick and activates one revision for everyone. Timeout
or a client mismatch aborts or follows an explicit resync/disconnect policy. There is no
unilateral mid-race client reload. Seamless mid-round editing is a later experiment that
needs atomic content/state bootstrap, prediction-history repair and late-join tests.

Distinguish a future **session recording** from today's pose-based basin `.run` files and
from short-lived client input replay. Record source/content provenance, definition/initial
state, native checkpoints, seed/RNG state, input and accepted-command chronology, plus reload
and migration boundaries. Viewing a recording should use recorded outcomes without rerunning
untrusted source by default; diagnostic re-simulation can explicitly use the exact pinned
bundle. Checkpoint/seek must reconstruct objective HUD and revision as well as boats. Give
plugin courses separate best/ghost compatibility keys and preserve legacy files unchanged.

## Milestones and acceptance

Each milestone is independently reviewable in a separate worktree/PR. Complete the requested
step and its documentation; the roadmap is not authorization to implement every later track.
Use existing C fixtures as the parity oracle. Keep native gameplay unchanged with plugins
disabled and retain a straightforward no-plugin launch until
[the plugins-by-default cutover](PLUGIN_DEFAULT_PLAN.md#pd5--cutover) retires this rule. Every implementation handoff runs
the Debug build and full CTest preset, plus the focused checks below; report unavailable GPU,
browser, physical input or human-play evidence explicitly.

P0 → P1 → P2 establishes the two descriptor slices; P3 adds behavior and P4 makes the
authoring tools practical. **P5 and P6 can branch independently from P4:** a solo in-game
agent does not need to wait for multiplayer, prediction or session-recording qualification.
P6 uses the already tested local staging/preview/undo path. Only shared-room agent editing
depends on P5's authority and content contract. The [location-life track](#location-life-track-proposed-2026-10-30)
can also proceed from P4 for native solo use: an in-game agent is not a prerequisite.
Scene changes that affect contacts, water or prediction require P5 before room use.

### P0 — bounded descriptor loader and contract (implemented 2026-10-28)

**Deliverable:** a tiny headless loader can evaluate the proposed voyage example, decode it
to owned native data and reject bad packages. No game-loop integration or behavioral VM yet.

1. Select/pin the Lua release, source checksum, compile options and license packaging. Add a
   private static target in [CMake](../CMakeLists.txt); keep Lua out of the core/net dependency
   graph. Do a small Emscripten compile/load preflight using [Web.cmake](../cmake/Web.cmake),
   not a browser qualification project.
2. Add `boat_plugin` ownership/status/diagnostics, the restricted Lua loader, bounded manifest
   parsing and the `BoatVoyageRoute` decoder. Input is immutable bytes; filesystem acquisition
   belongs to `boat_plugin_io`. One file and one capability suffice. Decide exact version,
   invalid-field, allocation and text-only loading behavior in a short API reference.
3. Add a proposed `tools/validate_boat_plugin.c` and minimal fixtures. Exercise valid data,
   syntax errors, forbidden facilities, bad coordinates/counts/IDs, malformed/deep data,
   memory/instruction exhaustion and a nonterminating top-level script. Test cleanup and
   repeated failed loads; add the library to relevant boundary checks without weakening them.
4. Compare the decoded example field by field with `boat_voyage_outer`, including float
   conversion and copied name lifetime. Print a deterministic definition digest and structured
   errors. Profile compiler/native decoder work separately from Lua instruction work.

**Acceptance:** unchanged existing build/tests; validated reference definition equals the C
baseline; failures leave a supplied last-good descriptor intact, leak no resources and do
not call I/O from core. Test failure deadlines must return on the expected event, never wait
for a chosen duration. P0's web result is compile/load feasibility only. Update this plan,
the index and the new API guide; no visual acceptance is needed for this headless step.

#### P0 result, 2026-10-28

Implemented on branch `plugin-p0`:

- **Runtime:** Lua 5.4.9, fetched by `FetchContent` with lua.org's published SHA-256
  (`2335b6c5…6fb8e6`). It builds as the private static `proteus_lua` from the VM plus the
  base, string, table and math libraries only. `io`, `os`, `debug`, `package`, `coroutine` and
  `utf8` are not compiled, and a force-included header fixes the string-hash seed.
  `proteus_boat_core` and `proteus_boat_net` do not link it, and configure fails if they do.
  The boundary self-test also rejects a core `lua.h` or `boat_plugin.h` include.
- **Host:** `src/boat_plugin.[ch]` holds the manifest, voyage validation, owned
  `BoatPluginVoyage`, copy/rebind and FNV-1a digest. `src/boat_plugin_lua.[ch]` is the
  restricted state: counting allocator, 100-instruction count hook, text-only load, raw-access
  decoder and a protected setup, so allocation failures never panic. `src/boat_plugin_io.[ch]`
  does the only file reads, with `O_NOFOLLOW`, regular files only and reads bounded by the
  limit. The manifest uses the vendored jsmn through helpers moved from the server directory
  into `src/boat_json.[ch]` (`proteus_json`); `boat_directory` still passes. Contract, schema,
  allowlist, limits and diagnostics: [plugin API](PLUGIN_API.md).
- **Reference package:** [`plugins/langedrag`](../plugins/langedrag/), the proposed example plus
  the two fields the C route has, `exposure.build = 60` and `weather_range = 2500`. It is not
  yet packaged into native or web assets (P1).
- **Decisions made at this gate:**
  - Lua 5.4 rather than the newer 5.5 line.
  - Manifest values are fixed by host policy, and packages cannot change limits.
  - Diagnostics carry the file, Lua's own line for syntax, runtime and budget errors, and the
    field path for schema and manifest errors.
  - Validation stops at the first error; there is no clamping.
  - Out-parameters are written only on success.
  - `pcall`, metatables, string patterns and `table.move` are omitted, because the hook cannot
    bound their C work or catching would defeat the budget.
  - The last shelter must have radius 0 and lie inside the arrival area, and no shelter may lie
    in exposed water.
  - Map-water checks live in the validator for now.

Verification, 2026-10-28, M1 Max/macOS 26.6.2, Debug (`PROTEUS_GPU_TESTS=OFF`):

- `cmake --build build -j` built with no project warnings, only the existing bundled
  `stb_vorbis` warning. `ctest --preset full` passed **248/248 active tests** in 18.4 s, with
  13 retired wake-field tests disabled. That includes the new twelve `boat_plugin.*` cases
  (quick lane, about 0.5 s together), `boat_directory`, `test_time` and both boundary checks.
- `validate_boat_plugin --case reference_matches_compiled` prints
  `digest=e8509272172cb7c4 compiled=e8509272172cb7c4 instructions<100 peak_bytes=17409
  fields=35`. All shelter, arrival, exposure, fraction and range floats compare bit-equal, and
  names survive freeing the source bytes and scribbling over the original after a copy.
- Rejections exercised:
  - syntax errors with lines, bytecode, compiler nesting overflow and runtime errors;
  - 26 forbidden facilities;
  - 27 invalid-field variants, each with its path;
  - shelter counts of 1 and 5, gaps and deep data;
  - 13 bad manifests and both size limits;
  - memory exhaustion, including too little heap to start;
  - three nonterminating loops stopped by the budget at its exact step, and deep recursion;
  - package files: missing, escaping, symbolic link, directory, one byte over the limit, and
    exactly at the limit.

  Every failure left a preloaded definition byte-identical and reported zero leaked bytes;
  48 repeated failed/valid loads gave identical diagnostics.
- A separate ASan/UBSan build (`-fsanitize=address,undefined`) ran all cases with no reports.
  macOS ASan has no leak detector; leaks are covered by the allocator's live-byte count.
- **Web preflight:** with Emscripten 6.0.12 and Node 24.2.0, `validate_boat_plugin.js`
  (`-sENVIRONMENT=node -sNODERAWFS=1`) passed every case, file cases included, with the same
  digest (peak 13 473 bytes with 32-bit pointers). This shows compile/load feasibility only;
  the browser game does not link the plugin host, and no browser run was made.
- **Cost:** the reference load evaluates in under 100 Lua instructions with a 17 KiB peak heap
  and 35 decoded fields; a whole load takes about 0.2 ms in Debug (case timer, reported only). A
  load that exhausts the 1 M-instruction budget takes about 10 ms in Debug. Compile/decode work
  is bounded by input size and heap rather than separately timed.

Limits: there is no game-loop integration, reload, session override, packaging or user-visible
authoring, so P1 owns all of that. The Windows (MSVC) and Linux (GCC) builds are left to CI.
The file-case tests are POSIX-only, so the Windows reparse-point path is untested locally.
Instruction budgets do not bound C library work; the omitted functions are the mitigation.
This is not hostile-code containment. No visual, audio or manual check applies to this
headless step.

After merge (PR #48), the user play-tested the native game on 2026-10-28 and confirmed the
voyage still works. P0 does not change gameplay, so this was a no-regression check, not
acceptance of plugin authoring.

### P1 — Långedrag live authoring (implemented 2026-10-28)

**Deliverable:** run solo `outer` with the Lua-authored current voyage; edit, reload and undo
without rebuilding C. Preserve existing C arrival/recovery/exposure behavior.

1. Add the explicit route override/lifetime seam in [`boat_session.[ch]`](../src/boat_session.h)
   and the application water setup in [`wake_velocity.c`](../tools/wake_velocity.c). Keep
   `boat_location`'s compiled default untouched. Use the drive's shared tick boundary
   ([`wake_velocity_drive.h`](../tools/wake_velocity_drive.h)) so screenshots and the windowed
   application activate definitions in the same order. Preserve source/wake ordering.
2. Add the shipped example directory and opt-in launch/reload selection. Flag/key names are
   implementation choices, not commands available today. Start with Reload, then add the
   debounced native save poll. Show active/candidate revision, success/error and reset policy
   in a small authoring panel. Syntax errors include file/line; schema errors always include
   field path, with source line only when actually available.
3. Implement native candidate staging, compatible pointer rebinding, restart-voyage versus
   restart-session policy and safe retirement. Invalid file saves do not remove the voyage.
   Copy descriptor strings before destroying Lua. Undo follows the same transaction path.
4. Extend [`validate_boat_voyage.c`](../tools/validate_boat_voyage.c) or its fixtures to drive
   the compiled and Lua definitions with identical input/virtual time, comparing progress,
   recovery, arrival and physical traces. Exercise reload while inside a shelter/arrival
   radius, while exposed and after arrival; changed ordered IDs must never retain an invalid
   shelter index. Check failed staging, reset, repeated reload and shutdown lifetimes.
5. Capture and inspect equivalent CPU `outer` frames at useful voyage states, and the error/
   reload UI. Add GPU captures when presentation changes and a windowed edit/reload/recovery
   demo for motion/input/lifetime behavior. Record a real author edit and distinguish scripted
   navigation from human play. Reuse the existing route autopilot; do not wait minutes in tests.

**Acceptance:** the reference plugin reproduces the current outing; editing a display name
preserves boat/progress, a gameplay edit follows its chosen reset policy, a broken file leaves
the running revision usable, and unloading/reloading has no dangling names or route pointers.
The example ships in native/web asset packaging as appropriate, with plugin mode rejected in
rooms and legacy recording modes until their compatibility work exists. Update the adventure
owner's description of where route data lives, launch/control guides and the index. First
human authoring acceptance is a separate recorded result, not inferred from screenshots.

#### P1 result, 2026-10-28

Implemented on branch `plugin-p1`, cut from `main` at `3de5035` (P0 merged). The contract,
keys and policy table are in [the plugin API](PLUGIN_API.md#playing-and-live-authoring-p1).

- **Session seam:** `BoatSession.voyage_route` is the route that init and restarts begin. It
  defaults to the definition's compiled `boat_voyage_outer`; `boat_location` is untouched.
  `boat_session_rebind_voyage` swaps the borrowed route and keeps progress.
  `boat_session_restart_voyage` begins a route at the current time. It uses the new
  `boat_voyage_restart`, which keeps the weather already met (`observed`, `weather`,
  `weather_started`), so the physical water does not change under a voyage restart.
- **Host:** `src/boat_plugin_host.[ch]` (in `proteus_plugins`, no I/O or clock) owns revisions in
  fixed slots: the active one, eight for Undo and one held candidate. Staging is load, then the
  map check, then `boat_plugin_classify`. The map check (`boat_plugin_check_location`, moved from
  the P0 validator) takes the session's location and compiled water. A commit is refused if its
  base revision is no longer active. Strings are copied into native storage before the Lua
  state closes (P0), and a slot is reused only after the session was rebound away from it.
- **Policies:** comments/layout only is unchanged; names only applies at once. Ordered IDs,
  shelter geometry, arrival or `exposed_fraction` restart the voyage on F7. `exposure` or
  `weather_range` restart the session on F7, through the game's existing full-restart path. A
  different id/kind/location/API is refused. Every restart begins at shelter 0, so no old index
  survives a changed route.
- **Game:** `--plugin DIR` (native only), implemented in `tools/wake_velocity_plugin.[ch]`. It
  selects the manifest's location and is refused with rooms, `--course`, `--replay` and
  `--ghost`. Saves are polled every 0.25 s and staged once they have been unchanged for 0.3 s.
  The keys are F5 reload, F6 undo/discard and F7 apply. A bottom-left panel shows the revision
  and the outcome, which is also printed to stderr. Definitions change in the frame loop before
  that frame's ticks, never inside `timed_boat_step`, so source and wake order are unchanged. At
  launch the plugin route is installed at tick 0, before the screenshot drive or the first
  windowed tick. Both paths therefore activate it in the same order without moving it into
  `wake_velocity_drive`. The browser game does not link the plugin host.
- **Packaging:** configure extracts Lua's notice from `lua.h` to `build/lua-LICENSE.txt`. The
  macOS app and Windows zip ship it beside the other notices, with the reference package at
  `assets/plugins/langedrag`. The Windows workflow renders a frame from the packaged plugin in
  its relocated-executable check. `CMP0156` is set so that Apple's linker does not see
  `proteus_json` twice.
- **Decisions made at this gate:**
  - F5/F6/F7 rather than one key, so that a restart is never one keypress away from a reload.
  - An incompatible save is held, not applied, even with polling.
  - Undo holds revisions, not source text; there is no Redo.
  - The browser has no plugin mode until P4's import.
  - The first shelter must still be the location's spawn.

Verification, 2026-10-28, M1 Max/macOS 26.6.2, Debug (`PROTEUS_GPU_TESTS=OFF`):

- `cmake --build build -j` built with no project warnings. `ctest --preset full` passed
  **264/264 active tests** in 13.6 s, with 13 retired wake-field tests disabled. That includes
  the eleven new `boat_plugin_session.*` cases, the five new `wake_velocity_plugin_*` launch
  tests, `boat_voyage`, `test_time` and both boundary checks.
- `validate_boat_plugin_session` (`tools/validate_boat_plugin_session.c`):
  - `compiled_parity` drives a compiled and a plugin session with identical inputs from the
    fjord turn through the exposed flank to Långedrag, with a forced recovery. Over 15 505 ticks
    the boat, storm and every voyage field (arrival 129.21 s, 3 298 exposed ticks, recovery,
    events) are equal at every tick. It is labelled slow at about 0.19 s.
  - The other ten cases are quick and take about 37 ms together:
    - classification of sixteen edits and an identical copy;
    - the location check, with field paths for a moved start, a shelter ashore and an arrival
      against the harbour edge;
    - a rename keeping progress and weather;
    - voyage restarts inside a shelter, while exposed, after arrival, and when the route loses
      a shelter the boat had reached;
    - a session restart;
    - five broken stages that leave the active revision byte-identical and leak nothing;
    - Undo/Discard and 24 revisions over the eight-deep history, never writing the borrowed slot;
    - the save poller on a virtual clock;
    - 96 reload cycles;
    - `game_controls`, which runs the game's F5/F6/F7/polling glue on files in a build-tree
      scratch directory.
- Frames: CPU and GPU `--screenshot --time 3` at `outer` with `--plugin plugins/langedrag`
  are byte-identical to `--location outer` (CPU also from the packaged app). An edited exposure
  (moved, 3 m, `weather_range = 0`) renders rain, a dark sky and rough water at the same
  `--start` where the compiled route is calm, so authored data reaches the renderer.
- **Windowed agent run:** a 1 500-frame GPU window on a scratch copy of the package, edited by a
  script while it ran:
  - a rename applied at once (R2, progress kept);
  - `radius = 0` was refused with `voyage.arrival.radius` and R2 kept;
  - a fixed save with a new arrival radius was held for F7;
  - a truncated file was refused with `main.lua:2` and R2 kept;
  - restoring the reference applied as a rename.

  In-game captures (a temporary capture hook, not committed) showed the panel's two lines
  legible at 960x600 for each state. No keys were pressed, so the F5–F7 paths were exercised
  only by `game_controls`, not in a window. The boat was not driven.
- Packaging: `tools/macos/package.py --version p1-check` built a signed scratch app containing
  `lua-LICENSE.txt` and `assets/plugins/langedrag`. Its executable loaded the bundled plugin from
  another directory. A MinSizeRel Emscripten 6.0.12 build of `wake_velocity` succeeded, with no
  plugin host linked.

User check, 2026-10-28: after this run the user tried the P1 branch natively and reported
"Seems to work well!" They drove out to Långedrag, entered the squall for a while, and found the
voyage hints looking as they used to. This is human play of the route (arrival, exposure and
HUD unchanged). The user then renamed a shelter in `main.lua` while playing, and the save
reloaded automatically and applied the new name. This is the first human live edit (the
rename policy, through save polling). F5–F7, a held route or water restart, Undo and a
broken save have not been tried by a human yet.

Limits: rooms, the course/replays, recordings and the browser keep
the compiled voyage. Windows and Linux are left to CI, including the packaged-plugin frame on
Windows. Polling reads both files (at most 68 KiB) every 0.25 s; that cost was not measured
separately. Undo does not restore the files on disk, and F5 after Undo stages the files
again.

### P2 — river course authoring through the same path (implemented 2026-10-29)

**Deliverable:** a second small example defines the existing `BoatRaceCourse` and
`BoatRaceRules`, demonstrating that this API supports more than one voyage.

Add bounded race decoding with the existing eight-gate/four-slot capacities and native
validation. Extend [`boat_race_trial.c`](../tools/boat_race_trial.c) and
[`validate_boat_race.c`](../tools/validate_boat_race.c) to use an owned candidate definition;
keep the C race engine and compiled fixture as reference. A local trial or headless fleet
is sufficient: production network races remain compiled until P5. Expose a preview of gates,
the ferry and the existing storm, with source clearance and reasonable world-envelope checks.
Validate whole routes, not merely each finite coordinate.

Course changes commit at a new run/round with progress reset. A label-only revision can retain
progress if the gameplay digest is unchanged. Compare scripted four-boat results, crossings,
recovery, DNF/deadlines and native hashes with the baseline; inspect CPU/GPU gate/ferry/storm
frames and run a windowed trial. Verify rejects for impossible counts, degenerate gates,
invalid source geometry and unsafe recovery arrangements. Update [the race plan](RACE_PLAN.md)
and authoring examples. Do not generalize the basin trial/replay engine merely to reuse an
API name; that is a later separate adapter if wanted.

#### P2 result, 2026-10-29

Implemented on `plugin-p2`, based on merged P1 PR #49 (`be15608`):

- `plugins/river-sprint` describes the current seven gates, four grid positions, ferry,
  travelling storm and all `BoatRaceRules`. The bounded `race.define` decoder uses the same
  Lua setup, protected evaluation, budgets and diagnostics as P0/P1. Definitions are native,
  pointer-free and copied only on successful decoding.
- The existing revision host selects voyage or race at startup. Race label changes preserve
  the run; every gameplay change is held for an explicit new run. F6 Undo uses the same policy;
  invalid candidates leave the active descriptor intact. An Undo now clears obsolete errors.
- Staging checks the river map, gate openings, ordered approach lanes, grid spacing/hull
  clearance, ferry footprint along its complete crossing, and recovery/parking candidates.
  `boat_race_recovery_candidate` shares the actual native placement recipe with validation;
  placement order and live recovery behavior are unchanged.
- `boat_race_trial --plugin plugins/river-sprint [--play]` previews authored gates, ferry and
  storm, reloads saves/F5, undoes/discards on F6 and applies gameplay on F7 with a full preview
  reset. The preview uses the first authored grid position, facing the start direction,
  so moving that slot offsets the boat independently of the start-line buoys (corrected
  after the user's overlapping-buoy report). Shared application glue accepts injected time/input. Staging discards simulation
  delta across its frame boundary. A frame-borrowed `BoatViewState.race.course` override feeds
  both renderers; NULL keeps the compiled course. Production rooms, browser, basin runs and
  replay formats are unchanged.
- `boat_race_plugin.*` runs the existing native race cases using the Lua fixture.
  `boat_plugin_race.*` adds decoding/rejection, revision/lifetime, real-file trial controls,
  and physical parity checks. Windows CI builds and runs the new targets.

Verification, 2026-10-29, Apple Silicon/macOS, Debug with GPU tests enabled:

- Debug build is warning-clean. Before the final panel/control refinements, the full suite
  passed **308/308 enabled tests**, including
  all 26 enabled GPU checks; 15 retired wake-field experiments remained disabled. The first sandboxed
  attempt could not open sockets/graphics; the first desktop-enabled attempt had transient
  monitor-initialization failures. A complete desktop-enabled retry passed.
  After the final panel/control refinements, the Debug build and full CPU preset passed
  **282/282 enabled tests** in 13.84 s (`PROTEUS_GPU_TESTS=OFF`), with 13 retired field
  cases disabled. Final GPU recapture could not initialize a monitor; the earlier complete
  GPU run is scoped to its pre-refinement revision rather than inferred for the final panel.
- All 16 focused plugin/race case tests passed under ASan/UBSan. No sanitizer reports occurred;
  macOS ASan has no leak detector, so Lua allocator live-byte checks remain the leak evidence.
- The reference gameplay digest equals the compiled course/rules: `66dd02070a95d104`.
  Four real boats finished after 9276 ticks, with identical fleet hashes, phase/deadline state,
  entrant progress and events at every tick; final hash `6426c932d3225d32`.
  The reused native suite covers crossings, ties, recovery, parking, leavers/DNF, grid holds,
  deadlines, checkpoint restoration and a second round.
- CPU reference and compiled-course frames at 18 s are byte-identical. Inspected CPU/GPU
  frames show the ferry and authored gate-width edit; an edited stationary storm is also
  captured on both renderers. A scripted 480-frame GPU window applied a manifest rename,
  retained R2 after a bad `race.source.speed`, then held a gate-width edit as R3 for F7.
  Temporary in-game capture instrumentation showed the error and held-restart panels;
  it was removed. That review prompted a darker panel background and omission of unknown
  source-line zero. Final panel recapture was blocked by macOS monitor initialization.
- Spawn correction after the user's buoy-overlap report: the preview now starts at `grid[1]`
  rather than the start-line centre. A warning-clean rebuild and all 282 enabled CPU tests
  passed again. Inspected CPU frames at time zero show the default grid spawn clear of the
  buoy row, and a validated edit to `x = -206.3, z = -256.0` moves it about 5 m back.
  A later 120-frame windowed `--play` startup/exit check passed; no human input is claimed.

User check, 2026-10-29: after the spawn correction, the user tried several spawn positions.
Valid edits updated the preview; other edits reported errors. They then drove the course
successfully and reported "Works well!" This verifies human use of spawn authoring,
validation feedback and driving the local course. The user did not specify which reload
keys or save-polling paths they used, so individual key paths and Undo remain covered only
by automated checks.

Limits: this is a local course/source preview, not a new production race mode. The preview
has no countdown/standings/results HUD; authored rules are exercised headlessly through the
native engine. Human feedback covers spawn edits, validation errors and successful local
course driving. Exhaustive control coverage, Windows/Linux execution and browser P2
qualification are not claimed. F5/F6/F7 and save polling are exercised through the same trial
adapter on real files with virtual time. Conservative sampled map clearance does not prove
all driving lines or guarantee an immediately free recovery among moving boats. The new
example lives in the source tree; standard native game packages continue shipping the P1
voyage example. This completes P2; subsequent P3 work is recorded below.

### P3 — small stateful objectives, transactions and inspection

**Deliverable:** add one optional objective to a supported voyage, such as visiting two
shelters before arrival, with explicit serialized Lua state and a native HUD row. Keep the
underlying voyage completeable independently while testing the extension.

Implement the fresh-instance transition, canonical state codec, virtual-time/RNG/event bridge,
native command staging, ownership and quarantine contracts above. Start with objective/HUD
commands; physical actor mutation is unnecessary. Add one migration and a reset path, state
inspection, event/command trace, deterministic step/seek in the review harness and rollback of
an isolated preview checkpoint. Test “change state + queue valid command + queue invalid
command + error”: no state, RNG, acknowledgment or native command may partially commit.
Test hidden closure mutation, resource conflicts, stale handles, event overflow, budget failure
and reload during pending timers. Compare repeated and resumed runs by canonical hashes.
Inspect native-resolution HUD frames and motion; measure callback/staging cost before enabling
continuous background reload. This milestone owns the first real behavior scripting API.

#### P3.1 result, 2026-10-29

P3 is split into reviewable steps. **P3.1 implements the isolated transaction foundation** in
[`boat_plugin_state.[ch]`](../src/boat_plugin_state.h), exercised by
[`validate_boat_plugin_state`](../tools/validate_boat_plugin_state.c). P3 as a whole remains
in progress. At this historical step no manifest capability or gameplay callback was enabled;
P3.2 below adds that integration.

- Every transition evaluates immutable source and runs one callback in a fresh restricted Lua
  instance, using the existing allocator, libraries, diagnostics and instruction hook. Setup,
  compilation, callback, checkpoint decoding and native output staging are protected. The
  initial combined module/callback budget is 1 M instructions and 2 MiB Lua heap, reducible by
  the host; it is not a compiler deadline. Separate migration/update budgets remain future work.
- The canonical state codec permits booleans, byte strings, signed 64-bit integers, dense
  arrays and sorted string-keyed records. Floating-point state is deliberately unsupported in
  this first step. Limits are 16 KiB, depth 16, 256 tables, 256 entries/table, 1024 bytes/string
  and 63 bytes/key. Cycles, shared tables, mixed/sparse keys, metatables and other values fail.
  Checkpoint input is bounds-checked and re-encoded to reject noncanonical or trailing bytes.
- One caller-owned objective checkpoint owns eight bounded HUD rows. `hud.set` carries its
  local ID and host generation; unknown fields/kinds, duplicate writes, stale generations and
  total capacity failure reject the batch. The harness does not render these rows or claim
  resources in an actual game session yet.
- Callback state, xorshift32 RNG, ordered event acknowledgment, tick and HUD output commit
  together after validation. Failed updates suspend callbacks, preserve the committed data,
  and allow explicit reset or successful migration. Candidate migration failure leaves even
  quarantine unchanged. Updates require increasing boundaries divisible by 12 (10 Hz at the
  native 120 Hz clock). Up to 64 events carry exact integer ticks/sequences and bounded text.
  Overflow suspends visibly rather than acknowledging loss; the caller retains the batch.
- Tests cover repeated/resumed runs, an isolated checkpoint restore/replay, hidden closure
  mutation, state/RNG mutation followed by valid/invalid command construction and error,
  invalid commands after a valid intent, conflicts, stale handles, migration failure/reset,
  malformed checkpoints, integer extremes, ticks beyond 2^53, event overflow/order, HUD limits
  and instruction/memory exhaustion. Digests encode fields explicitly, excluding padding and
  quarantine; no native floating-point physics or network format changes.

Verification, 2026-10-29, Apple Silicon/macOS, Debug, `PROTEUS_GPU_TESTS=OFF`:

- Warning-clean project rebuild and full CTest preset: **288/288 enabled tests passed**, with
  13 retired field tests disabled. The initial sandboxed run failed ten socket-dependent
  existing tests; the full rerun with local socket access passed; the final rerun completed in 13.81 s.
- All six new state cases passed under ASan/UBSan, with no sanitizer findings. Lua live-byte
  accounting is zero after every execution. macOS ASan does not provide leak detection.
- The repeated/resumed fixture hash is `72dc362096aebd83`. The 100-update fixture runs twice,
  plus init and a discarded preview step, in about 14–16 ms total in the unsanitized Debug
  validator. The final callback's Lua heap peak is 17,304 bytes. This is a small headless
  fixture measurement, not a worst-case staging deadline or a live game performance result.
- Windows CI builds and selects the new cases; Windows/Linux execution is pending CI.
  No visual, input, audio, asset-loading or live session path changed, so no new frame or
  windowed playback review was performed. GPU, browser and human acceptance are not claimed.

**Next recorded at P3.1 (completed below): P3.2** adds a lossless `BoatVoyage` output list and native integration for an optional
“visit two shelters” objective, with its HUD row and source/state revision ownership. Preserve
independent native voyage completion, use exact boundary T/T+1 staging, and test the adapter
with a virtual clock before enabling callbacks or reload in play. Then **P3.3** completes
pending-timer/reload semantics, schema-aware revision migration and removed-resource retirement,
state/trace inspection and deterministic scenario seek with a complete native preview checkpoint.
The current isolated state copy does not rewind a moving world. Timer commands, live resource
composition/second-owner rejection, retained revision/total host memory accounting, background
reload qualification and visual/manual review remained unimplemented at P3.1. P3.2 below
supersedes the live-adapter/source-ownership/visual-review limits. The isolated harness itself
is not a source revision manager or a distribution API.

#### P3.2 result, 2026-10-29

**Implemented:** lossless voyage event output and a live optional shelter-tour objective.
The [API](PLUGIN_API.md#live-optional-voyage-objective-p32) owns the current package contract.
Run `./build/wake_velocity --plugin plugins/shelter-tour` from the repository root.

- `boat_voyage_step_events` emits every shelter/weather/exposure/arrival transition in native
  order, while retaining the old latest-event field. `BoatSession` exposes each step/recovery
  output. The core remains free of Lua, drawing, file access and callback pointers.
- Explicit `objective.hud` grants `init`/`update` alongside the voyage in the same bounded
  source. Fresh-instance transactions use P3.1's canonical state/RNG/HUD command staging.
  The example counts unique BRIDGE/EAST SHORE visits before arrival; dock, repeated visits
  and recovery do not add progress. Native arrival succeeds with a completed or missed tour.
- The adapter copies all native notices into a bounded queue with exact integer ticks,
  per-run sequences and authored shelter IDs. It dispatches after T at 10 Hz and commits the
  entire successful checkpoint immediately before physics T+1. A recovery after dispatch at T
  remains queued through that commit for the next callback; only acknowledged notices retire.
  Errors/overflow suspend visibly, preserving committed data and unacknowledged events;
  native boating, weather and completion continue.
- Revision slots own accepted source bytes for active/held/Undo revisions. Manifest-only
  renames preserve state and pending output. Any behavior source byte change or capability
  change is held for an explicit native voyage/objective restart; exposure edits restart
  the session. Undo restores source with fresh state under the same policy. No old objective
  checkpoint is restored beside an advancing native world. Reset clears rows and quarantine.
- The native plugin panel renders up to eight bounded rows. Standard native packages keep the
  descriptor-only Långedrag example; the new opt-in tour lives in the source tree.

Verification, 2026-10-29, Apple Silicon/macOS, Debug, `PROTEUS_GPU_TESTS=OFF`:

- Project rebuild is warning-clean. Full CTest preset: **293/293 enabled tests passed**;
  13 retired wake-field tests remain disabled. Socket-dependent tests ran with local socket
  access. The expanded capability/init-rejection assertions also passed in Debug and ASan/UBSan.
- All **22** objective/session cases passed under ASan/UBSan, with no reports. Lua live-byte
  accounting remains zero; macOS ASan has no leak detector. The tests cover simultaneous
  notices, T/T+1 staging, recovery after dispatch, overflow with pending output, source-only
  reload/Undo, metadata renames preserving pending output, failed candidates and quarantine.
- The new 1,200-tick repeated live-adapter fixture matches native boat/voyage/weather with
  callbacks disabled at every tick, and repeats objective hash `ae172bad2f670749`. The sample's
  200 dispatches take about **24 ms CPU total**; two launches including descriptor decoding
  and init probes take about **1.5 ms CPU total** (reported measurements, no timing assertion).
  Final callback Lua peak is **21,671 bytes**. This fixture does not establish worst-case
  compilation/staging latency or a reload deadline. The fixed host is **667,328 bytes** and
  complete adapter **776,992 bytes** on this platform, including ten bounded revision slots.
- Inspected 960×600 GPU window captures show `TOUR COMPLETE: 2`, `TOUR MISSED: 0` and
  `OBJECTIVE SUSPENDED`, alongside independent native arrival and unobscured instruments.
  Three scripted 120-frame runs used temporary boat placement/error/capture instrumentation;
  it was removed. An uninstrumented 120-frame GPU demo and display-free CPU tour capture
  also completed. Artifacts are local under `build/p32-*`; no human input/playtest is claimed.
- Changed local documentation links and command examples were checked; a stale deathmatch
  milestone link in the affected testing guide was corrected. Windows CI already selects
  the extended validator; execution
  on Windows/Linux and the complete GPU CTest lane remain unverified locally.

User check, 2026-10-29: the user drove to Långedrag with P3.2 and reported
“Works well! I went to langedrag and it worked fine!” This confirms a successful human
voyage through arrival. The report does not specify intermediate shelter visits or the final
objective row, so complete/missed tour outcomes, recovery and live reload/Undo remain covered
by automated/scripted checks rather than separately confirmed human use.

Windows CI follow-up, 2026-10-29: [PR #52's Windows run](https://github.com/doomdavve/proteus/actions/runs/40902213742)
compiled successfully but crashed in 16 plugin session/race/preview checks. Large aggregate
initializers in `boat_plugin_host_start` and `wake_plugin_open` could duplicate the fixed
revision history on the stack, exceeding Windows' default 1 MB stack. Both owners now clear
their storage in place before assigning launch fields. Revision ownership and Undo limits
are unchanged. On Apple Silicon/macOS Debug, `live_objective` reproduced the crash with
`ulimit -s 1024` before the fix; all 27 session/race/preview/native-plugin checks passed under
that limit afterward. Configure and warning-clean build passed, followed by **293/293 enabled
tests** in `ctest --preset full` with local socket access and `PROTEUS_GPU_TESTS=OFF`;
13 retired wake-field tests remained disabled. No new manual playback check was performed.
Windows execution remains pending the pushed CI run; a limited-stack macOS run does not
establish MSVC behavior.

**Next recorded at P3.2 (timers completed below): P3.3** — timers and pending-timer reload policy, schema-aware live migration, resource
composition/retirement, state/command inspection and deterministic scenario seek with a complete
native preview checkpoint. Continuous background reload/callback cost is not qualified as a
deadline. Browser/rooms and targeted human edge-case checks remain open; the trip through
arrival has been user-playtested.

#### P3.3a result, 2026-10-29

**Implemented:** bounded deterministic timers in the objective transaction engine and native
solo adapter. P3.3 is split into this timer step and the remaining live migration/inspection
work. The [timer API](PLUGIN_API.md#deterministic-timers-p33a) owns the exact contract.

- Explicit `objective.timers` follows `voyage.define` and `objective.hud`; races, duplicates,
  unsupported ordering and timer commands without the host grant are rejected.
- A checkpoint owns at most 16 timers with absolute signed 64-bit simulation deadlines and
  printable local IDs. `timer.set` replaces an ID, `timer.cancel` retires it. Due records sort
  by deadline/ID and appear in a separate `context.timers` list, preserving native event order,
  sequence and acknowledgment. A timer due between 10 Hz callbacks retains its original tick.
- Due consumption, new commands, state, RNG and HUD output commit together, including native
  T/T+1 staging. Errors, malformed commands, capacity failures and queue overflow retain
  committed timers. Due timers can be re-armed in their consuming update. Checkpoint assignment
  and canonical hashing include the grant and sorted timers; timer-free hashes stay unchanged.
- Manifest-only renames keep pending timers/output. Explicit behavior restart and Undo retire
  all old-generation timers and pending output before init creates new resources at the current
  native tick. Successful isolated migrations retain pending timers unless explicitly changed;
  failed migrations preserve everything. At this historical step, schema-aware live source
  migration was not implemented; P3.3b below supplies it.

Verification, 2026-10-29, Apple Silicon/macOS, Debug, `PROTEUS_GPU_TESTS=OFF`:

- Project rebuild passed without project warnings. Full `ctest --preset full` passed
  **298/298 enabled tests** in 13.86 s with local socket access; 13 retired field tests stayed
  disabled. The sandbox-only run failed the ten existing socket-dependent tests.
- All **27** objective/session cases passed under ASan/UBSan without reports, and in an
  unsanitized run with a 1 MB stack limit. Lua live-byte checks remained zero. macOS ASan does
  not provide leak detection; the sanitizer dependency build emitted bundled raylib warnings.
  Windows/Linux execution remains pending CI; limited-stack macOS is not MSVC qualification.
- A temporary timer-enabled package ran in an uninstrumented 180-frame GPU windowed demo;
  the inspected capture shows `TIMER FIRED: 1` above unobscured native instruments. A display-free
  CPU capture completed too; that capture path does not render the authoring panel. Local
  artifacts are `build/p33-timer-window.png` and `build/p33-timer.png`; the temporary package is
  `build/p33-timer-demo`. No human input, audio listening or user acceptance is claimed.
- Changed documentation links and command examples were checked. No new visual primitive or
  rendering code changed. Full GPU CTest, browser/rooms, background callback/reload deadlines
  and human timer reload/Undo checks remain unverified.

**Next recorded at P3.3a (implemented below): P3.3b** — schema-aware live revision migration and removed-resource retirement,
resource composition/second-owner rejection, state/command trace inspection and deterministic
scenario seek with a complete native preview checkpoint. Retained revision/total host memory
accounting, separate callback/migration budgets, background staging deadline qualification,
browser/room support and targeted human reload/recovery checks remain open. The existing
P3.2 voyage-through-arrival acceptance does not establish human acceptance of timers.

#### P3.3b result, 2026-10-29

**Implemented:** schema-aware migration in the production solo adapter, explicit HUD/timer
retirement and deterministic ownership/composition, plus native state/command inspection and
checkpoint/seek review. The [P3.3b API](PLUGIN_API.md#live-migration-and-review-p33b) owns the
exact compatibility and resource contracts. Runtime qualification at this step remained open;
P3.4 below records the native result.

- Objective schemas are authored integers 1..65535. A candidate `migrate(context,state)`
  explicitly accepts the prior/target schema, including equal-version behavior edits. With
  unchanged native gameplay, F7 migrates committed state at the current native boundary with
  a fresh generation. Native boat/voyage/water/wakes continue. Success retires pending old-code
  output but retains its unacknowledged input; failure retains source, state, resources,
  quarantine and pending output. Undo migrates current state with the destination code, or
  explicitly restarts voyage/objective when that code has no migration. No stale historical
  objective state is installed beside a moving boat.
- `hud.remove` and `timer.cancel` retire resources transactionally; removing the timer grant
  retires every timer on successful migration. Retained rows/timers transfer to the new
  generation. HUD composition is bounded, namespaced by owner ID and ordered by the caller;
  duplicate owners and capacity failure reject without partial writes. The game still hosts
  one voyage owner and rejects a second owner before mutating either. This primitive is not
  multi-voyage gameplay or shared weather/music ownership.
- `boat_plugin_review DIRECTORY [CHECKPOINT_TICK [SEEK_TICK]]` runs a deterministic display-free
  calm solo fixture with player/traffic wakes. Native checkpoint APIs capture revision source
  and route strings, full objective/pending output and event queue, native clock/boat/voyage,
  presentation/water/wake pools and diagnostics/trace. Restore rebinds internal pointers and
  replays authored inputs/recovery without save polling. Inspection exposes canonical state
  bytes, current HUD/timers, queued events, pending digest and the last 16 transition hashes
  and accepted command batches. The fixed fixture is a review harness, not a persisted save,
  interactive seek UI or recorded source-edit timeline.
- Tests cover migration failure with pending T/T+1 output, schema migration/Undo after time
  advances, invalid retirement after a valid intent, timer retention/cancel/grant removal,
  composition conflicts/second-owner rejection and seek versus uninterrupted native stepping.
  Seek compares boat/voyage/presentation, both wake pools, objective/pending hashes, event
  sequence and command trace. A checkpoint survives the original owner being cleared and
  restores prior revision/history after further edits and Undo.

Verification, 2026-10-29, Apple Silicon/macOS, Debug, `PROTEUS_GPU_TESTS=OFF`:

- Project build is warning-clean. Full CTest preset passed **302/302 enabled tests** with
  local socket access; 13 retired field cases remain disabled. The sandbox-only run blocked
  the ten existing socket-dependent tests.
- All **31 objective/session cases** passed under ASan/UBSan without findings; macOS ASan
  does not provide leak detection. All objective/session cases passed with a 1 MB stack limit.
  The sanitizer dependency build emitted upstream raylib warnings. These checks include the
  additional Undo/history checkpoint coverage.
- `boat_plugin_review plugins/shelter-tour 120 360` and the zero-tick boundary command ran.
  A CPU `--time 3` plugin frame and a 1200-frame scripted GPU windowed demo completed; inspected
  GPU HUD rows remain above unobscured instruments. Local artifacts live under this worktree's
  `build/p33b-*`. CPU screenshot capture omits the authoring panel. No human keyboard driving,
  migration/recovery playtest, audio listening or user acceptance is claimed.
- All 302 local links/anchors in the six changed Markdown files resolve; new review command
  examples and game help were exercised. Initial Linux/macOS CI passed; Windows CI exposed
  the test-comparison issue recorded below; full GPU CTests, browser/rooms, deadline qualification and human
  migration/Undo/recovery checks remain open.

**Windows CI correction, 2026-10-29:** the [initial Windows run](https://github.com/doomdavve/proteus/actions/runs/40914390065)
passed 123/124 selected checks; `boat_plugin_session.preview_seek` reached the wake-pool
`memcmp` assertion and failed, rather than reporting a stack overflow. C struct assignment
need not preserve padding. The seek validator now compares every declared wake/emitter,
presentation/particle/flag and active trace/command field instead of their object bytes.
It deliberately changes known wake padding and verifies equality still passes, then changes
an emitter's tracked tick and verifies equality fails. Byte comparisons used to prove an
untouched destination remain appropriate. This changes test comparisons only, not runtime
state, physics or checkpoint storage. On Apple Silicon/macOS, the warning-clean Debug
build and all 302 enabled full-preset tests passed (13.78 s; sockets enabled, GPU CTests off);
all 20 session cases also passed with a 1 MB stack limit. A separate Clang Release build
with `-ftrivial-auto-var-init=pattern` passed all 20 session cases under the same stack limit
(one bundled `stb_vorbis.c` warning). The [replacement Windows CI run](https://github.com/doomdavve/proteus/actions/runs/40915584973)
remained in libcurl dependency setup at handoff; its MSVC result is pending. No new
audiovisual review was performed for this test-only fix.

**Next recorded at P3.3b (implemented below): P3 runtime qualification** — retained revision/total host memory accounting, separate
callback/migration budgets and measured staging/background reload deadlines. Review checkpoint
support deliberately excludes recordings/course playback, other physical boats, combat/radial
and legacy wake fields; immutable resources/directory/handling must outlive checkpoints.
Application camera/window/audio state and external effects are outside the display-free
harness. Broader preview timelines and interactive inspection belong to later authoring work.

#### P3.4 result, 2026-10-29

**Implemented:** native retained-revision/owner storage accounting, separate entry/callback/
migration instruction allowances, preserved staging cost diagnostics and a repeatable explicit
benchmark. This completes P3's trusted native implementation and local runtime qualification;
it does not establish hostile-input isolation, uninterrupted reload or human acceptance of
migration/recovery. The [API](PLUGIN_API.md#native-runtime-qualification-p34) owns exact budgets,
accounting exclusions, benchmark commands and synchronous staging policy.

- Entry evaluation retains 1 M instructions; init/reset/update receives a fresh 100,000 and
  migration 500,000, each reducible by the native caller. A single 2 MiB allocator covers both
  phases. Tests distinguish expensive entry from callback failure, allow that callback under
  migration's larger budget and verify failed entry/migration rollback without quarantine.
- The host reports reachable source bytes and counts across history, a held candidate and
  Undo without double counting. A 32-reload full-source test fills/evicts history and checks
  staged, committed, Undo and failed-stage accounting. Ten 64 KiB slots are preallocated;
  logical retirement does not free their physical reservation. Native inspection includes
  all adapter-owned buffers, objective/pending state, events and trace. A static assertion
  caps the persistent native owner at 1 MiB; its single Lua heap adds 2 MiB. Compiler/state
  scratch stack, allocator metadata, borrowed world/view and extra preview copies are separate.
- Location validation previously cleared successful Lua cost diagnostics. Staging now keeps
  descriptor cost and aggregates the init probe, taking the maximum sequential heap peak.
  The benchmark includes compile-heavy full-size sources and failure paths, outside CTest.

Measured on Apple Silicon/macOS, Debug, 100 samples per case, GPU CTests off. Times are
elapsed UTC milliseconds; p95/maximum are sample observations, not worst-case deadlines:

| Case | p95 ms | Maximum ms |
| --- | ---: | ---: |
| Reference stage, descriptor plus init probe | 0.438 | 0.505 |
| Read and stage reference | 0.250 | 0.382 |
| Padded 64 KiB stage | 0.606 | 0.666 |
| Dense 64 KiB table stage | 7.211 | 7.373 |
| Nested-token compiler rejection | 0.044 | 0.047 |
| Reference update | 0.128 | 0.175 |
| 100,000-instruction callback exhaustion | 0.820 | 0.850 |
| 500,000-instruction migration exhaustion | 3.727 | 3.789 |
| 1 M-instruction entry exhaustion | 7.758 | 7.923 |
| Heap rejection | 0.050 | 0.107 |
| Successful expensive entry plus callback | 6.028 | 6.095 |

The host reserves **667,344 bytes**; the entire native adapter reserves **824,968 bytes**.
Including its allowed Lua heap gives **2,922,120 bytes** of owned storage, excluding native
stack/allocator overhead/world/view. Each native preview checkpoint adds **859,464 bytes**.
Dense staging peaks at **694,584 Lua bytes**, reference update at **21,757 bytes**; every
reported execution closes with zero live Lua bytes. Committed full-source history uses
589,824 payload bytes, within rather than additional to the fixed native reservation.

**Decision recorded at P3.4 (main-game descriptor staging superseded by P4.2):** retain synchronous authoring pauses for trusted local edits. The experiment
meets local review thresholds of 100 ms per stage and 8.33 ms per reference update. These
are measured review thresholds, not enforced wall-clock deadlines, real-time guarantees or
exhaustive hostile-code bounds. Save polling also pauses synchronously; simulation delta is
discarded. No background worker was added. P4 must establish isolation/cancellation before
arbitrary imports or uninterrupted background authoring; parser/C-library work is outside
the instruction hook. This explicitly resolves the earlier P3 staging/background question
for the current local-only scope rather than claiming a worker deadline was tested.

Verification, 2026-10-29, Apple Silicon/macOS, Debug:

- Project build and full `ctest --preset full` pass **304/304 enabled tests**, with local
  sockets and `PROTEUS_GPU_TESTS=OFF` (14.34 s); 13 retired field cases stay disabled.
  All 282 local links/anchors across the four changed Markdown files resolve; benchmark and
  review command examples were exercised.
- All **33 objective/session cases** pass under ASan/UBSan without findings and in a separate
  unsanitized run with a 1 MiB stack limit. macOS ASan does not provide leak detection; Lua
  allocator live-byte checks remain zero. Dependency builds emitted upstream raylib warnings;
  the final project rebuild is warning-clean. The benchmark and review command above ran.
- No rendering, motion/input, audio or asset-loading behavior changed; no new audiovisual
  review or human migration/recovery playtest was performed. Windows/Linux execution and
  timing await CI/target qualification; browser, isolated workers and hard wall-clock
  cancellation remain unverified. No user acceptance is inferred from automated checks.

**Next: P4**, beginning with focused capability/schema/validation/preview operations and the
import/background staging isolation gate. Keep P3 human migration/Undo/recovery and target
performance qualification visible alongside that work.

### P4 — practical authoring on native and browser

**Deliverable:** a human and an external LLM can discover the supported API, author either
example, validate it, inspect state, capture a frame and apply/undo a revision using the same
bounded host operations. Add small schemas/examples and structured errors, not a second game
engine or a bespoke script IDE.

Provide a focused validator/preview CLI and optional local tool bridge: list capabilities,
inspect the selected world/owner, read/write the selected plugin workspace, validate a
candidate, run a bounded virtual scenario, capture a frame, stage/apply and revert. Pin each
request to a bundle/base revision and return test evidence with artifact/revision IDs. A
hand-authored handful of map anchors and camera bookmarks can make prompts useful without a
semantic-map project. Developer inspection must not become permanent clutter in normal play.

Compile the same runtime into Emscripten, bundle example text, and provide import/reload/export
with the same transactions. Browser source/state persistence is host-owned and must handle
storage denial; plugins do not get filesystem APIs. Verify package paths after relocation,
native load/reload and browser repeated sessions, source edits, errors and cleanup. Check
memory/compiler/callback costs on physical devices; declare unsupported capabilities rather
than silently changing behavior. Native shell/file-watching assumptions must not leak into
the common API. Browser agent connectivity can remain deferred.

#### P4.1 result, 2026-10-29

Implemented the first native authoring slice: `tools/plugin_author.py capabilities` reports
supported operations, kinds, capabilities, budgets, examples and explicitly unsupported work.
`validate DIRECTORY` snapshots the two bounded regular package files and invokes a disposable
`boat_plugin_worker` with immutable length-framed bytes. The worker uses the existing revision
host, including location checks and the objective init probe. It returns bounded JSON errors,
gameplay digest and cost evidence; the supervisor adds a SHA-256 bundle ID and caller-supplied
base revision. An expected bundle ID rejects changed snapshots before starting a worker.

The reusable `ValidationJob` offers nonblocking poll and explicit cancellation. Its caller
supplies monotonic time; expiry/cancellation kills and reaps the worker and closes temporary
handles. No source is evaluated in the supervisor or applied to a running session. This is
process fault/deadline isolation over restricted Lua, **not an OS security sandbox**. Source
acquisition still runs synchronously; regular local files are required. No game/background
staging path, imported-candidate installation, full schema discovery, scenario preview,
capture, tool bridge, relocation packaging or browser support is claimed.

Verification, 2026-10-29, Apple Silicon/macOS, Debug: build and full
`ctest --preset full` pass **305/305 enabled tests** (18.96 s), with local sockets and
`PROTEUS_GPU_TESTS=OFF`; 13 retired field cases remain disabled. The build emitted an upstream
raylib/stb_vorbis warning; project sources are warning-clean. All 286 local links/anchors in
the four changed guides resolve. Discovery and all three documented validation commands ran.
The dedicated test's ten cases cover all
three examples, structured Lua errors and budget rejection, framing limits, changed snapshot
pins, oversized/link/FIFO rejection, virtual-time deadlines/cancellation/reaping and invalid
worker output. An intentionally stuck real process is killed/reaped at a virtual deadline;
native worker success also runs through the reusable supervisor. No visual,
input, audio or game asset-loading behavior changes; no new audiovisual or human acceptance
review is claimed. Windows/Linux execution awaits CI.

**Next recorded at P4.1 (native slice implemented below): P4.2.** Add machine-readable schema discovery and bounded scenario preview/evidence;
connect isolated requests to live candidate staging with base-revision rejection and atomic
commit/Undo. Do not treat the returned correlation revision or digest as permission to apply,
or re-evaluate imported Lua on the game thread. Then qualify imported/background staging and
browser import/export separately. P3 human recovery/migration and target timing remain open.

#### P4.2 result, 2026-10-29

Implemented native schema discovery, isolated bounded scenario evidence and live descriptor
staging for the existing **trusted local** voyage workflow:

- `plugin_author.py schema` returns JSON Schema projections for manifests, voyage/race data and
  objective commands, plus callback/context/state descriptions and explicit native semantic
  constraints. This describes Lua records, not executable functions or a second interpreter.
  The native parser remains authoritative; automated tests exercise every advertised numeric
  descriptor bound against that decoder.
- `preview DIRECTORY --ticks N` snapshots the same immutable bytes as validation and runs in
  a disposable worker (0..72000 virtual ticks, default 360). The voyage fixture uses the
  complete checkpoint/seek harness, player/traffic wakes and objective HUD; the race fixture
  drives four native entrants. Versioned fixture names, tick/position or fleet evidence,
  committed/pending objective digests and a SHA-256 artifact ID accompany bundle/base pins.
  Failed updates reject the preview instead of producing a success artifact. This is scenario
  evidence, not course completion, playability, frame capture or performance qualification.
- `wake_velocity --plugin` now stages settled saves/F5 through its own headless
  `--plugin-stage-worker` process. It receives immutable framed bytes and returns a versioned,
  bounded, fieldwise binary64/string descriptor plus diagnostics; no raw structs, pointers or
  interpreter state cross the boundary. Completion uses the same native schema/map checks
  and classification without re-evaluating the candidate Lua. F7 is ignored while staging;
  F6 cancels/reaps it, and superseding reloads and shutdown also cancel/reap. The active
  revision continues stepping. Worker errors/deadlines retain active source/state/world.
- Every request pins the active revision and a monotonic commit epoch, including Undo.
  Completion against a changed base is rejected, even if the active revision number has
  returned to the same value. Successful candidates use the existing between-tick
  rename/migration/restart and Undo transactions. An isolated completion is an internal
  request receipt, not an externally installable digest or cache artifact.
- A request owns one bounded source snapshot (native reservation capped at 80 KiB), two
  temporary streams and one worker; there is no unbounded request queue. Inspection reports
  request bytes separately from the game owner. Process RSS, C/stdio/OS overhead and the
  worker's world/Lua allocations are separate. Cancellation/deadline acceptance follows the
  caller's polls; this is process fault isolation, not an OS security sandbox.

Verification, 2026-10-29, Apple Silicon/macOS, Debug, `PROTEUS_GPU_TESTS=OFF`: build and full
`ctest --preset full` pass **306/306 enabled tests** (14.33 s) with loopback access; 13 retired field
cases remain disabled. All 311 local links/anchors in the five changed guides resolve.
The sandbox-only run failed the ten socket-dependent cases; the
unrestricted full run passed. Project sources are warning-clean (the clean initial build
emitted the existing upstream stb_vorbis warning). The authoring tests cover schemas and all
numeric descriptor bounds, repeatable voyage/race artifacts, callback rejection, framing,
bundle pins and virtual-time supervisor cleanup. `boat_plugin_stage` covers live native
stepping during staging, Apply/rebind, repeated holds, Undo with same-number/new-epoch stale
rejection, virtual deadline against a stuck worker, cancellation, malformed/oversized output,
syntax rejection and the actual F5/completion adapter. The 120-frame windowed GPU demo passed.
A relocated executable/assets/package check in paths containing spaces verified save polling,
worker startup and automatic rename commit during a 240-frame GPU demo. These are agent-run
smoke checks; no human keyboard/migration/Undo/recovery acceptance or timing claim is made.

**Limits and next recorded at P4.2 (browser descriptor subset implemented below):** the standalone race trial still stages synchronously. Native startup,
objective updates and Apply-time init/migration retain the trusted synchronous execution
contract; map/file acquisition and temporary-stream setup also remain synchronous. Isolated
**descriptor staging** does not qualify arbitrary imports or uninterrupted objective execution.
Before enabling browser imported gameplay, isolate those remaining evaluation paths (or limit
and explicitly advertise the supported capability subset), qualify cancellation/storage
failure/repeated sessions on that target, then add browser import/reload/export. Interactive
preview controls, capture, workspace bridge, curated anchors and release-tool packaging also
remain open. Windows/Linux process execution awaits CI; target timing and P3 human recovery
qualification remain open.

#### P4.3 result, 2026-10-29

Implemented browser import/reload/export for an explicitly limited capability subset:
**solo `outer` voyage descriptors (`voyage.define`, schema 1)**. Objectives/timers and race
imports reject before entry evaluation; rooms and recordings retain compiled content.
This resolves the import gate for descriptors without extending the trusted native callback
execution contract to browser imports.

- The opt-in **PLUGIN VOYAGE** panel loads the reference example, imports/exports a version-1
  source bundle, edits the two text files, stages/reloads, launches the staged voyage and
  applies/undoes held revisions. Opening it pauses solo input; editor keys do not drive the
  boat. Normal play retains the shared title. `plugin_author.py export DIRECTORY` emits the
  same portable JSON bundle; exports are source drafts, not validation or state receipts.
- Each evaluation uses a disposable Web Worker running the same pinned Lua and native
  manifest/schema/map checks. The game consumes the matching fieldwise receipt and rechecks
  native data, including its capability subset, without evaluating imported source at launch,
  reload, Apply, Undo or on a tick. The prepared receipt is consumed once and matched to exact
  bytes and revision/commit epoch; a later source reload creates a fresh worker request.
- The browser rejects changed bases, syntax/capability errors, bad framing/output and expired
  or canceled requests while retaining the active voyage. UI validation and a native-triggered
  reload each own at most one job (transport cap two), with five-second acceptance deadlines.
  Late callbacks are inert; completion/cancel/page hide terminates workers and retires entries.
  Tab scheduling can delay watchdog delivery; this is not a hard real-time deadline.
- The bundle contains `format`, `version`, and UTF-8 `manifest`/`source` strings, with the
  existing 4 KiB/64 KiB encoded-byte caps and a 200,000-byte wrapper cap. Source persistence is
  host-owned localStorage, with visible nonfatal denial/corruption handling. Restored source
  must revalidate before play. No native world progress, callback state or historical Undo
  checkpoint is persisted/exported.
- Browser packaging content-addresses the lazy worker wrapper/module/Wasm and example bundle,
  ships the Lua notice, and removes obsolete generated assets. A PR-only browser build workflow
  checks the event contract and packaged Wasm build without publishing a site. Windows CI now
  builds/runs the disposable authoring/staging checks; its initial constant-NaN fixture compile
  error was corrected to the portable `NAN` macro. The reference parity check now compares
  each storm field's bits instead of unspecified struct padding exposed by MSVC.

Verification, 2026-10-29, Apple Silicon/macOS: native Debug build and full CPU CTest pass
**307/307 enabled tests** (14.84 s), local loopback enabled, GPU CTests off; 13 retired field
cases disabled. Emscripten **6.0.12** MinSizeRel `web` builds and packages successfully; project
sources are warning-clean, with the clean web build's upstream miniaudio/stb warnings retained.
All 384 local links/anchors across the nine changed guides resolve.
The Node contract covers bundle encoding/limits, independent immutable frames, bounded worker
slots, one-use receipts, stale epochs, cancellation/late output, virtual deadlines and worker
fault/exit/framing rejection. `check_web_plugin_author.py` passes in local Chrome with SwiftShader:
export/import equality, objective/race rejection, real worker launch, live rename, syntax error,
held water Apply/Undo, stale response across Undo, cancellation/late completion, source
restore/revalidation/repeated sessions, and storage denial. Gameplay, staged/held/error and
storage-denial captures were inspected. This is agent-run browser qualification, not user play
acceptance; Firefox/Safari, physical devices, tab suspension, peak memory/runtime and imported
objective/callback execution remain unqualified. No website/release was published.

**Next:** target/browser qualification and remaining P4 capture/workspace/anchor tools. Before
adding objectives to browser imports, isolate their init/update/migration/Undo evaluation and
state persistence transactions. Browser race authoring needs its own native preview integration.
The P3 human migration/Undo/recovery and target timing gates remain open.

#### P4.4 result, 2026-10-30

Implemented selected draft workspace operations in `tools/plugin_author.py`:
`workspace-read` returns both bounded UTF-8 files and the validation-compatible SHA-256
bundle ID; `workspace-write` accepts one allowlisted file from bounded stdin and requires
`--expect-bundle`. It checks the pin before and after preparing a same-directory replacement,
then atomically replaces that one file. Failed preparation/replacement cleans temporary files.
Responses identify source drafts and previous/resulting bundle IDs. No Lua is evaluated and
no live revision is installed; invalid drafts remain editable. See the
[CLI contract](PLUGIN_API.md#native-authoring-tools-p41) for commands and concurrency limits.

Verification, 2026-10-30, Apple Silicon/macOS Debug, GPU CTests off: configure/build
succeeded using the existing pinned dependency source trees. Full `ctest --preset full`
passed **307/307 enabled tests** (14.29 s) with loopback access; 13 retired field cases
remain disabled. The sandbox-only run failed the ten socket-dependent cases. After the final
response-construction refinement, all **19 authoring checks** passed, including five workspace
checks for UTF-8 round trips, stale pins, edits during preparation, bounded input, path/link/FIFO
rejection, failed-replace cleanup and actual CLI draft saves without Lua evaluation.
All 266 local links/anchors in the three changed guides resolve; `git diff --check` passes.
After the Windows CI encoding failure, the preparation test compares preserved bytes and the
other authoring fixture reads specify UTF-8. On 2026-10-30, macOS Debug configure/build and
full CTest passed again (307 enabled; 13 disabled), and all 19 authoring checks passed with
UTF-8 mode disabled and the C locale. Windows verification of the fix awaits CI.
No new target performance claim is made.
No gameplay, rendering, input, audio or runtime asset-loading changes are included; no new
manual playback or human authoring acceptance is claimed.

**Remaining P4 work (recorded at P4.4; capture and anchors implemented below):** frame capture with pinned evidence; curated anchors and camera bookmarks;
live owner inspection/stage/apply/undo tool access and optional bridge transport (P4.6 below); interactive
preview controls; release-tool packaging; broader Windows/Linux/browser and physical-device
memory/compiler/callback qualification, tab suspension and repeated-session cleanup. Browser
objectives require isolated init/update/migration/Undo and state persistence; browser races
need native preview integration. P3 human migration/Undo/recovery acceptance remains open.
The new CLI operates on trusted local source drafts, uses optimistic pins rather than editor
locks, and does not provide an atomic two-file transaction or a live game connection.

#### P4.5 result, 2026-10-30

Implemented `capture` and `anchors` in `tools/plugin_author.py`; see the
[capture contract](PLUGIN_API.md#frame-capture-and-anchors-p45).

- `anchors` publishes seven hand-curated poses from `tools/plugin_anchors.json`: five outer
  voyage places (dock, bridge, east shore, squall edge, Långedrag arrival) and the river sprint
  start/finish. Each is a prompt-friendly named coordinate and a camera bookmark. Plugins cannot
  read them; they add no gameplay data or semantic-map project.
- `capture` pins one draft snapshot (`--expect-bundle`, `--base-revision`), writes those exact
  bytes into a private package and runs the existing display-free `wake_velocity --renderer
  cpu --plugin … --screenshot` path with the anchor's `--start` and optional camera distance/
  height, under the validation job's poll/kill/reap deadline. Evidence records the game's
  reported plugin id, revision and gameplay digest, plus PNG size/SHA-256; `artifact_id` hashes
  the pins, scenario and evidence. Verified frames atomically replace `--output`. Native load
  diagnostics are `rejected`; other failures are `capture_failed`.
- No C, rendering, input, audio or runtime asset-loading code changed. The game's screenshot
  path already composed `--plugin`, `--start` and camera options.

Limits: voyage plugins only (the race trial has no screenshot path); CPU 320×200 world frames
without HUD/objective rows; the child is the trusted native game, so its Lua runs as in `--plugin`,
bounded only by the process deadline. Frames are repeatable per build/platform, not portable
golden images. No live game connection, inspection, Apply or Undo is provided.

Verification, 2026-10-30, Apple Silicon/macOS Debug, GPU CTests off: configure (existing
pinned dependency sources) and build succeeded warning-clean for project sources. Full
`ctest --preset full` passed **308/308 enabled tests** (16.47 s; 13 retired field cases
disabled), including the new slow `render` test `boat_plugin_capture` (8.7 s): every outer anchor
is navigable, two pinned shelter-tour captures produce identical frames/evidence/artifacts, a
syntax-error candidate is `rejected`, and the CLI honours `--expect-bundle` and replaces
`--output` without leftovers. Fake-process checks cover deadline cleanup of the private package,
rejected/failed/no-revision/non-PNG output and request limits. Bridge, squall-edge, Långedrag and
shelter-tour bridge captures were inspected: boat, wake, shoreline and squall render at the expected
poses. PR CI on 2026-10-30 passed on Linux and macOS (308/308, `boat_plugin_capture` ran) and
Windows; the Windows lane runs its CPU/multiplayer subset, so Windows capture is unverified. No human authoring acceptance is claimed.

**Next recorded at P4.5 (live tool bridge implemented below):** live tool bridge — authenticated
local access to owner inspection and stage/apply/undo in a running native game — then interactive
preview controls, release-tool packaging and broader target/browser qualification. Browser
objectives still require isolated init/update/migration/Undo and state persistence; race capture
needs a race-trial screenshot path. P3 human migration/Undo/recovery acceptance remains open.

#### P4.6 result, 2026-10-30

Implemented the live tool bridge; see the [bridge contract](PLUGIN_API.md#live-tool-bridge-p46).

- `wake_velocity --plugin DIR --plugin-bridge MAILBOX` (native, opt-in) opens a private local
  directory mailbox: POSIX mode 0700 owned by the user (anything else is refused), and a
  `session` file (0600) with a random per-launch token and session id. Requests without the
  current token are `unauthorized` before any owner state is revealed; closing withdraws the
  session and answers a pending request `cancelled`. No sockets or new dependencies; the game
  reads one request per 0.25 s poll between ticks, focused or not.
- `tools/plugin_author.py live-inspect|live-stage|live-apply|live-undo|live-discard` use the
  same job lifecycle as validation/capture. Inspection returns the active revision, commit
  epoch, digest, held candidate and change kind, outcome, panel text, diagnostics, the boat and
  voyage progress, plus the existing developer inspection lines. Every mutation pins the commit
  epoch it saw and the active (stage/undo) or held (apply) revision; stage also pins the draft
  `--expect-bundle` and sends those exact bytes. Stale pins change nothing.
- Requests run through the existing between-tick transactions: stage uses the same isolated
  staging as a save/F5 and answers when it finishes (or `superseded` by a save/F5); apply/undo/
  discard are F7/F6. A water Apply restarts the session as F7 does. `WakePlugin` now records a
  machine outcome per status and counts staging requests.
- Live smoke testing found that a bridge stage of bytes the package already held, but that no poll
  had seen yet, was staged again by save polling and silently dropped a held Undo candidate as
  UNCHANGED. The bridge now marks its bytes as the package's save only when the package on disk
  matches; a regression case covers it.

Verification, 2026-10-30, Apple Silicon/macOS Debug, GPU CTests off: configure (existing pinned
dependency sources) and build succeeded, warning-clean for project sources. Full
`ctest --preset full` passed **309/309 enabled tests** (13 retired field cases disabled),
including the new `wake_velocity_bridge` validator (token refusal, poll cadence, missing/stale
pins, malformed payloads, deferred isolated staging while the session steps, busy, held Apply,
Undo/Discard/conflicts, rejection diagnostics, water restart, Discard cancelling a pending stage,
save-poll agreement and session withdrawal on close) and `boat_plugin_author`'s new client
checks: request framing, hard-link no-clobber, virtual deadlines, foreign responses, pin
requirements, and an end-to-end CLI run against `validate_wake_bridge --serve` (inspect, rename
applied, stale repeat, held route Apply, Undo, Discard, rejected candidate). Agent-run windowed
smoke test: `wake_velocity --demo` with `--plugin-bridge` on a copied Långedrag package; the CLI
inspected it, staged a rename (applied), staged a water change (held), applied it (session
restarted), undid it (held) and applied the Undo. Save polling applied an editor-style save during
the same session. On exit the mailbox held no files. No human play acceptance is claimed.

Limits: voyage plugins in `wake_velocity` only (no race trial, browser or room bridge); one
client at a time; cancelling a client does not retract a request the game has read. Windows
uses the directory's inherited ACL instead of a mode check, and Windows/Linux bridge behavior
awaits CI. The bridge drives trusted native Lua exactly as F5–F7 do; it is same-user access
control, not a sandbox. No rendering, audio or asset-loading change; no new audiovisual review.

**Next:** interactive preview controls, then release-tool packaging and broader target/browser
qualification. Browser objectives still require isolated init/update/migration/Undo and state
persistence; race capture and a race bridge need race-trial support. P3 human
migration/Undo/recovery acceptance remains open.

#### P4.7 result, 2026-10-30

The user chose release-tool packaging ahead of interactive preview controls, which were
deferred. Implemented the [packaged authoring tools](PLUGIN_API.md#packaged-authoring-tools-p47):

- `tools/package_plugin_tools.py` installs `plugin_author.py`, `plugin_schema.py`, the anchors,
  `PLUGIN_API.md` and a README into a package's `authoring/` directory. It copies
  `boat_plugin_worker` beside the game and the three examples into `assets/plugins`. It also
  writes `plugin_tools.json` (layout 1), which records those paths relative to `authoring/`.
  `tools/macos/package.py` and the Windows workflow use it. Before, packages shipped only
  `langedrag`.
- macOS: the worker is in `Contents/MacOS`, checked for arm64 and system-only libraries like the
  game, and ad-hoc signed before the app. The tool sets `sys.dont_write_bytecode`, so using it
  leaves the signed bundle unchanged.
- `plugin_author.py` takes its default worker, game, anchors and capture working directory from
  `plugin_tools.json` when present and from the checkout otherwise. It refuses other layout
  versions. `capabilities` reports `layout`, and in a package it reports absolute anchors,
  reference and example paths.
- CI: the macOS relocated-app step copies `shelter-tour` out of the extracted app, runs the
  packaged validate, preview and capture, then re-verifies the signature. The Windows relocated
  check runs the same operations on a `langedrag` copy through the runner's Python.
  `test_plugin_author.py Packaged` builds a layout with the real helper in a temporary path
  containing spaces and drives the copied tool from another directory. It covers capabilities,
  anchors, validate of every example, preview, no bytecode cache and refusal of layout 2.
  `boat_plugin_author` runs it without capture; `boat_plugin_capture` adds a capture.

Verification, 2026-10-30, Apple Silicon/macOS Debug, GPU CTests off: build warning-clean for
project sources (only the bundled stb_vorbis warning). Full `ctest --preset full` passed **309/309
enabled tests** (13 retired field cases disabled). Agent-run local package check: I packaged a
Debug build with `tools/macos/package.py` and extracted the ZIP to a temporary directory.
`codesign --verify --deep --strict` passed. From another directory, the packaged tool reported
`layout` kind `package`. It validated `langedrag`, previewed a copied `shelter-tour` and
`river-sprint`, and captured a 320×200 frame at `outer.dock`, which I inspected. The signature
still verified afterwards. The macOS and Windows CI package steps have not run yet; the PR's runs
are their first check. No human acceptance is claimed.

Limits: Python is not bundled. Linux has no native package. Packaged `river-sprint` can be
validated and previewed but not played, because `boat_race_trial` is not shipped. The macOS app
is ad-hoc signed; on a downloaded, quarantined app, Gatekeeper may need **Open Anyway** before
it runs the worker, which is unverified. The packaged `PLUGIN_API.md` links into the source tree.
No gameplay, rendering, audio or runtime asset-loading change; no new audiovisual review.

**Next recorded at P4.7 (automated part implemented below):** broader target/browser qualification: Windows/Linux and physical-device memory,
compiler and callback costs, tab suspension and repeated-session cleanup. Browser objectives
still require isolated init/update/migration/Undo and state persistence. Interactive preview
controls (proposed: in-game checkpoint/rewind over `wake_plugin_preview_*`) are deferred and
unspecified. P3 human migration/Undo/recovery acceptance remains open.

#### P4.8 result, 2026-10-30

Qualified what an agent and CI can check without physical devices or a human player:

- `tools/check_web_plugin_author.py` takes `--browser chromium|firefox|webkit` and uses
  Playwright's engines; `--executable` still selects a Chromium-family binary such as system
  Chrome. Besides the P4.3 transactions, it now checks:
  - **Repeated validations:** 20 in a row (`--cycles`). Each runs in a fresh worker, and every
    worker must have closed afterwards.
  - **Tab freeze (Chromium only):** freezing the page mid-validation through the lifecycle API
    for 1.5 s must leave a consistent owner and a running game. Freezing for 6 s while a real
    result is held must, on resume, reject at the five-second deadline, keep the voyage and
    ignore the late result.
  - **Navigation mid-validation:** leaving the page during validation, then three restored
    sessions (`--sessions`). Each must revalidate, launch and leave no worker alive.
  - **Evidence:** per-validation durations, the Chromium main-thread heap after garbage
    collection, and the engine and its version go to `results.json`. Durations and heap are
    recorded, not asserted.
- The PR-only **Browser plugin authoring** workflow now serves its built site and runs the check
  on Chromium, Firefox and WebKit (Playwright 1.64.1 on Ubuntu). It uploads the screenshots and
  `results.json`, and a failed run also saves `failure.png` and the console log. Headless Firefox
  cannot create a WebGL context on the GPU-less runner ("Exhausted GL driver options"), so CI runs
  it with `--headed` under `xvfb-run`, where Mesa renders in software.
- Native targets: Linux CI already ran every plugin test (309/309 on `1f0b4a2`). Windows CI ran
  them all except `boat_plugin_capture`, which it now runs too. Both jobs now run
  `boat_plugin_bench plugins/shelter-tour` and write its staging/compiler/callback costs to the job
  summary. Fixed the two GCC warnings that Linux CI reported in plugin sources
  (`boat_plugin_lua.c` unused value, `validate_boat_plugin.c` format truncation); the
  descriptor behavior is unchanged.
- CI exposed a Windows-only test bug: the `Packaged` capture case built its temporary layout on
  `C:` and pointed it at the game on `D:`, which a relative layout path cannot express. The case
  now builds the layout on the game's drive. Real packages keep the game inside the package.

Verification, 2026-10-30, Apple Silicon/macOS: native Debug build warning-clean for project
sources; full `ctest --preset full` passed **309/309 enabled tests**. Emscripten 6.0.12
MinSizeRel `web` build packaged. The extended check passed on Playwright Chromium 156.0.8078.4,
Firefox 157.0 and WebKit 27.2, and on system Chrome 156.0.8078.74:

| Engine | Cold validation | Median of 20 | Max of 20 | Freeze cases |
|---|---|---|---|---|
| Chromium 156 | 90 ms | 60 ms | 61 ms | passed (rename applied after resume; deadline across freeze) |
| Firefox 157 | 67 ms | 53 ms | 55 ms | not exercised |
| WebKit 27.2 | 41 ms | 37 ms | 39 ms | not exercised |

The Chromium main-thread heap after collection went from 2.74 to 2.82 MB across the 20
validations. I inspected the voyage captures from all three engines; each rendered the Långedrag
voyage with the plugin HUD row. No human acceptance is claimed.

CI on PR #65's final commit passed every workflow. Linux ran 309/309 and Windows 130/130, now
including `boat_plugin_capture`. On Ubuntu runners the three engines took:

| Engine (Ubuntu CI) | Cold validation | Median of 20 | Max of 20 |
|---|---|---|---|
| Chromium 156, headless | 123 ms | 91 ms | 135 ms |
| Firefox 157, headed under Xvfb | 136 ms | 89 ms | 108 ms |
| WebKit 27.2, headless | 79 ms | 70 ms | 80 ms |

`boat_plugin_bench plugins/shelter-tour` reported these p50 costs on the shared runners:

| Runner | Reference stage | Dense 64 KiB stage | Update | Callback budget trip |
|---|---|---|---|---|
| Linux | 0.15 ms | 5.6 ms | 0.07 ms | 0.43 ms |
| Windows | 0.12 ms | 3.6 ms | 0.06 ms | 0.37 ms |

Limits: these are desktop engines on one Mac and on CI runners, not Safari, mobile browsers or physical
devices. Browser freeze is Chromium's lifecycle emulation, not a real background-tab policy.
Firefox/WebKit freeze, worker Wasm memory and browser-process memory are not measured. Native
costs on Linux/Windows come from shared CI runners, so they are evidence, not budgets. No
gameplay, rendering, audio or asset-loading change; no new audiovisual review.

**Next recorded at P4.8 (implemented below):** browser objective isolation. Physical-device memory
and timing, real background-tab behavior on phones, Safari, and P3 human
migration/Undo/recovery acceptance need the user.

#### P4.9 result, 2026-10-30

Implemented the reviewed browser objective isolation in two connected slices:

- **P4.9a protocol/evaluator:** `boat_plugin_eval` defines PEQ1/PER1, with fieldwise encoding,
  lossless signed/unsigned 64-bit fields, 128 KiB request and 48 KiB reply caps. Replies pin a
  unique request, revision/epoch/candidate, action/tick and complete canonical input (source,
  checkpoint and event batch). Host decoding validates framing, canonical state, capacities,
  ownership/generation/schema/grants and acknowledgment without running Lua. The worker reuses
  the existing atomic transaction engine and creates a fresh bounded Lua VM for every batch.
- **P4.9b browser lifecycle:** optional voyage HUD/timer grants now use one reusable worker/Wasm
  evaluator, separate from the two existing disposable descriptor-staging lanes. All init,
  update, reset and migration/Undo calls are isolated. At dispatch T, solo simulation waits
  before T+1; rendering/UI and deadline polling continue. Waiting wall time is discarded rather
  than becoming simulation time. Init completes before native stepping. Candidate success
  commits code/state/schema/generation together; candidate failure retains active state and old
  pending output. Update failure/timeout preserves the last commit and suspends callbacks while
  native boating continues. Undo/cancel can abandon a pending migration; late output is inert.
  A reset requested while waiting is deferred. Other Apply/Undo/reload attempts during an
  ordinary callback report BUSY and can be retried.
- **In-session persistence:** C owns state, RNG, timers, events, HUD and queued output between
  worker calls. Evaluator replacement cannot turn VM memory into a save. Page reload still ends
  the native session and restores source only; durable objective/world/Undo checkpoints remain
  out of scope. The evaluator's Wasm memory is capped at 64 MiB, stack at 1 MiB, and Lua heap at
  the existing 2 MiB. Worker/browser-process overhead remains additional.
- **Qualification:** `boat_plugin_eval` adds native parity, wide integer, max-state/source/
  capacity, malformed/truncated/stale receipt and protected-field tests. The Node transport
  contract adds one-job ownership, reuse, cancellation, late response/watchdog isolation,
  deadline, malformed output and worker failure cases under virtual time.
  `check_web_plugin_objective.py`, called by the existing three-engine browser authoring job,
  adds real imported init/update/timer execution, pinned simulation ticks, recovery-event
  ownership, migration cancellation/Apply/Undo/restart, atomic candidate failure, quarantine,
  timeout/freeze and repeated-session cleanup. It records callback delivery costs and Wasm
  linear memory separately from the descriptor validation evidence.

Verification, 2026-10-30, Apple Silicon/macOS: Debug configure/build passed with the existing
pinned dependency sources. Project sources are warning-clean (the first build retained the
bundled `stb_vorbis` warning). With local socket access, full `ctest --preset full` passed
**310/310 enabled tests** (13 retired field cases disabled, GPU CTests off). Node transport
contracts passed. The new codec/validator also passed ASan/UBSan with the existing Lua/core
libraries uninstrumented; this is not a whole-runtime sanitizer claim. An agent-run windowed
native `shelter-tour --demo --frames 180` completed without failure; this is not human play.

Emscripten 6.0.12 MinSizeRel `web` packaged successfully. The complete extended browser check
passed on Playwright Chromium 156.0.8078.4, Firefox 157.0 and WebKit 27.2. Each ran 20 descriptor
validations, objective transactions/failures and three restored sessions. Chromium also passed
an objective result held across a six-second lifecycle freeze, in addition to the existing
short descriptor freeze. Captures from all three engines were inspected and show the running
objective HUD. Worker evaluation and main-thread delivery were measured separately on the
small reference callback fixture:

| Engine (local Mac) | Worker evaluation mean / max | Delivery mean / max | Warm callbacks sampled |
|---|---|---|---|
| Chromium 156 (SwiftShader) | 0.26 / 0.40 ms | 70.1 / 123.7 ms | 21 |
| Firefox 157 | 0.40 / 1 ms | 2.9 / 5 ms | 20 |
| WebKit 27.2 | 0.40 / 1 ms | 3.6 / 5 ms | 20 |

All three reported **16 MiB Wasm linear memory** for the active objective evaluator. These
figures exclude deliberate result holds/fault tests, initial game/runtime startup and browser
process memory. Worker evaluation includes codec/VM work; delivery also includes browser and
game-loop scheduling. Chromium's much higher delivery delay under software rendering is an
observed limitation, not a Lua execution cost or a physical-device performance guarantee.
The measurements are evidence, not budgets. Browser startup/VM timing and memory on phones,
physical Safari and real background-tab policies remain unqualified. The browser's synchronous
staging fallback is compiled out; imported source has no fallback to main-thread evaluation.
All 380 local links across the eight affected Markdown guides resolve. No release/site was
published and no human authoring/play acceptance is claimed. Linux CI on source commit `8869e0f` passed 310/310 enabled tests; macOS and Windows workflows
also passed. The first browser CI run passed WebKit but exposed a frame-rate dependency in the
Chromium/Firefox recovery-input fixture: capped frame deltas made the held Backspace gesture
outlive the callback deadline on the software renderer. The check now drives the existing
menu's immediate recovery action and waits for observed menu/reset state. The runtime's
five-second callback deadline is unchanged. On `77b1212`, all native workflows and the full Chromium/Firefox authoring checks passed.
WebKit exposed a separate restored-session readiness assumption: revision 1 was visible
before lazy scenery loading finished, so the next reload interrupted startup with zero native
ticks. The repeated-session check now waits for an observed native tick before reloading,
proving each restored session actually played; production navigation behavior is unchanged.
Startup deadline review also found a real clock-origin mismatch: INIT dispatch precedes
`InitWindow`, which resets GLFW time. Browser plugin open/frame calls now use the stable
`emscripten_get_now` clock instead. A regression offsets the page clock and delays host
consumption of an already cached successful INIT reply: the host must expire it without
publishing state, even though the JS completion watchdog has already retired. Native callers
retain their existing clock. Final source `d78c86e` passed every workflow on
[PR #67](https://github.com/doomdavve/proteus/pull/67): Linux, macOS, Windows and the complete
three-engine browser qualification, including the cached INIT deadline regression. Prior P4.8
results remain historical evidence. GCC still reports pre-existing warnings outside the new
plugin sources.

**Next:** P5's authority/prediction/recording contract review as described in the current status.
Browser races, interactive preview controls, durable full-session saves, native asynchronous
objective execution and an in-game agent are not implemented. Physical Safari/mobile devices,
real background-tab policies and P3 human migration/Undo/recovery acceptance remain open.

#### Browser CI cadence, 2026-10-30

At the user's request, the full browser matrix no longer runs on every relevant PR. PRs keep
Node contracts and the packaged Wasm build; manual dispatch retains full three-engine runtime
qualification and evidence. Superseded PR runs are canceled, and no `main` push trigger is
added. The [browser guide](WEB_BUILD.md#browser-authoring-ci-lanes-2026-10-30) owns the command
and coverage tradeoff. P5 remains the next implementation task.

Verification, 2026-10-30, Apple Silicon/macOS: Debug configure/build passed using the pinned
existing dependency sources (project sources warning-clean; bundled `stb_vorbis` warning).
Full `ctest --preset full` passed 310/310 enabled tests with local socket access, GPU CTests
off; Node authoring contracts and actionlint 1.7.7 passed. All 322 local links in the four
affected Markdown guides resolve. The full browser commands are unchanged from P4.9's green
qualification and were not rerun for this CI-only change. The PR run checks the automatic
lane and skipped browser steps. No gameplay or audiovisual change.

### P5 — authority, prediction and recorded revisions

**Deliverable:** one already proven authored scenario works in a room and can be recorded or
reconstructed with its revision/state. Implement the explicit content, command/checkpoint,
compatibility and round-boundary reload contract above in the owning multiplayer/protocol
and recording code. Add client rejection/readiness, late join/reconnect, loss/reordering,
duplicate effects, stale revisions, failed room reload and prediction replay checks under
virtual time. Verify mixed native/browser clients with matching content and explicit rejection
of old clients. Recheck resource/page limits and four-player load; human multiplayer play is
its own acceptance gate. Seamless mid-round source swaps are not required for this milestone.

### P6 — in-game asynchronous agent

The [live console step](PLUGIN_DEFAULT_PLAN.md#pc--live-console-and-edits-without-reset) of the
plugins-by-default plan supplies this milestone's command layer and live-apply policy. PC's
`tools/wake_command.[ch]` is now the dispatcher: the agent submits the same edit and `/` command
lines as a player, appears in the console, is undone the same way and cannot move the boat.

**Deliverable:** an optional in-game conversation can use the proven P4 authoring tools to
make and test a supported change, show its result and apply it under the current authoring
policy. Begin in a solo development session; shared/public-room editing is a separate host
permission and protocol feature.

Keep a provider adapter and tool dispatcher outside Lua and outside the fixed tick. The
adapter handles asynchronous requests, streaming UI, cancellation, bounded retries, token/
cost limits and offline/provider errors. Start with Claude if desired while keeping requests,
tools and artifacts provider-independent. Model latency never pauses simulation or becomes
simulation time. A completed request supplies candidate bytes; the existing staging pipeline
chooses the commit tick. Discard responses for stale session/base revisions unless rebased
and tested explicitly.

Credentials belong to the native host's appropriate credential storage or an explicit
session-only path, never `plugin.json`, Lua globals, tool transcripts, screenshots or exported
bundles. Browser support must choose session-only direct access where feasible or a trusted
relay; do not bake a secret into the published Wasm/site. Provider-specific authentication,
SDK, billing and browser/CORS support are implementation-time checks, not assumed facts here.

The model receives capability/schema docs, selected state, logs and artifacts. Tool access is
limited to the chosen plugin workspace and host operations, with no arbitrary shell, raw
engine memory or secret reads. Treat plugin/log text as untrusted input to the assistant, not
instructions that grant more tools. Keep a visible change/test history, cancellation, undo
and an easy “return to last working revision.” If the requested feature needs an unavailable
native primitive, explain the gap and produce a concrete C work item or external worktree/PR
proposal instead of pretending the Lua API can do it.

## Location-life track (proposed 2026-10-30)

**Status:** L1.1–L4.3 implemented for native solo use, including the `harbor-life` package and
the `scene.define` grant; L5–L8 are planned. Timers can schedule objective work; they do not grant
control over boats, birds, lights or other scene actors. The main queue now follows the
[plugins-by-default plan](PLUGIN_DEFAULT_PLAN.md). This is a separate capability track, not a
renumbering of P0–P6 or authorization to implement all slices.

**Intended result:** a location plugin can dress and populate a marina, run harbor traffic
and ferries, put a tram on a visible bridge, arrange wildlife, and set an evening lighting
schedule. Lua supplies bounded definitions and schedules; native owners generate meshes,
evaluate motion/reactions, maintain contacts and render one shared scene snapshot. Prefer
editable descriptors before introducing callbacks. A useful first example is a small marina
with a departing ferry and nearby gulls, followed by an evening variant when lighting exists.

Planning update, 2026-10-30: the [execution guide](#execution-guide-for-a-smaller-implementation-model)
breaks L1–L8 into individually scoped tasks, with proposed starting decisions, source owners,
failure cases and acceptance evidence. **L1.1–L4.3 are complete; the first playable harbor gate is
implemented and awaits human review.** `scene.define` installs and stages the two-berth harbor-life
layout, its small pontoon and gangway, one authored passing craft, a retained ferry with an edited
dwell, the retained schooner/lift dependency and one authored flock with timed feeding/alarm cues.
Next in this track is L5.1 shared fixed daylight/dusk; L5–L8 remain later work.

### Coverage and delivery slices

| Slice | Native foundation today | Planned script support and missing native work | Reviewable completion |
| --- | --- | --- | --- |
| L1 — scene definition and ownership | Shared explicit-time `WaterfrontSceneFrame`; P4 schema, validation, preview and revision tools | Add a caller-owned, location-scoped scene definition with stable IDs, bounded counts and registered recipes. Decide package composition with voyage/race content and duplicate-ID/conflict policy before choosing manifest syntax. Expose inspected actor IDs, supported recipes and budgets through discovery. No general scene grant exists today. | A small scene descriptor validates, installs atomically and restores the compiled scene when disabled; invalid or over-budget edits preserve the active scene. |
| L2 — marina layout and population | Generated boat recipes, seven fixed berths, pontoons/quay/gangway fittings, shared wave poses and moored-hull contacts in `waterfront_harbor` | Author berth positions/headings and boat recipes, then bounded pontoon/jetty/attachment layouts. Validate water depth/shore placement as supported, spacing, access lanes and contact/camera clearance. New structures need matching native geometry/contact support. Terrain excavation, shoreline editing and arbitrary mesh import are separate work. | An authored two-berth marina with a clear entrance; moving/removing a berth updates visible boats, ropes, camera clearance and contacts together. Separate static layout from later occupancy/arrival scheduling. |
| L3 — harbor traffic and ferry schedules | Four river craft in `waterfront_traffic`; upstream ferries and schooner in `waterfront_upstream` use a 1,200-second repeating timetable, pier dwell/departure and a coordinated Hisingsbron lift | Replace fixed actor/route/timetable inputs with validated native-owned definitions. Author supported craft, routes, stops, dwell times, departure offsets and service periods; retain native motion evaluation. Bind ferry/bridge clearance coordination explicitly. Ordinary traffic trails are visual; physical wakes require a separately supported bounded recipe. No traffic avoidance AI, passenger/boarding or real transit feed is implied. | Change one ferry's stop/dwell/departure schedule and add a small craft route; inspect both at identical times, including stop, loop and bridge-lift boundaries. Verify shore, berth and pairwise route clearance and shared visible/contact poses. |
| L4 — gulls and wildlife orchestration | 32 reactive gulls in four compiled groups; deterministic circling, feeding/alarm cues and boat avoidance | Author flock region, count, supported flight recipe and cue schedule within native limits. Add bounded native commands only if a playable event needs them. Perching, habitat/terrain clearance and persistent animal behavior need native additions. Seals are L8, not a gull recipe with a new name. | Move a flock and schedule a feeding/alarm cue; direct/backward seeks and nearby boat reactions remain repeatable, with no bird clipping accepted merely because validation passed. |
| L5 — daylight and dusk | Daytime scene and title-only dusk preview; [sunset plan](SUNSET_PLAN.md) proposes shared gameplay lighting | Implement that plan's lighting frame, CPU/GPU/cache agreement and reviewed fixed presets first. Then expose approved preset/phase and bounded transitions at explicit presentation times. Keep presentation phase separate from physics, weather and race clocks. A full astronomical day/night simulation is outside the first slice. | A plugin selects fixed dusk and a repeatable afternoon-to-dusk transition; sky, scenery, water/reflections and navigation readability agree in both renderers. |
| L6 — building and street lighting | Shared lamp/car placement and renderer lighting machinery; city room activity remains pending | Add stable building/window-group and street-lamp identities, bounded occupancy/on/off/intensity recipes and schedules. Implement native window emission/local-light selection and budgets where absent; distinguish glowing windows/lenses from lights illuminating nearby surfaces. Coordinate defaults with L5 without requiring per-window Lua updates. | A street group switches on and sparse occupied rooms change at authored times, remaining attached to buildings under camera motion and consistent across render passes. Daytime overrides and abrupt reloads have explicit behavior. |
| L7 — trams visible from the water | Shared actor snapshots and road cars are reusable foundations; no tram actor, track route or script API exists | Add a native tram silhouette/articulated recipe as needed, authored tracks with deck elevation, stops, speeds and bounded scheduling. Use bridge geometry/clearance and lift closures; a timetable alone must not send a tram across a raised span. This is authored transit motion, not a road/rail navigation simulation. | One readable tram crosses a selected visible bridge; inspect water-level views, supports/occlusion, stop/loop continuity and bridge closure behavior before exposing the recipe to plugins. |
| L8 — seals and other new animals | No seal model, motion or behavior adapter exists | Build one native animal recipe, bounded habitat/haul-out and surfacing/diving behavior, water/shore placement and agreed contact policy. Then expose species-specific definitions/cues using L4's ownership pattern. Do not promise an arbitrary animal API. | One seal encounter reads clearly from the boat at native resolution; native behavior/placement review precedes script control. |

L1 is the first implementation task **when this track is selected**, followed by L2's small
marina descriptor. L3 and L4 build on L1 and existing actors; L5 can progress independently
in the sunset owner, and L6 follows the shared lighting work. L7/L8 require new native actor
work and are later slices rather than blockers for marina/traffic authoring. These labels
organize plugin integration; they do not reorder the living-city plan's people/cyclist queue.

### Execution guide for a smaller implementation model

All tasks below are **planned**, not implemented. Select one numbered task per worktree/PR;
complete its tests and documentation before advancing. Read this section, the current
[API](PLUGIN_API.md), and the named native owner first. Follow existing descriptor, staging
and test patterns rather than introducing a second plugin host. File names below are entry
points, not permission to refactor every caller. If a prerequisite is absent, finish that
task first; do not mock a missing primitive in Lua or advertise an unsupported capability.

**First playable target — `plugins/harbor-life` (proposed package):** a native solo `outer`
voyage with two occupied berths, an open approach lane, one passing small craft, an existing
ferry with a visible dwell/departure, and one nearby flock. Keep the existing voyage playable.
Use the same saved camera bookmarks and explicit times for comparisons. Daytime is sufficient
for this target; dusk, illuminated rooms, trams and seals are later additions. Success means
the player can approach, watch activity, and leave without an invisible collision or an
activity disappearing on reload. Numerical validation and human judgment of liveliness are
separate gates.

Phase the example's ferry departure and gull cue into its first 60 simulated seconds so
existing pinned capture limits can show them. Full 1,200-second timetable coverage belongs
in native direct-time validators; do not raise the current 60-second capture or 72,000-tick
simulation-preview limits merely to inspect one later schedule boundary.

Recommended order: **L1.1–L1.4 → L2.1–L2.3 → L3.1–L3.4 → L4.1–L4.2 → L4.3**.
L5 follows its native sunset prerequisites; L6 follows L5. L7 and L8 are independent native
extensions after the first playable target. P5 remains the main plugin queue's next task;
selecting an L task does not authorize room integration or the remaining L tasks.

#### Starting decisions and implementation boundaries

Use these defaults to avoid reopening architecture in each task. L1.1 must check them against
actual owners and document any necessary deviation before implementation:

- Extend the existing single `kind = "voyage"`, `location = "outer"` package with an optional
  top-level `scene` descriptor and proposed `scene.define` grant. Keep `api = 1` only if the
  extension is additive; old packages retain identical behavior and old hosts reject the new
  grant. Do not add a new package kind, a second simultaneous package, dependencies, or a
  scene callback API. Existing objectives/timers may coexist but receive no actor commands.
  `scene.define` and the fields below are design targets, **not today's API**.
- One active package owns the override. Absent `scene` means all compiled defaults. Within
  `scene`, an absent subsystem uses its compiled defaults; a present subsystem replaces its
  entire supported collection, and an empty collection disables it where safe. Reject an
  override of an unsupported subsystem. Keep the large docked ferry and bridge dependencies
  compiled until their adapter can own the complete dependency group. No merging by array
  index, patching global arrays, or simultaneous compiled/authored copies.
- Stable authored IDs use the existing shelter-ID spelling/length rule. Require uniqueness
  within the scene, including nested resources; qualify external identity by package ID.
  Reordering arrays must not change identity. Discovery exposes compiled actors as read-only
  references until the corresponding subsystem is authorable. Registered recipe IDs select
  existing native geometry; Lua cannot supply mesh vertices or renderer transforms.
- Store positions in simulation metres (`x = -east`, `z = north`) and headings in radians,
  matching the voyage API. Convert once at the native adapter boundary. Explicitly name
  elevation relative to still water or terrain; never infer it from a `Vector3.y`. Reject
  non-finite, unknown, duplicate, out-of-range and wrong-location values with field paths.
- Begin with current capacities: at most seven authored moored hulls, eight travelling craft
  including retained default traffic, 32 gulls across at most four flocks, and 32 selected
  scene lamps. These are proposed ceilings, not new support promises. L1.1 records every
  array, mesh, rope, route-point and retained-revision budget; L3.1 adds route/stop bounds,
  L5/L6 add lighting bounds before exposing those fields. Reject overflow rather than
  truncating. New structures, trams and seals receive explicit native budgets in their tasks.
- Scene schedules use session tick origin zero and integer 120 Hz ticks; pauses freeze them,
  simulation speed changes advance them with simulation, restart resets them, and preserving
  Apply/Undo retains the current tick. Define half-open intervals `[start, end)` and positive
  periods; normalize negative preview time with positive modulo. Convert to existing native
  double-time evaluators at one boundary. Presentation phase is a separate value derived
  from that input, never a replacement water/race clock. Avoid stateful cue delivery where
  a pure time envelope suffices.
- Stage copied C definitions and generated resources before commit. A physical layout or
  route change initially requires explicit restart at a validated safe spawn; preserve-only
  Apply refuses it. Cosmetic changes may preserve time only when resource/ID compatibility
  is proven. Undo of physical edits follows the same restart rule. Candidate failure, stale
  pins, allocation failure or unsafe spawn leaves active revision and gameplay untouched.
  Keep active, candidate and Undo storage charged to existing host budgets.
- Headless collision/motion data stays in the native core with no Lua/host dependency;
  renderer meshes and GPU resources stay with their native presentation owners. Never pass
  a `WaterfrontSceneFrame` into physics. Both consume one accepted definition and explicit
  time; gull avoidance must query authored traffic too. Do not change ENet, protocol or
  recording formats in L1–L4. Reject scene packages in rooms, browser imports and recordings
  that cannot represent them; retain existing non-scene workflows.

#### L1 — make a scene revision ownable and inspectable

| Task | Concrete change and entry points | Completion evidence / stop boundary |
| --- | --- | --- |
| L1.1 — freeze the first contract | Inspect `boat_plugin.[ch]`, `boat_plugin_host.[ch]`, `boat_session.h`, `waterfront_scene.[ch]`, `waterfront_harbor.[ch]`, `waterfront_traffic.[ch]` and their CMake library membership. Add the proposed schema, subsystem replacement semantics, ID/recipe registry, numeric/resource limits and ownership/lifetime diagram to the API as explicitly planned. List every draw, contact, camera, gull-avoidance and wake consumer that needs the accepted definition. Select two existing berth recipes and one outer-map approach using compiled water and scenery queries; record coordinates and a camera bookmark, not guessed safe positions. | A reviewer can trace compiled/default → candidate → accepted → Undo → release for all consumers. Record why the proposed capacities fit existing arrays. This PR is contract/fixture planning only; no new grant is advertised as supported. |
| L1.2 — typed definition and strict decoding | Add a small plain C scene definition (proposed `src/boat_scene_definition.[ch]`) and extend `boat_plugin.[ch]`/the Lua decoder. Initially permit only a versioned scene envelope with no authorable subsystem; accept omitted scene as before. Add stable ID/recipe helpers and checked counts; no Lua pointers survive evaluation. Update `tools/plugin_schema.py` and validator fixtures with the planned fields, gated from live support until L1.3. | Old voyage/race/objective fixtures remain identical. Test omitted/empty envelope, missing grant, wrong location, unknown subsystem, duplicate IDs, invalid numbers and capacity+1. Test manifest grant ordering alongside optional objective grants. No live scene installation in this task. |
| L1.3 — revision lifecycle and application binding | Extend host candidate/active/Undo ownership, content identity, inspection and `tools/wake_velocity_plugin.[ch]` binding using L1.1's diagram. Install between ticks, invalidate dependent view caches, and restore compiled defaults on disable/session exit. Keep unsupported subsystem edits rejected. Reuse existing pins, restart policy and retirement rules. | Headless session tests cover valid Apply, stale Apply, invalid candidate, allocation/staging failure, Undo, disable, repeated enter/leave and independent sessions. No dangling borrower after retiring a revision; defaults render identically with an omitted scene. An envelope-only `scene.define` may now be reported with its exact supported subset. |
| L1.4 — close tooling and unsupported paths | Thread scene identity through isolated worker/stage serialization, preview, inspection and CPU capture; extend `tools/plugin_author.py`, `tools/plugin_schema.py` and relevant Python/C validators. Validate worker wire versions/lengths before using new payloads. Explicitly reject scene grants in browser imports, room startup and unsupported replay/recording paths, including objective-bearing packages. | Tool output reports supported subsystems, limits and rejected targets truthfully. Test worker round trip, malformed/truncated payload, pinned capture and browser rejection. Existing browser voyage/objective cases still pass. Update packaged tool metadata/guides where discovery changes; no new browser scene evaluator. |

#### L1.1 result, 2026-10-30

**Implemented:** the frozen contract and first marina site fixture. The
[API](PLUGIN_API.md#location-life-scene-contract-planned-l11) owns the proposed package
composition, `scene` schema, replacement/restart semantics, registry, capacities, consumer and
lifetime inventory. No grant or runtime scene field exists yet; L1.1 is planning evidence only.

- **Checked owners:** `boat_plugin.[ch]` (manifest/decoder/classification), `boat_plugin_host.[ch]`
  (fixed revision slots and pins), `boat_session.h` (borrowed route seam), `waterfront_scene.[ch]`
  (shared frame), `waterfront_harbor.[ch]` (seven compiled hulls, pontoon/gangway/quay, contacts,
  camera), `waterfront_traffic.[ch]`, and CMake membership. `waterfront_harbor` and
  `waterfront_traffic` are in `proteus_boat_core`; `waterfront_scene` is in `proteus_render`.
  A plain `BoatSceneDefinition` therefore belongs in core and may be borrowed by physics, while
  no Lua or host type may enter core.
- **Capacity decision:** 7 berths reuse `WATERFRONT_HARBOR_BOATS` and the compiled hull records.
  Four dock modules cover the three compiled parts plus one spare; L2.3 raises the rope-array
  capacity from the compiled 42 lines to 60 for four authored gangway/rail groups. The two-berth example needs only berth pose/draw, hull/pontoon contact and camera exclusion;
  no new mesh, light, wake or actor pool is introduced by L1–L2.
- **Site fixture:** [`tools/validate_scene_site.c`](../tools/validate_scene_site.c) checks the
  planned site with the compiled `outer` water and native skiff/runabout recipes. It passes with
  two berths at simulation `(13, 167)` and `(13, 173)` heading `−π/2`, a 14 × 2.35 m pontoon at
  `(20, 170)`, a gangway from land at `(34.1, 170)` to the pontoon landing `(21, 170)`, an 8 m
  entrance at `(6, 168)` and an east-facing start/camera bookmark at `(2, 170)`, heading `π/2`. The fixture also
  samples the `x = 6`, `z = 150..176` approach lane with a 2.4 m hull and checks both moored hull
  envelopes. `tools/plugin_anchors.json` gains the matching `outer.marina` bookmark; L2.2 later
  corrected it from `(6, 158)` to `(2, 170)`, heading `π/2`, because the original view faced
  south and did not frame both hulls. Depth remains unverified because the compiled map has no
  bathymetry.
- **Decisions made at this gate:** the scene is additive to the existing voyage package, not a
  new kind or dependency system; absent subsystems use compiled defaults and present ones replace
  their whole collection; one package owns the scene; physical layout edits require the existing
  explicit session restart; the docked ferry and bridge stay compiled; rooms, browser imports and
  recordings reject `scene.define` until their own gates; moored hulls add no wakes and gulls do
  not yet avoid berths.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- `cmake --build build -j` built the new `validate_scene_site` without project warnings.
- `./build/validate_scene_site` printed
  `scene_site berths=2 pontoon=14.0x2.3 gangway=13.1m bookmark=2,170 failures=0` (the bookmark was corrected in L2.2).
- Full `ctest --preset full` passed **311/311 enabled tests** (22.46 s) with local socket access
  and `PROTEUS_GPU_TESTS=OFF`; 13 retired wake-field tests remained disabled. `scene_site` is in
  the quick lane. Changed local links/anchors and `git diff --check` passed. This result does not
  claim a scene grant, a visual change or human acceptance.

**Next in this track: L1.2.** Add the plain C scene definition and strict versioned envelope
decoding without installing a live scene. The later results below remain authoritative for
their own steps.

#### L1.2 result, 2026-10-30

**Implemented:** pointer-free scene storage and strict envelope decoding, with no live install.
[`boat_scene_definition.[ch]`](../src/boat_scene_definition.h) are in `proteus_boat_core`; the
Lua/manifest work is in `boat_plugin.[ch]` and `boat_plugin_lua.[ch]`.

- **Manifest:** voyages accept `scene.define` as the first optional grant, before `objective.hud`
  and `objective.timers`. Reordering, repetition, timers without HUD, and a scene grant on a race
  are rejected at `capabilities[n]`. Existing package grants and fixtures are unchanged.
- **Lua:** `scene` is optional. Omitted means the compiled scene. A present table must be exactly
  `{ version = 1 }`; `marina` and every other field are currently unknown. A scene without the
  grant fails at `scene`; a wrong version fails at `scene.version`. The envelope is copied into
  `BoatPluginVoyage` with a no-pointer `BoatSceneDefinition` and digest.
- **Native definition:** `boat_scene_validate` already enforces the planned 7-berth/4-module
  capacities, unique scene-wide IDs, recipe/mooring/module enums, finite 20 km coordinates,
  entrance width, gangway attachment and ID spelling. `boat_scene_berth_spec` and the recipe name
  helpers are the single native recipe registry. This lets L2.2 expose fields without moving
  ownership or changing the validation rules. `boat_scene_has_content` distinguishes the empty
  envelope from authored layout for later classification.
- **No live scene:** the host loads and copies the envelope, but no session, renderer, contact or
  camera consumer reads it until L1.3. The isolated stage-worker wire still carries only the
  voyage; L1.4 owns its versioned scene payload. Because L1.2 has no authorable content, dropping
  the envelope in that path currently changes no running behavior; this would block L2.2 and is
  recorded in the next-task notes.
- **Discovery at this step:** `tools/plugin_schema.py` advertised the accepted envelope, a
  separate `scene_planned` projection and explicit native constraints; `plugin_author.py`
  reported the grant as accepted but `installed: false`, with no supported subsystem and depth
  unverified. L2.2/L2.3 later moved berths and modules into the live `scene` schema.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Project build is warning-clean. Full `ctest --preset full` passed **312/312 enabled tests**
  (18.75 s) with local socket access; 13 retired wake-field tests remained disabled.
- `validate_boat_plugin --case scene_contract` covers omitted and granted-but-absent envelopes,
  missing grant, malformed tables, unknown `marina`, wrong version, manifest ordering/repetition,
  direct definition validation (duplicates, invalid numbers/recipe, capacity + 1, gangway target),
  digest stability/change and recipe lookup. The reference digest remains `e8509272172cb7c4`.
- Both core/net boundary tests pass with the new core source. No rendering, input, audio or
  asset-loading behavior changed, so no new visual or motion check applies; human acceptance is
  not claimed.

**Next in this track: L1.3.** Install the empty envelope through the existing revision host and
bind `BoatWorld`/`BoatViewState` so later content has a live path, without advertising a marina
subsystem yet. L1.4 must then add the scene envelope to the isolated worker wire before L2.2.

#### L1.3 result, 2026-10-30

**Implemented:** the accepted envelope now has a complete owner-to-borrower path, still with no
authorable marina content. No renderer or contact rule was changed.

- `BoatWorld.scene` and `BoatViewState.scene` are frame-borrowed pointers. `boat_session_view`
  publishes the world's scene; `boat_world_restart` preserves it, so a voyage or session restart
  does not silently fall back to compiled defaults.
- `wake_velocity_plugin` binds the active revision's scene at open, at every host commit
  (rename, Undo, migration, voyage/session restart), and in preview checkpoint save/restore. A
  candidate envelope with no content still classifies as unchanged/renamed, so it never forces a
  restart. `wake_plugin_close` restores the compiled voyage route and scene before host storage
  can be reused; re-entry no longer leaves a dangling borrower.
- Inspection reports `scene present/version/digest/content/subsystems=none`. The isolated
  stage-worker wire still omits the envelope (L1.4); in-process launch and host staging carry it.
- No renderer, contact or camera code consumed the pointer at this step. L2.1 replaced the
  compiled harbor layout input and L2.2 wired contact and the first authored package; both are
  recorded below. An empty envelope consequently rendered and collided exactly as before.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build and full `ctest --preset full`: **313/313 enabled tests** (23.57 s) with
  local socket access; 13 retired wake-field tests remain disabled.
- New `validate_boat_plugin_session --case scene_binding` covers open binding, manifest rename,
  rejected scene version with R2 kept, Undo rebinding, two independent sessions, close restoring
  compiled defaults, and clean re-entry.
- CPU `--time 3` outer captures are byte-identical for compiled, reference-plugin and
  envelope-plugin launches (SHA-256
  `ad4a277fc752c2be1cfd4c446429db11827f06c189b0b8cae6bf0c7d156cebb5`). The inspected frame is
  the unchanged compiled marina. This is a no-regression capture, not human acceptance.

**Next in this track: L1.4.** Carry the scene envelope and its identity through the isolated
worker/stage wire, preview, inspection and capture; reject scene grants in browser imports,
rooms and unsupported replay paths. Do this before L2.2 exposes berths.

#### L1.4 result, 2026-10-30

**Implemented:** the scene envelope is carried by every native authoring path, and browsers,
rooms and incompatible modes reject it explicitly.

- **Worker wire:** `PST3` extends `PST2` with the complete pointer-free scene definition. The
  worker writes and the parent reads version, presence flags, counted berth/module records,
  recipes, positions, headings and gangway attachments. Readers refuse another wire version,
  out-of-range counts, unknown enums and non-finite numbers before decoding. `web/plugin_author.js`
  uses the same magic and refuses older replies.
- **Evidence:** `plugin_author.py validate/preview` output gains a top-level `scene` object
  (`present`, `version`, `digest`, `content`, `subsystems: []`). Preview binds an accepted scene
  to its fixture session. Capture scenarios record `scene_grant` and accept a scene package. The
  native inspection line from L1.3 remains the live-session view.
- **Discovery:** `capabilities` now reports `scene.installed: true`, no supported subsystem,
  planned `marina.berths/modules`, 7/4 capacities and depth unverified. `scene_content` and
  `browser_scene` are listed as unsupported; no scene install is claimed in the browser.
- **Unsupported targets:** the browser bundle constructor rejects `scene.define` before a worker
  starts, and the Emscripten stage worker repeats the rejection authoritatively. `--plugin` is
  already refused with rooms, `--course`, `--replay` and `--ghost`, so those paths cannot load a
  scene package. Objective-bearing scene packages follow the same rejection.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build and full `ctest --preset full`: **313/313 enabled tests** (20.67 s) with
  local socket access; 13 retired wake-field tests remain disabled.
- `validate_boat_plugin_stage` round-trips a scene package through the real child worker and
  checks the digest/version, alongside its existing malformed, oversized, stale, deadline and
  cancellation cases. `test_plugin_author.py` checks worker/preview scene evidence and the
  default absent envelope; the Node contract test rejects a scene bundle and uses `PST3`.
- `plugin_author.py capture build/l14-scene --anchor outer.marina --time 1` returned
  `scene_grant: true`, gameplay digest `e8509272172cb7c4`, a 320×200 PNG and no worker error.
  The inspected frame shows the compiled marina from the pinned approach; no authored berth is
  visible because L2.2 has not exposed one.
- `wake_velocity --plugin build/l14-scene --replay missing.run` exits 1 with the existing
  solo-only refusal before loading a replay. No human acceptance is claimed.

**Next in this track: L2.1.** Parameterize the existing moored hulls so meshes, ropes, bobbing
poses, contacts and camera queries share one caller-owned berth list, preserving compiled
parity. L2.2 then exposes the first two-berth package.

#### L2.1 result, 2026-10-30

**Implemented:** the native harbor can evaluate a caller-owned berth list while the compiled
seven remain the default. No Lua field, renderer binding or package berth is exposed yet.

- `WaterfrontBerth` now owns a stable ID, a registered `recipe`, and a `mooring` rule while
  retaining the existing kind/pose fields. Mesh generation is per recipe; `waterfront_harbor_
  recipe_mesh` selects it. `waterfront_harbor_berth_at` converts a scene berth from simulation
  x/z to renderer X/Z once.
- `waterfront_harbor_evaluate_scene`, `waterfront_harbor_contact_height_scene` and
  `waterfront_harbor_camera_clear_scene` select the compiled list for NULL (or no authored
  berths) and an authored present list otherwise. The old functions are wrappers with unchanged
  behavior. The frame carries `berth_count` and a recipe per boat; CPU/GPU draw only that count.
- Body, rope, contact and camera iteration all use the same selected list. The compiled hulls
  are never appended when an authored list is present, so moving/removing a berth cannot leave
  an invisible compiled contact. Authored pontoon moorings use the compiled pontoon's cleat
  region; authored quay mooring and authored dock modules are deferred to L2.3.
- Renderers and physics do not yet receive `BoatWorld.scene` in this step. L2.2 wires that
  binding and the Lua decoder; L2.1 proves the native list and its consumers in isolation.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build and full `ctest --preset full`: **313/313 enabled tests** (22.73 s) with
  local socket access; 13 retired wake-field tests remain disabled.
- `validate_harbor` adds default parity (`evaluate(NULL)` versus compiled), zero/two/seven
  berths, independent evaluations, reordered IDs, move/remove of one berth, per-recipe meshes,
  rope counts, contact and camera exclusion, and the no-invisible-compiled-hull checks. The
  default frame is still byte-equal to the compiled evaluation.
- CPU `--time 3` outer captures for compiled and reference-plugin launches remain byte-identical
  (SHA-256 `ad4a277fc752c2be1cfd4c446429db11827f06c189b0b8cae6bf0c7d156cebb5`). No authored
  berth is rendered yet; GPU path is unchanged by the recipe index and was not visually rerun.

**Next in this track: L2.2.** Decode `scene.marina.berths`, wire the accepted scene into
`BoatWorld`/both renderers, and add the two-berth `plugins/harbor-life` package at the L1.1 site.

#### L2.2 result, 2026-10-30

**Implemented:** bounded berth/entrance decoding, map validation, the first `plugins/harbor-life`
package, and live scene consumption by physics and both renderers. The compiled pontoon,
gangway, quay, ferry and bridge remain independent.

- `scene.marina` accepted strict `entrance` and `berths` records at this step; `modules` was
  still rejected as an unknown field and L2.3 supersedes that. A present berth list replaces only
  the compiled hull collection; an empty list disables it. The decoder copies recipes, mooring rules, source x/z and headings before Lua
  closes.
- `boat_plugin_check_location` checks each hull plus the native 0.45 m lateral/0.9 m longitudinal
  excursion against the compiled map with 0.5 m water margin, pairwise hull clearance, the
  declared entrance, the location spawn and every voyage recovery shelter. Depth is reported
  unverified; no bathymetry is invented.
- `BoatWorld.scene` now reaches hull contact and combat. `BoatViewState.scene` reaches
  `boat_scene_frame`, the direct CPU harbor pass and therefore the GPU frame. The selected
  berths supply pose, mesh recipe, ropes, contact and camera data from the same list.
- `plugins/harbor-life` keeps the Långedrag voyage and adds the two berths at `(13, 167)` and
  `(13, 173)`, entrance `(6, 168)` width 8 m. The pinned `outer.marina` bookmark was corrected
  to `(2, 170)`, heading `π/2`: heading 0 faces south, and the original `(6, 158)` view did not
  frame both hulls. The approach-lane fixture still samples `x = 6`, `z = 150..176`.
- A berth/entrance edit classifies as an explicit session restart; the panel now says
  `THE MARINA LAYOUT` rather than claiming the exposed water changed. Undo reapplies the whole
  previous layout. An unsafe candidate preserves active revision and scene.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build and full `ctest --preset full`: **315/315 enabled tests** (16.69 s) with
  local socket access; 13 retired wake-field tests remain disabled.
- `validate_boat_plugin --case scene_berths` checks the package fields/digest and rejects unknown
  modules, missing entrance, duplicate IDs, unknown recipes/mooring, a berth ashore, overlapping
  berths, a blocked entrance, a blocked spawn and capacity + 1. `--case harbor_life_scene` checks
  live install, F7 restart, Undo and unsafe-candidate preservation.
- `validate_harbor` adds a deterministic approach along the lane and two eastward impacts that
  stop before each moored hull's centre, using the real `BoatWorld.scene` contact path.
- CPU captures at `outer.marina` before/after moving the runabout 6 m are visually inspected:
  both hulls and their ropes are visible, and only the runabout moves. SHA-256 changed from
  `5a5996e5…75f9974` to `e03963dd…0f0c789a`. `validate_scene_site` prints
  `bookmark=2,170 failures=0`.
- Node contracts and the Python worker/authoring tests pass, including `harbor-life` discovery,
  worker evidence (`berths: 2`) and preview. No GPU capture, human driving or human acceptance is
  claimed.

**Next in this track: L2.3.** Extract the compiled pontoon/gangway/quay into bounded registered
modules with attachment/anchor rules, replace the example's compiled pontoon with one small
authored pontoon and gangway, and add matching contact/camera geometry.

#### L2.3 result, 2026-10-30

**Implemented:** registered dock modules with deterministic attachment/anchor rules, shared
draw/contact/camera geometry, and the first small authored pontoon and gangway.

- `scene.marina.modules` accepts 0–4 strict records. Pontoon/small pontoon/quay records carry
  `id`, `kind`, `x`, `z`, `heading`; a gangway carries `id`, `kind`, `anchor`, `attaches` to an
  existing pontoon. Unknown fields, duplicate IDs, missing targets, detached/water anchors,
  spans outside 2–20 m, land pontoons, overlapping solid modules and capacity + 1 reject with
  field paths. A present empty list disables the compiled dock furniture; the ferry and bridge
  remain independent compiled actors.
- `pontoon_small` is a 14 × 2.35 m walkway with three east cleats. The quay recipe is now local
  to its footing so an authored pose can move it; default rendering/contact are unchanged.
  CPU/GPU draw module meshes, the gangway box and only the authored module list.
- The shared frame, contact and camera paths consume authored module poses. Pontoon contact
  includes the long recipe's fingers; quay contact/camera covers its equipment footprint; the
  gangway adds a conservative camera centreline volume. Authored berth ropes attach to the
  nearest authored pontoon's cleat region. The gangway shore end stays on land while its pontoon
  landing follows the wave pose.
- `plugins/harbor-life` now owns the small pontoon at `(20, 170)` and a gangway from land
  `(34.1, 170)` to it. Both berths moor to that pontoon. A module edit follows the same F7
  session-restart/Undo policy as a berth edit.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build and full `ctest --preset full`: **315/315 enabled tests** (22.51 s) with
  local socket access; 13 retired wake-field tests remain disabled.
- `validate_harbor` checks module count/kinds, authored-pontoon rope anchors at two times, a
  fixed gangway shore end with a moving landing, module contact/camera exclusion, and the absence
  of the compiled dock contact when modules are authored. `validate_boat_plugin --case
  scene_berths` adds unknown kind, missing target, water anchor, overlong span, land pontoon,
  overlap and capacity + 1. `harbor_life_scene` edits and Undo-restores the pontoon.
- `validate_boat_plugin_stage` round-trips the harbor-life scene (2 berths, 2 modules) through
  the real child worker. `test_plugin_author.py` expects both authored subsystems; Node contract
  remains green.
- CPU and GPU `outer.marina` captures show the small pontoon, gangway, both hulls and their ropes;
  a shore-side CPU view was inspected for the gangway/pontoon join. The default compiled CPU
  capture remains byte-identical (`ad4a277f…6cebb5`). Human motion/acceptance is not claimed.

**PR stack (unmerged, review in order):**
[#75 L1.1](https://github.com/doomdavve/proteus/pull/75) →
[#76 L1.2](https://github.com/doomdavve/proteus/pull/76) →
[#77 L1.3](https://github.com/doomdavve/proteus/pull/77) →
[#78 L1.4](https://github.com/doomdavve/proteus/pull/78) →
[#79 L2.1](https://github.com/doomdavve/proteus/pull/79) →
[#80 L2.2](https://github.com/doomdavve/proteus/pull/80) →
[#81 L2.3](https://github.com/doomdavve/proteus/pull/81). Each later PR targets its predecessor;
they must land in order. Local verification above uses the complete stacked L2.3 worktree.
No PR is merged and no release/deployment was performed.

User feedback, 2026-10-30: the marina layout works, but F7 restarts the session at the compiled
outer start dock, so the player must travel back to the marina after each physical edit. This is
the current deliberate policy: a berth/module move is not applied under a moving boat and the
first implementation has no safe in-place preview. Future authoring UX should either preserve
the boat when the new layout validates the player's current clearance, or restart at the
verified marina entrance/approach bookmark; a pinned review camera that renders the candidate
without moving the boat is the other useful option. Record human acceptance of the edit loop as
pending rather than treating this convenience gap as a failed scene.

#### L3.1 result, 2026-10-30

**Implemented:** caller-owned travelling-craft routes and timetables, with compiled parity and no
Lua field yet. New core files [`waterfront_route.[ch]`](../src/waterfront_route.h) own eight
bounded actors (four river circuits and four outer upstream vessels), straight/arc motion, stops,
dwell arithmetic, bridge crossings and lift evaluation. `waterfront_traffic.[ch]` and
`waterfront_upstream.[ch]` retain their wrappers and now delegate to the definition; `NULL`
selects the compiled defaults.

- **Limits:** 8 actors, 32 corner points and 8 stops per actor. Structural validation rejects
  unknown shape/recipe, duplicate/ill-spelled IDs, non-finite coordinates, zero-length segments,
  corner radii larger than their adjacent segments, unordered/out-of-range stops, inconsistent
  period/travel/dwell arithmetic, malformed ellipses/circuits and bridge actors that stop.
  `sizeof(WaterfrontRouteDefinition)` is 4,232 bytes (384 bytes of points and 64 bytes of stops).
- **Parity:** default poses, velocities, periods, contact hulls, visual trails and bridge lift
  match the previous compiled evaluator, including negative time, lap wrap and the 1,200 s
  timetable. Default upstream stops remain `{1,4,8}` / `{0,3}`. The definition is pure-time;
  route motion is cached only by the definition digest, never by accumulated simulation state.
- **Bridge:** the compiled schooner remains the sole default bridge actor. Lift crossings are
  derived from the route rather than a second timetable, so later authored tall vessels can join
  the same dependency group. L3.4 adds the full authored-window and overlap rules.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build. Full `ctest --preset full` passed **315/315 enabled tests** (23.22 s)
  with local socket access; 13 retired wake-field tests remain disabled.
- `validate_traffic` now checks default route validation, all eight actors' periods/motion
  against the wrappers at negative time, boundaries and 30 days, direct versus stepped seeks,
  upstream stops, lift parity across the full timetable, frame/wake parity, one custom closed
  loop and structural failures (duplicate IDs, capacity + 1, overlong corner radius,
  discontinuous period, bad stop, bridge actor without a crossing).
- `boat_core_boundary` and `boat_net_boundary` pass with the new core files; no renderer, Lua,
  host or asset path consumes the definition yet. No visual or human acceptance is claimed.

#### L3.2 result, 2026-10-30

**Implemented:** one authored small-craft route in `plugins/harbor-life`, full-collection
traffic replacement, explicit retained compiled actors, and map/schedule corridor validation.
No physical traffic wake was added.

- **Schema:** `scene.traffic.vessels` is a dense 0–8 list. A present list replaces all travelling
  craft; an empty list disables them. An entry either retains a compiled actor
  (`retain = "river_runabout"`) or selects a registered `route`/`recipe` with positive `speed`,
  `phase` and `period`. Eight route presets cover the compiled actors plus `marina_pass`, a
  4 m/s loop north of the marina. IDs are unique, non-finite and over-budget values reject with
  `scene.traffic.vessels[n]` paths, and an authored craft may not reuse the compiled actor's
  schedule without declaring it as a retain.
- **Safety:** every authored hull is sampled over the 4,160 s combined river timetable at 0.25 s
  with half a step of travel inflating the envelope. It must clear compiled water, the declared
  entrance, authored berths and solid dock modules, other authored craft and every compiled
  vessel. Retained actors keep their compiled route and are only checked against authored traffic.
  This is a conservative bounded sample, not a general path planner or proof for arbitrary speed.
- **Presentation/physics:** the accepted definition reaches the shared scene frame, both
  renderers, player contacts, combat obstacle queries and camera exclusion through one route
  pointer. Frame mesh slots select the registered craft recipe without duplicating geometry.
  Ordinary traffic wakes remain visual; authored routes never enable the race's physical pool.
- **Package:** the passing runabout loops at ~24.85 s (native preset `marina_pass`) and the
  compiled river runabout is retained explicitly. The new `outer.marina-pass` anchor at
  `(0, 205)` faces the loop; the pinned `outer.marina` bookmark still frames the slips.
- **Worker:** the scene wire is now `PST4`, carrying route/timetable and retain identity; the
  response bound rose to 32 KiB. Browser imports continue to reject `scene.define`.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build. Full `ctest --preset full` passed **322/322 enabled tests** (23.73 s) with
  local socket access; 13 retired wake-field tests remain disabled.
- `validate_boat_plugin --case scene_traffic` loads the package, checks the route/timetable and
  retain identity, rejects unknown recipe/route, zero speed, a one-second discontinuous loop and a
  non-retained schedule conflict, and checks native capacity/geometry rejection.
  `validate_boat_plugin_session --case harbor_life_scene` stages a traffic edit, restarts on
  Apply, restores it through Undo and keeps a rejected edit from replacing R1.
  `validate_traffic` checks the shared frame pose/motion, contact and camera identity across
  reorder, removal and empty replacement. `validate_boat_plugin_stage` round-trips the two-craft
  scene through the real `PST4` worker; worker/author tests expect the new traffic evidence.
- CPU captures at `outer.marina` and `outer.marina-pass` show the marina unchanged and the
  passing runabout plus retained river craft on the loop. This is a still-frame inspection, not a
  windowed motion or human acceptance check. No GPU capture was run at this step.

#### L3.3 result, 2026-10-30

**Implemented:** named, ordered ferry stops on registered routes; explicit or shared dwell;
positive repeat-period arithmetic; and the harbor-life ferry's edited Stenpiren departure.

- **Schema:** a retained or authored vessel may carry up to eight `{ id, dwell }` stop overrides.
  IDs must match the registered route's named piers in order (`stenpiren`, `lindholmspiren`,
  `eriksberg` for `alvsnabben`; `stenpiren`, `frihamnen` for `frihamnen`). A positive dwell is the
  stop's departure offset; an omitted dwell shares the remaining period. Mixed explicit/shared
  dwell is supported. Travel uses the existing native smoothstep acceleration/braking profile.
- **Validation:** a period shorter than travel plus explicit dwell rejects with the vessel's
  field path. Stop IDs must be valid and unique. Retain entries may now override stop dwells while
  keeping the compiled route and motion; their map/schedule equivalence is not rechecked against
  the compiled service.
- **Example:** `plugins/harbor-life` retains `alvsnabben`, extends Stenpiren to 35 s and leaves the
  other two stops on the shared remainder of the 1,200 s timetable. Discovery lists the registered
  stop anchors. The passing craft and retained river runabout remain in the package.
- **Boundaries:** departure is still a dwell end, not passenger boarding; there is no transit
  feed, passenger count or per-stop callback. Route motion remains a pure function of time.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build. Full `ctest --preset full` passed **322/322 enabled tests** with local
  socket access; 13 retired wake-field tests remain disabled.
- `validate_traffic` checks every stop interval one tick either side of arrival, dwell end and
  departure, stationary hull contact, acceleration after departure, 1,200 s wrap, identical
  reload at an explicit time, fewer stationary trail segments, a mixed-dwell timetable and a
  too-short period. `validate_boat_plugin --case scene_traffic` checks the package's stop IDs and
  dwell plus wrong-ID and negative-dwell rejection. Session Apply/Undo covers a dwell edit and
  preserves the other stops. The PST4 worker carries stop IDs/dwell.
- `WaterfrontRouteDefinition` storage is now 5,768 bytes (stops 256 bytes). No GPU or windowed
  motion run was needed for this schedule-only change.

#### L3.4 result, 2026-10-30

**Implemented:** the Hisingsbron lift and tall-vessel passages are one validated dependency group.
The harbor-life package retains the compiled schooner, so its passage and the lift remain visible.

- **Group validation:** each bridge actor must cross inside the lift span, carry enough raised
  clearance for its tallest mast, have a period longer than the complete lift cycle, enter only
  after the span is fully raised and leave before it lowers, and avoid overlapping another
  bridge actor's hull-occupancy window. A declared bridge actor whose route never crosses the
  span rejects. A missing bridge actor leaves the lift down; an absent traffic subsystem keeps
  the compiled service and lift.
- **Conflicts:** authored craft are sampled against stationary berths and solid dock modules as
  well as other authored craft. Two retained compiled actors keep their existing relative
  schedules (the compiled upstream service already shares berths). A retained bridge actor's
  changed stop dwell still participates in authored-traffic conflicts.
- **Review tooling:** `harbor_review --view bridge` frames the lift span and upstream service with
  `water.upstream` enabled. It is a presentation-only camera; the compiled traffic data is
  unchanged.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build. Full `ctest --preset full` passed **322/322 enabled tests** (24.50 s) with
  local socket access; 13 retired wake-field tests remain disabled.
- `validate_traffic` checks both default windows, fully raised entry/leave, ramp/hold boundaries,
  span-pose height, hull contact at the crossing, a too-slow passage rejection, low-lift
  clearance rejection, overlapping tall-vessel windows and a bridge actor with no crossing.
  The harbor-life package's retained schooner is checked by package/session/stage tests.
- CPU `harbor_review --view bridge` captures at 300 s (down), 342 s (raised), 382/432 s (raised
  with schooner passing) and 502/542 s (down) were inspected. The long lift timetable is covered
  by direct-time validation, not by raising the 60 s `wake_velocity --time` capture limit. CPU
  review has no GPU reflections/shadows; those remain part of L4.3's GPU inspection.

#### L4.1 result, 2026-10-30

**Implemented:** the four compiled gull groups now live in a caller-owned
[`WaterfrontFlockDefinition`](../src/waterfront_flock.h) with pure-time evaluation. `NULL` selects
the compiled groups, preserving the previous paths, cues and wing motion.

- **Limits:** four flocks, eight birds each, 32 birds total and 16 cue intervals per flock. A
  flock owns its simulation centre, bird count/seed, radius, height, signed turn rate, follow
  actor and optional explicit cues. Validation rejects duplicate/ill-spelled IDs, bad counts,
  non-finite centres, invalid radius/height/rate, unknown follow actors, too many cues and
  malformed intervals.
- **Avoidance:** player avoidance is unchanged. Traffic avoidance now reads the accepted route
  definition and all its actors, so an authored craft can disturb a nearby flock; the compiled
  path keeps the first four river craft. The follow flock uses the accepted actor's motion.
- **Cues:** compiled flocks keep their trigonometric feeding/alarm story. Explicit `[start,
  end)` 120 Hz cue intervals with fade and amplitude already evaluate natively; alarm outranks
  feeding, which outranks circling. L4.2 exposes this through the scene schema.
- **Integration:** `boat_scene_frame` and the CPU preview pass the accepted scene's traffic into
  the flock evaluator. The shared `WaterfrontSceneFrame` still carries the same 32 × 3 transforms
  for every render pass, so there is no new mesh/light/wake pool.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build. Full `ctest --preset full` passed **322/322 enabled tests** (23.99 s) with
  local socket access; 13 retired wake-field tests remain disabled.
- `validate_traffic` compares the new evaluator against an independent copy of the previous gull
  equations at 13 instants (position within 1e-5, rotation 1e-3, scale 1e-5), and checks zero/max
  counts, independent simultaneous definitions, explicit feeding/alarm cues, player and authored
  traffic reactions, forward/backward/direct seeks, continuity and finite bird heights.
  Existing snapshot/seek/player-stimulus checks still pass.
- No Lua field, worker payload or renderer change yet; L4.2 adds the descriptor. No visual or
  human acceptance is claimed, and building/terrain clipping remains a visual review item.

#### L4.2 result, 2026-10-30

**Implemented:** `scene.gulls.flocks` exposes up to four authored flocks, with a registered
`gull` recipe, centre/count/seed/phase, circling region and up to 16 cue intervals per flock.
The harbor-life package replaces the compiled groups with one flock near the marina.

- **Schema:** cue intervals are half-open 120 Hz tick ranges `[start, stop)`, with an optional
  `fade` and 0–1 `amplitude`. Alarm outranks feeding, which outranks circling. No timer, spawn or
  despawn state is created: cues are evaluated from the requested session time. Validation rejects
  unknown recipes, duplicate IDs, bad counts (1–8 per flock, 32 total), non-finite centres,
  invalid radius/height/rate/phase, too many cues, unknown cue kinds and malformed intervals.
- **Classification:** a cue-only edit (timing, fade, amplitude) keeps the session clock and
  applies as a compatible cosmetic revision. A flock count/centre/radius/height/rate edit remains
  a physical layout change and follows the existing F7 restart policy; Undo mirrors it.
- **Example:** `plugins/harbor-life` has one eight-bird `harbor` flock at simulation `(-4, 182)`,
  with a feeding interval at 2–5 s and an alarm climb at 7.5–10 s. It replaces the compiled
  groups because a present list is a full-collection replacement.
- **Worker:** the scene wire is now `PST5` and carries flock fields/cues; the response bound is
  32 KiB. Browser imports still reject `scene.define`.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build. Full `ctest --preset full` passed **322/322 enabled tests** (25.98 s) with
  local socket access; 13 retired wake-field tests remain disabled.
- `validate_boat_plugin --case scene_traffic` checks the package flock/cues and rejects a wrong
  recipe, count + 1, unknown cue kind and an empty interval.
  `validate_boat_plugin_session --case harbor_life_scene` checks a cue-only Apply keeps the tick,
  a flock move holds for restart and Undo restores the old cue. Stage round-trips the flock through
  `PST5`; worker/author evidence reports `flocks: 1`.
- CPU captures at 1/3/8/11 s from the marina approach were inspected: the flock is visible as
  distant silhouettes at 320×200; the alarm interval lifts the group in the numeric checks but a
  still frame does not prove motion clarity. Human acceptance and bird/building clipping review
  remain pending for L4.3.

#### L4.3 result, 2026-10-30

**Implemented:** the first playable harbor-life package now combines both berths, the small
pontoon/gangway, the passing craft, the retained ferry with an edited dwell, the retained
schooner/lift and the authored gull flock/cues. Its README carries editable recipes, pinned
camera/time recipes, worker/live-bridge commands and a human-review checklist.

- **Deterministic fixture:** new [`validate_harbor_life`](../tools/validate_harbor_life.c) loads the
  real package, drives the `x = 6` entrance approach at throttle, idles/turns, exits south, checks
  player clearance on every tick, inspects the combined frame, checks feeding versus alarm height,
  walks one ferry dwell/departure and seeks the 1,200 s service. It also renders the compiled and
  populated scenes at the same camera/time sweep and reports p50/p95, meshes, lights, wakes and
  host memory.
- **Measured on this host (Apple Silicon/macOS, Debug, 320×200 CPU, 180 frames, same camera):**
  compiled p50/p95 **9.095/9.270 ms**, populated **8.972/9.125 ms**, supported-maximum scene
  **10.762/10.935 ms** (repeat runs varied by a few tenths of a millisecond). The populated frame had 2 berths, 2 modules, 4 travelling craft
  (48 traffic wake patches), 1 flock/8 birds, 32 meshes, 37 attached lights and 20 cars; the host
  retained one revision and 746,952 reserved bytes. The maximum scene (7 berths, 4 modules,
  8 craft, 4×8 birds) occupies 7,944 bytes. These are CPU render timings, not GPU or frame-pacing
  acceptance.
- **Worker/live bridge:** `validate_wake_bridge --serve-harbor DIR` hosts the combined package in
  the real mailbox. `live-inspect` reported R1 with `vessels=4 flocks=1`; `live-stage` of a
  cue-only draft reported `R2 ACTIVE, renamed / PROGRESS KEPT`; `live-undo` restored R1. Wall
  times were about **0.07 s inspect** and **1.79 s stage/apply** including isolated worker
  startup. The package also round-trips through `validate_boat_plugin_stage`.
- **Captures:** CPU `outer.marina` (3 s), `outer.marina-pass` (9 s), and bridge review at
  300/342/382/432/502/542 s were produced and inspected. They show the marina layout, passing
  craft/retained traffic and the lift/schooner phases. Gulls read as distant silhouettes at
  320×200; a still capture cannot establish motion clarity.

Verification, 2026-10-30, Apple Silicon/macOS, Debug (`PROTEUS_GPU_TESTS=OFF`):

- Warning-clean build. Full `ctest --preset full` passed **323/323 enabled tests** with local
  socket access; 13 retired wake-field tests remain disabled.
- The GUI/window platform is unavailable in this execution environment (`GLFW: Failed to
  initialize platform`). Therefore the requested GPU captures and windowed motion demo could not
  be run here; CPU captures and the headless worker/live-bridge path were exercised instead.
  This is a verification-environment limitation, not a code failure.

**Human acceptance is pending.** Review should answer whether activity is noticeable from the
boat, the entrance stays clear, and the result feels alive rather than populated; also inspect
bird/building/berth clipping in motion. No human acceptance is inferred from tests.

**Next in this track: L5.1**, the native fixed day/dusk lighting frame required before scripted
lighting. The requested L1.1–L4.3 sequence stops here; do not begin L5 without a new direction.

#### L2 — a small marina before a marina editor

| Task | Concrete change and entry points | Completion evidence / stop boundary |
| --- | --- | --- |
| L2.1 — parameterize existing moored hulls | Add caller-owned berth inputs to `waterfront_harbor.[ch]`; retain compiled wrappers while callers migrate. Start with existing hull recipes, positions/headings and native attachment points. Route generated meshes, bobbing poses, ropes, static hull envelopes and camera queries through the same accepted berth list. Do not add pontoons or arbitrary attachments yet. | `tools/validate_harbor.c` proves default parity, zero/two/seven berths, reordered IDs and independent instances. Move/remove one berth and verify its mesh, ropes, contact and camera exclusion all move/disappear together. No invisible compiled hull remains. |
| L2.2 — expose berths and first package | Add bounded `scene.marina.berths` decoding/validation and discovery; implement the two-berth `plugins/harbor-life` example at L1.1's verified site, using the existing outer voyage. Validate hull footprints plus mooring excursion against water/shore, other berths and declared entrance clearance. Use actual map queries; where depth is unavailable, report depth as unverified rather than inventing a bathymetry test. Reject unsupported structure fields. | Bad shore/overlap/entrance and unsafe restart candidates preserve the old revision. Add a deterministic boat approach/exit fixture proving the lane is usable and both hulls block at visible positions. Capture the same site before/after a berth edit; live restart Apply/Undo restores the whole layout. |
| L2.3 — registered dock modules | Extract the existing pontoon/gangway/quay recipes into bounded supported modules with attachment IDs and explicit anchor/end rules. Add matching contact/camera geometry where missing before exposing a module. Keep the large docked ferry independent. Update site validation and schema with module/rope/mesh bounds; no freeform jetty mesh editor. | One small pontoon and gangway attach correctly at the two-berth site; wave movement preserves shore anchoring and ropes. Inspect water/shore joins, player/camera clearance and both renderer passes. Reject detached, overlapping or over-budget modules. Static occupancy is complete here; arriving/departing moored occupants need a separate future ownership transfer task. |

#### L3 — traffic, stops and a bridge that stays safe

| Task | Concrete change and entry points | Completion evidence / stop boundary |
| --- | --- | --- |
| L3.1 — native route definitions with compiled parity | Extract route segments and timetable inputs from `waterfront_traffic.[ch]` and `waterfront_upstream.[ch]` into caller-owned native data. Preserve the current straight/arc motion, smooth departures, four river craft, upstream service and 1,200-second timetable. Set ceilings of 32 route segments and eight stops per actor initially; validate count/length arithmetic and measure storage before accepting them. Expose no Lua route fields yet. | Extend `tools/validate_traffic.c`: default poses, velocities, periods, contact hulls, visual trails and bridge lift agree at stops, departures, negative time and wrap. Direct seek and stepped evaluation agree. Keep wrappers only until all consumers can select the accepted data. |
| L3.2 — one authored small-craft route | Decode registered vessel recipe, route/segment IDs, speed, phase and period for one craft; native code computes motion and trails. Define full-collection replacement with explicit retained defaults within the eight-actor ceiling. Validate swept hull corridors against shore, marina/access lane and other scheduled craft; reject unsupported intersections rather than implementing avoidance. | The first package adds one visibly passing craft. Tests check segment joins, zero/negative lengths or speed, discontinuous loops, invalid arc radii, phase wrap, capacity+1 and route conflicts. Verify shared draw/contact/camera/wake-source identity after reorder, restart Apply and removal. Physical traffic wakes remain off unless an existing independently budgeted recipe is explicitly selected and tested. |
| L3.3 — ferry stops and service schedule | Add named existing pier anchors, ordered stops, dwell/departure offsets and positive repeat periods. Use the native acceleration/braking profile and reject travel times too short for distance/speed limits. Start with one existing upstream ferry; retain other dependencies explicitly. Update discovery and the example to change one stop's dwell and departure. | Evaluate one tick before/at/after arrival, dwell end, departure and period wrap. Verify continuous position/heading and the intended native velocity profile, stationary dwell hull/contact, visual trail decay at stops and no duplicated ferry after reload. No boarding, passenger counts or real transit feed. |
| L3.4 — coordinated lift and integrated route safety | Bind tall-vessel routes and bridge clearance/lift windows as one validated dependency group. Preserve the compiled schooner/lift when untouched; refuse partial replacement that breaks it. Account for hull height, lift transition time and scheduled occupancy; include stationary berths and ferry stops in conflicts. | Test too-early passage, insufficient lift, overlapping passage windows and malformed references; each rejects atomically. Inspect all lift phase boundaries, reflections/shadows and collider agreement. Demonstrate the first package's small craft plus departing ferry without breaking the existing schooner passage. No tram closure logic yet. |

Route safety must use conservative swept envelopes over segments and scheduled intervals;
checking a few screenshots or sampled points is not proof of clearance. L3.1 records the
supported geometry and clearance tolerances. Where a crossing cannot be proven safe within
those bounded primitives, reject it with actor/segment IDs and the conflicting interval.
Do not let an implementation model silently turn this task into a general path planner.

#### L4 — authored gulls and the first lively-harbor gate

| Task | Concrete change and entry points | Completion evidence / stop boundary |
| --- | --- | --- |
| L4.1 — native flock definitions | Extract the four group anchors, per-bird phase/seed, circling/feeding/alarm envelopes from `waterfront_scene.c` into caller-owned bounded definitions. Keep 32 birds total, at most four flocks and current mesh recipes. Rebind avoidance to the accepted traffic definition and current player input; no hidden time accumulation or global random state. | `tools/validate_waterfront_actors.c` checks compiled parity, zero/max counts, two simultaneous scenes and forward/backward seek with identical explicit player/traffic inputs. Check nearby player and authored craft reactions, velocity/wing continuity and bird/terrain/building clearance at the chosen site. |
| L4.2 — flock descriptor and timed cues | Decode flock ID, center/region, count, recipe, deterministic seed/phase and at most 16 feeding/alarm cue intervals per flock. Define overlap priority (alarm before feeding before circling), bounded amplitude and fade durations in schema. Evaluate envelopes natively at requested ticks; no timer-driven spawn/despawn or new objective commands. | Move the example's flock near the marina and schedule one feeding/alarm event. Test invalid habitat/count, cue overlap/priority, boundary ticks, wrap and repeated seek/reload. Apply/Undo preserves time for compatible cosmetic changes; count/layout changes follow the documented restart classification. Inspect readable flight and actual clipping separately from numeric acceptance. |
| L4.3 — first playable harbor qualification | Combine the two berths, dock module, small craft, scheduled ferry and flock in `plugins/harbor-life`. Add editable README instructions, recipe/anchor discovery, a deterministic approach/idle/departure input fixture, inspection expectations and pinned camera/time recipes. Exercise the actual worker/live bridge path, not just native evaluator calls. | Build/full CTest plus CPU/GPU captures and a windowed motion demo. Watch at least one complete ferry dwell/departure and the gull cue; inspect a full timetable by virtual-time seeks. Record baseline vs populated frame p50/p95, staging/Apply latency, mesh/light/wake counts and retained memory at typical/max counts using the same camera/input. Human review asks whether activity is noticeable from the boat, the entrance stays clear and the result feels alive. Record pending human acceptance explicitly. |

If combined cost exceeds existing frame/storage budgets, reduce supported content or fix the
measured native bottleneck before raising capacities. Do not introduce activation streaming
or a scheduler framework without evidence that this bounded first scene needs it.

#### L5–L8 — later additions, each with a native prerequisite

These tasks are also separate PR-sized steps. Do not bundle their art, renderer and Lua work
into one task. Native visual review comes before publishing the associated recipe.

| Task | Work boundary and owner | Required result |
| --- | --- | --- |
| L5.1 — shared fixed daylight/dusk | Follow [SUNSET_PLAN.md](SUNSET_PLAN.md); move the approved title-only look into a shared gameplay lighting frame with explicit CPU/GPU, sky, water, reflection, shadow and cache inputs. Keep water physics unchanged. | Approved fixed day/dusk views at identical scene/time/camera, including navigation readability. Native result goes in the sunset owner; no Lua grant yet. |
| L5.2 — bounded lighting schedule | Expose registered preset IDs plus at most eight non-overlapping transition intervals, with start/end ticks and explicit interpolation/hold/wrap rules. Add discovery, validation, preview and revision classification. Do not script individual shader values. | First fixed dusk, then an afternoon-to-dusk variant of the harbor package; test boundary/direct/backward seek, pause, speed, Apply/Undo and default restoration. CPU/GPU and water/reflection colors agree throughout the transition. |
| L6.1 — native lamp/window groups | In `waterfront_scene.[ch]`, the renderer owners and [LIVING_CITY_PLAN.md](LIVING_CITY_PLAN.md), create stable read-only building/window-group anchors and registered street-light groups. Implement bounded window emission and local-light selection, using the existing 32-lamp selection ceiling. Record a separate window/emissive-group budget before adding fields. | One sparse occupied building and one street group work under fixed day/dusk, occlusion and camera movement. Verify emissive appearance separately from illumination of nearby surfaces; no Lua schedules yet. |
| L6.2 — scheduled building/street activity | Expose registered group IDs, deterministic occupancy seed, bounded intensity and at most 16 intervals per group. Define precedence: explicit group schedule overrides the preset's automatic lamp state; absent override follows L5. No per-window Lua callbacks. | Harbor evening variant lights the quay and changes sparse occupied rooms. Test unknown buildings, duplicate ownership, limits, daylight override, abrupt reload and Undo; CPU/GPU, selected-light costs and cache invalidation agree. |
| L7.1 — one native tram on a verified deck | In the living-city owner, choose a currently visible bridge/deck with verified geometry and track elevation; implement one readable native tram recipe and pure-time track/stop motion. Begin on a fixed span if lift interlocks are not ready. Record tram/segment/mesh budgets. | Boat-height views prove silhouette, deck alignment, supports/occlusion and stop/loop continuity. Do not promise arbitrary rail networks or new map coverage. |
| L7.2 — closure interlock before lift crossing | Extend L3.4's native bridge occupancy contract to deck traffic. Stop before the span, reserve crossing intervals and resume after lowering; reject mutually impossible vessel/tram timetables. | Boundary tests prove no tram enters/occupies a raised or moving span. Direct seek gives the same stopped/resumed pose without depending on prior frames. |
| L7.3 — expose the proven tram | Publish only the reviewed recipe/track anchors, stops and bounded timetable inputs through the scene schema and tools. Add one optional harbor-package tram. | Invalid deck/closure references reject atomically; captures, windowed motion and restart Apply/Undo pass. No script-owned rail physics, road routing or passenger simulation. |
| L8.1 — native seal encounter | In the living-city/wildlife owner, add one low-resolution seal recipe with bounded water habitat and optional verified haul-out anchor. Define pure-time surfacing/diving and player-proximity inputs; choose cosmetic-only/no hull contact initially. Record animal/mesh/region budgets. | Review from the player's camera: readable surfacing/dive silhouette, no shoreline/terrain clipping, repeatable seeks and bounded avoidance. Native behavior/art approval precedes script exposure. |
| L8.2 — species-specific descriptor | Expose only the seal's approved habitat/anchor, count, phase/seed and cue vocabulary, using L4 ownership and explicit limits. Add one optional encounter to the harbor package. | Reject unsupported species, invalid habitat and excess counts; verify boundary ticks, scene isolation, Apply/Undo/disable and CPU/GPU motion. Broader animals need their own native recipes/tasks. |

#### Per-task handoff checklist

Each implementation PR records the selected task ID, prerequisites actually present, changed
owners, accepted schema/limits, completed evidence, next task and remaining risks in this plan
and the linked native owner. Move a proposed field into the API's implemented contract only
when discovery, validation, staging, live use and unsupported-target rejection agree.

Use `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build -j` and
`ctest --preset full` from the task worktree; `ctest --preset quick` is the iteration lane.
Extend the existing harbor/traffic/actor/plugin validators where they own the behavior; add
new cases for semantic failure modes rather than tests that merely mirror the struct fields.
All schedule tests drive virtual ticks, including one tick either side of each boundary.
For visual/motion changes, record exact package revision, camera, inputs, times, renderer and
platform; retain comparison captures and run a windowed demo. A still capture cannot verify
departures, flight, light transitions or player clearance. Browser support stays rejected
until a separately selected qualification task; native success is not browser acceptance.

### Ownership, schedules and reload contract

- The plugin plan owns the scene descriptor/schedule contract and host integration. The
  [living-city plan](LIVING_CITY_PLAN.md), [harbor boats](HARBOR_BOATS.md),
  [harbor traffic](HARBOR_TRAFFIC.md), [gulls](HARBOR_GULLS.md) and
  [sunset plan](SUNSET_PLAN.md) own their native rendering, motion and visual acceptance.
  Reconcile dated harbor guides with upstream source before implementing schedule adapters:
  old river ellipse descriptions do not describe all current outer-map ferry behavior.
- Schedule against explicit host inputs with documented units, phase/origin and pause/restart
  policy. Periodic routes, timetables and lighting should be directly evaluable at a requested
  time; stateful callbacks need checkpointed state and ordered cues. No wall-clock polling or
  per-actor per-frame Lua loop. Preserve seeks and avoid duplicate departures/cues on reload.
- Use one owned definition and evaluated pose for drawing, reflections, shadows, contact
  envelopes and supported wake sources. Keep actor pools separate from the four player slots;
  bound meshes, lights, birds, route points, wakes and active regions. Measure frame and
  staging costs at the intended populated location, including browser targets when enabled.
- Define commit/restart policy per slice. Display-only changes can preserve progress when
  compatible. Moving a berth, pier or colliding vessel must validate player clearance and
  either stage a safe boundary/reset or refuse the edit. Retire old resources only after
  consumers release them; Apply and Undo restore the whole accepted definition/state.
  Composition cannot silently let two packages own the same berth, schedule or lighting phase.
- Start native solo. Browser capabilities remain explicitly rejected until their descriptor
  and any callback paths are isolated and qualified. Shared-room collision/water changes need
  P5's authority, content and prediction contract; shared visual schedules also need an agreed
  host phase/content identity. Existing pose recordings do not capture these scene revisions.

### Acceptance and next task

For each slice, extend discovery/schema, add a small editable example and diagnostics, and
exercise validation, deterministic preview/seek, pinned capture, live Apply/Undo and cleanup.
Check failed edits, ownership/resource retirement and capacity limits. Run build/full CTest;
inspect representative CPU/GPU stills and a windowed motion/lighting demo where behavior changes.
Record automated checks, manual playback and user acceptance separately in the native owner
and this plan. No scene-API implementation or visual acceptance is claimed by this proposal.

Completed planning task, 2026-10-30: expanded L1–L8 into per-PR tasks for a smaller model,
including the first playable harbor package, proposed composition/capacity/time/reload
defaults, native extraction before Lua exposure, tooling and unsupported-target checks.
Completed L1.1, 2026-10-30: froze the scene contract, consumer/lifetime inventory, capacities
and verified two-berth marina site; the API owns the proposed schema and the fixture passes.
Completed L1.2, 2026-10-30: added pointer-free scene storage, the canonical `scene.define` grant,
envelope decoding and planned-field validation without a live install.
Completed L1.3, 2026-10-30: bound the accepted envelope through the host, session, world and view,
with preview rebinding and compiled restoration on close.
Completed L1.4, 2026-10-30: added the PST3 scene staging wire, worker/preview/capture evidence,
truthful discovery, and browser/room/replay rejection.
Completed L2.1, 2026-10-30: parameterized native moored hulls by recipe and caller-owned berth
list, with default parity, move/remove coverage and no invisible compiled hull.
Completed L2.2, 2026-10-30: exposed two editable berths in `plugins/harbor-life`, wired the
accepted scene into physics and both renderers, and added map/restart validation.
Completed L2.3, 2026-10-30: added registered pontoon/gangway/quay modules, the small authored
pontoon and gangway, and shared pose/contact/camera/rope handling.
Completed L3.1, 2026-10-30: extracted eight native route/timetable actors, compiled-parity motion,
stops, bridge crossings and bounded structural validation into `waterfront_route`.
Completed L3.2, 2026-10-30: exposed full-collection traffic replacement with retained defaults,
added the `marina_pass` craft to `plugins/harbor-life`, and wired the accepted routes through
draw, contact, camera, combat and the PST4 worker wire.
Completed L3.3, 2026-10-30: added named ordered stops, shared/explicit dwell, period arithmetic and
the harbor-life ferry's edited Stenpiren departure.
Completed L3.4, 2026-10-30: bound tall-vessel passages and the Hisingsbron lift into one validated
group, retained the schooner in the example, and added a bridge review camera.
Completed L4.1, 2026-10-30: extracted four bounded gull flocks, pure-time cues and accepted-traffic
avoidance with compiled parity.
Completed L4.2, 2026-10-30: exposed bounded `scene.gulls.flocks`, authored cue intervals and the
marina flock; cue-only edits keep time while flock layout follows restart.
Completed L4.3, 2026-10-30: combined the package into the first playable harbor qualification,
added the deterministic approach/idle/departure fixture and measurements, exercised the package
through the isolated worker and headless live-bridge mailbox, and inspected CPU captures.
Next task for this track: **L5.1**, shared fixed daylight/dusk. The requested L1.1–L4.3 sequence
stops here.
Known limits: authored route geometry is selected from registered presets; no freeform route
editor, freeform module mesh editor or physical traffic wake recipe; no boarding/passenger
simulation; no tram closure interlock; depth is unverified; GPU/windowed motion and bird/building
clipping review could not run in this environment; and no tram/seal recipes, gameplay dusk,
building activity or human scene acceptance are implemented or claimed.

Execution-guide verification, 2026-10-30, Apple Silicon/macOS, documentation-only: Debug
configure reused the pinned raylib/Lua/ENet source trees; build passed with no project
warnings (one upstream `stb_vorbis.c` warning). Full `ctest --preset full` passed **310/310
enabled tests** with local socket access, 13 retired wake-field tests disabled and GPU CTests
off. The first sandboxed run failed ten socket-dependent tests; the complete socket-enabled
rerun passed. All 267 local links/anchors in this plan and the documentation index resolve;
`git diff --check` passed. No audiovisual run was needed for this documentation-only update.
No new runtime, browser qualification or human acceptance is claimed.

Prior planning verification, 2026-10-30, Apple Silicon/macOS, documentation-only: Debug build and full
`ctest --preset full` passed on the unchanged implementation baseline `1b74a25` in the main
checkout (309 enabled tests, 13 retired field cases disabled, `PROTEUS_GPU_TESTS=OFF`; local
socket access enabled). Added local links/anchors and `git diff --check` passed. No new runtime
implementation, audiovisual run or user acceptance is claimed.

Merge reconciliation, 2026-10-30: incorporated main `9ab9f3a`, preserving implemented P4.9
browser objectives and P5 as the next main-queue task alongside this proposed track. In the
merged worktree, Debug build and full CTest passed (310 enabled tests, 13 disabled, GPU tests
off, local socket access enabled); all 29 added local links/anchors and `git diff --check`
passed. No new audiovisual verification was needed for this documentation-only resolution.

## Longer-term creative roadmap

Grow capabilities when a playable example needs them, preserving the existing owning plans:

- **Voyage episodes:** optional deliveries, rescue encounters, visiting landmarks and choosing
  sheltered routes, built over the adventure's real map expansion and weather work. Cargo,
  rescue actors and more destinations need native resource/rule support before Lua orchestration.
- **Living waterfront:** the [location-life track](#location-life-track-proposed-2026-10-30)
  now specifies marina layout/population, harbor traffic and ferry timetables, gulls, trams,
  seals and building/street lighting. Pedestrians/cyclists remain native city work before
  script orchestration. Share scene snapshots and bounded activation by gameplay region.
- **Evening journeys:** scripted fixed lighting or gradual dusk, weather events and restrained
  audio cues after the sunset/weather owners provide consistent physical/presentation recipes.
  Presentation phase stays distinct from water and race clocks.
- **New competitions:** authored slalom, time trials, team objectives and supported combat
  variants using C course/match primitives. New projectiles, collision rules or boat classes
  remain native features with deterministic and network tests.
- **Creative collaboration:** save/share a small versioned scenario bundle, branch a preview,
  compare two candidates at identical inputs/time, and invite an agent to iterate against an
  explicit goal. Later support host-controlled multiplayer authoring and curated distribution.
  Package trust, signing, dependencies and an online catalog should wait for demonstrated need.

The most valuable early improvement is a short, observable edit/test/reload cycle. A large
framework, universal mod compatibility, open-ended agent autonomy and comprehensive world
generation are not prerequisites for that loop.

## Decisions to resolve at their implementation gates

| Gate | Decision and recommended starting point |
| --- | --- |
| P0 | **Resolved 2026-10-28** ([P0 result](#p0-result-2026-10-28)): Lua 5.4.9 via checksummed `FetchContent`, VM plus base/string/table/math only; strict jsmn manifest with host-fixed values; one text entry; native and Wasm/Node feasibility recorded; license packaging deferred to P1, when Lua first ships. |
| P0/P1 | Initial budgets and staging responsiveness: measure the descriptor case, permit a visible authoring pause first, then decide whether background compilation is justified. Do not call an instruction limit a compiler deadline. |
| P1 | Reload UX and distribution: choose a key/menu action and opt-in launch selection, preserve-only default with explicit restart policy for incompatible edits; retain compiled defaults until parity and authoring review. |
| P2/P3 | Supported rule extension depth: keep voyage/race/match engines in C and add orchestration first. Move no mature rule engine merely to make a larger Lua example. |
| P3 | P3.1 resolves state numbers to signed 64-bit integers; P3.2 supplies lossless voyage events, source ownership and a live optional objective/HUD. P3.3a adds deterministic tick timers and pending-timer reload policy. P3.3b adds live schema migration, resource retirement/composition and native inspection/seek. P3.4 adds native storage/budget/latency qualification and retains synchronous trusted edits; imported/background isolation is a P4 gate. |
| P4 | Imported-code trust/isolation, minimal landmark names and browser editing: begin with local authored examples and a few curated anchors; require the appropriate staging isolation before broader sharing. |
| P5 | Room content distribution, recording format and physical command prediction: preinstall matching packages and reload between rounds first; define version/size/late-join behavior before public scripted rooms. |
| P6 | Provider/key UX, native versus browser agent support and autonomy policy: use the same host tools and selected workspace; keep provider/secret work entirely outside plugins. |

## Verification and known limits of this planning change (history)

Reviewed on 2026-10-28: the [documentation index](README.md), the plans/guides and source
interfaces linked in the inventory, CMake library boundaries, the application/drive tick path,
voyage/session ownership, race/match outputs, fleet/prediction checkpoints, recording types,
and browser packaging. The primary Lua embedding references above were checked; the exact
runtime version is intentionally undecided. Local links and document consistency are checked
as part of handoff, with no source implementation implied by the proposed examples.

Existing-C baseline verification, 2026-10-28, macOS 26.6.2 arm64: Debug configure reused the
existing pinned raylib/ENet source checkouts; `cmake --build build -j` succeeded without
project warnings (one bundled `stb_vorbis` tautological-pointer warning). With local socket
access, `ctest --preset full` passed **230/230 active tests**, with **13 retired wake-field
tests disabled** and `PROTEUS_GPU_TESTS=OFF`. The first sandbox-only run failed ten
socket-dependent cases; the full unrestricted rerun passed. Logs are local artifacts at
`/private/tmp/proteus-plugin-plan-configure.log`,
`/private/tmp/proteus-plugin-plan-build.log` and
`/private/tmp/proteus-plugin-plan-ctest-unrestricted.log`.

This verifies the existing C baseline for a documentation-only change. There are no Lua
tests, implemented bindings, plugin performance results, browser-plugin results or in-game
agent results to report. No new audiovisual run was needed or performed; GPU checks and
manual authoring/play acceptance are not claimed. Exact budgets, safe background staging,
native/web behavior equality and migration semantics need implementation evidence at their
gates. Older plan summaries contain dated worktree/next-task language; the inventory links
those owners and current source rather than treating every historical proposal as delivered.
