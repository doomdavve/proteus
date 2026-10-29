# Lua gameplay plugins and live authoring

Status, 2026-10-29: **P0, P1 and P2 implemented.** Native solo `outer` sessions load and
live-edit the Långedrag voyage; the local river trial now loads race descriptors through the
same restricted evaluator and revision host. The compiled race remains the network default.
P3.1 provides atomic objective transactions and P3.2 connects lossless native voyage events
to an opt-in shelter-tour objective with a native HUD row. P3.3a adds deterministic tick timers
and their checkpoint/reload contract. P3.3b adds schema-aware native migration, explicit
resource retirement/composition and a complete compatible solo checkpoint/inspection harness.
There is no in-game agent. The agreed direction
is embedded Lua for gameplay authoring, with C retaining physics, collisions, wake math, rendering and networking.
This document owns the boundary, workflow and staged implementation; [the plugin API](PLUGIN_API.md)
owns the current contract. [All plans](README.md).

Completed task: **P3.3b, live migration, resource retirement/composition and inspection/seek**,
see the [P3.3b result](#p33b-result-2026-10-29). Timers are recorded in the
[P3.3a result](#p33a-result-2026-10-29). P3.2 integration is recorded in the
[P3.2 result](#p32-result-2026-10-29). P2 river authoring remains recorded in its
[P2 result](#p2-result-2026-10-29).
Earlier results remain recorded under [P0](#p0-result-2026-10-28) and
[P1](#p1-result-2026-10-28), including the user's native voyage play and live rename.
**Next task: P3 qualification — retained revision/total host memory accounting, separate
callback/migration budgets and measured staging/background reload deadlines.**
The milestones below are this plan's P0–P6, independent of similarly named foundation tasks.

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
| [Harbor boats](HARBOR_BOATS.md), [traffic](HARBOR_TRAFFIC.md), [`waterfront_harbor.h`](../src/waterfront_harbor.h), [`waterfront_traffic.h`](../src/waterfront_traffic.h) | Generated boat recipes, fixed berths, shared wave poses, mooring/contact envelopes, four travelling vessels on authored routes, and the docked large ferry. Ordinary traffic is kinematic; optional trial traffic wakes use a separate bounded pool. | A route/actor capability must add caller-owned descriptors and keep physical contacts, visible poses and wakes in agreement. Existing fixed functions are not a runtime actor editor. Do not allocate NPCs out of the four player slots or promise unlimited physical wake sources. |
| [Gameplay sunset](SUNSET_PLAN.md), [`waterfront_scene.c`](../src/waterfront_scene.c), [`renderer_gpu.c`](../src/renderer_gpu.c) | Daytime gameplay and a title-only dusk preview. The shared lighting frame and gameplay sunset remain proposed. | Expose fixed lighting presets/phase only after the sunset plan supplies and verifies the native shared frame/cache behavior. A Lua `sunset` name must not imply an implemented sun, window-light or day/night system. |
| [Deathmatch](DEATHMATCH_PLAN.md), [`boat_match.h`](../src/boat_match.h), [`boat_combat.h`](../src/boat_combat.h) | C match attribution, scores/rounds, water cannon, stern wave grenade, torpedoes, damage feedback and rearview presentation. Current protocol is v7. | Later author supported match parameters, objective/scoring variants and announcements. Projectile integration, hit/credit rules, pressure, capsizes and prediction remain C. New weapons need native work; balance and broader browser/platform qualification are unfinished. |
| [Multiplayer](MULTIPLAYER_PLAN.md), [protocol](MULTIPLAYER_PROTOCOL.md), [`boat_net_server.c`](../src/boat_net_server.c), [`boat_prediction.h`](../src/boat_prediction.h) | Four-player authority, ENet/native and optional browser transport, content checks, snapshots, input replay, wake reconciliation and remote views. The server currently initializes the compiled river course. | Authority can eventually own gameplay scripts, but script/config/state hashes and timed commands are not in today's wire contract. M5 impairment/resource/soak work and broader platform qualification remain. Scripted worlds must be explicitly rejected by existing clients until P5. |
| [Boating recordings](BOATING_PLAN.md#first-timed-course), [`boat_replay.h`](../src/boat_replay.h), [`boat_fleet.h`](../src/boat_fleet.h) | Basin runs record sampled poses and course splits for bests, ghosts and playback; fleet checkpoints support future-affecting C state and client input replay. | These are different systems. Neither is a general plugin-state/session recorder. Plugin provenance, commands, migrations and authored HUD playback need a deliberate new format/extension; keep existing recordings readable. |
| [Physics](BOAT_PHYSICS_PLAN.md), [wake rewrite](WAKE_REWRITE_PLAN.md), [arcade implementation](WAKE_ARCADE_IMPLEMENTATION.md), [`boat_wake_objects.h`](../src/boat_wake_objects.h) | Fixed-step physical handling and bounded arcade wake objects, jumps/landings/capsizes, native CPU/GPU water recipes. Physical-field research is retained as history and is disabled in the routine suite. | Expose supported presets and capped source recipes after validation. Do not put per-hull water sampling, wake math or a new physical-field solver in Lua. Preserve accepted feel; wake readability, tuning and platform/fleet costs retain their own gates. |
| [Foundations](BOATING_FOUNDATIONS_PLAN.md), [adventure ownership/tiling ledger](FREE_RIDING_ADVENTURE_FIRST_STEPS.md), [`boat_world.h`](../src/boat_world.h), [`boat_session.h`](../src/boat_session.h) | A 120 Hz integer clock, explicit physical-map resources, session/fleet ownership, view-owned caches and enforced core boundaries. A1/A2 provide important mapped-world work beyond the older foundation ledger. | Add a caller-owned plugin host around these owners. Never point a borrowed native definition at Lua memory. Full foundation workload/renderer qualification and larger-world measurements remain separate work. |
| [HUD](BOAT_HUD.md), [skins](BOAT_SKINS.md), [music](BOATING_PLAN.md#selectable-soundtracks--2026-10-24), [title](TITLE_SCREEN_PLAN.md) | Native marine instruments and shared bitmap UI, voyage/race/combat HUD, selectable XM tracks, Nordic skins and the explorer, title/settings/credits. Explorer selection also chooses a faster handling class in solo free boating. | Start with existing voyage labels, then a small objective/notice/marker vocabulary rendered by C. Later select registered cues and appearance presets. Arbitrary meshes, shader code, audio decoders, title replacement, a palette editor and title music are not initial APIs. Keep skin presentation separate from explicitly authorized handling selection. |
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
disabled and retain a straightforward no-plugin launch. Every implementation handoff runs
the Debug build and full CTest preset, plus the focused checks below; report unavailable GPU,
browser, physical input or human-play evidence explicitly.

P0 → P1 → P2 establishes the two descriptor slices; P3 adds behavior and P4 makes the
authoring tools practical. **P5 and P6 can branch independently from P4:** a solo in-game
agent does not need to wait for multiplayer, prediction or session-recording qualification.
P6 uses the already tested local staging/preview/undo path. Only shared-room agent editing
depends on P5's authority and content contract.

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
exact compatibility and resource contracts. P3 remains open for runtime qualification.

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
  examples and game help were exercised. Windows/Linux
  execution awaits CI; full GPU CTests, browser/rooms, deadline qualification and human
  migration/Undo/recovery checks remain open.

**Next: P3 runtime qualification** — retained revision/total host memory accounting, separate
callback/migration budgets and measured staging/background reload deadlines. Review checkpoint
support deliberately excludes recordings/course playback, other physical boats, combat/radial
and legacy wake fields; immutable resources/directory/handling must outlive checkpoints.
Application camera/window/audio state and external effects are outside the display-free
harness. Broader preview timelines and interactive inspection belong to later authoring work.

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

## Longer-term creative roadmap

Grow capabilities when a playable example needs them, preserving the existing owning plans:

- **Voyage episodes:** optional deliveries, rescue encounters, visiting landmarks and choosing
  sheltered routes, built over the adventure's real map expansion and weather work. Cargo,
  rescue actors and more destinations need native resource/rule support before Lua orchestration.
- **Living waterfront:** ferry timetables, small craft routes, local events and gull reactions;
  later pedestrians/cyclists and building activity once the city plan implements those actors.
  Share scene snapshots and bounded activation by gameplay region, not a script-owned crowd.
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
| P3 | P3.1 resolves state numbers to signed 64-bit integers; P3.2 supplies lossless voyage events, source ownership and a live optional objective/HUD. P3.3a adds deterministic tick timers and pending-timer reload policy. P3.3b adds live schema migration, resource retirement/composition and native inspection/seek. Memory/budget/deadline qualification remains open before P3 completion. |
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
