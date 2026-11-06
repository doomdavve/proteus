# Free ride sandbox: a working harbour

Status, 2026-11-06: **design proposal; no sandbox implementation.** This plan owns free ride's
interactive cargo, player changes to places, Creative/Working Harbour rules, boat ownership and
economy. The [adventure plan](FREE_RIDING_ADVENTURE_PLAN.md) retains geography, weather and the
journey toward Vinga. [Marina authoring](MARINA_AUTHORING_PLAN.md) owns static scene recipes and
scheduled harbour life; [plugins](PLUGIN_PLAN.md) own authoring/runtime contracts;
[room content](ROOM_CONTENT_PLAN.md) owns loading and distribution.

Completed: inspect the current free packages, scene/boat contracts and related plans; propose
the experience, bounded physics and ordered playable slices; incorporate an Astra design review
requested by the user. Next: **S0, a small cargo/crane
handling experiment**, before implementing earnings or progression. These are proposed design
choices, not user acceptance of a playable result. This request covers planning only.

## The experience

Give the boat a purpose and let the player leave useful changes behind. Dock beside a small
working quay, swing a pallet onto a deck boat, lash it down, cross the river, and unload the
parts that complete a pontoon. Next time, that pontoon is a usable place to moor. A boat that
was previously just scenery might need a tow; an exposed delivery becomes easier with a
different hull or a better loading arrangement. Cruising, jumping wakes and exploring remain
reasons to take a boat out even when no job is accepted.

The proposed loop is:

```text
Explore and spot an opportunity
        -> load / tow / help
        -> make an interesting boat journey
        -> deliver and change a place
        -> earn money, equipment or access to another kind of work
        -> choose the next outing
```

The first question is whether manipulating a load and carrying it are enjoyable. A payout
cannot rescue dull handling. Keep the playful Wake Velocity movement, readable low-resolution
presentation, compact C systems and recognizable Gothenburg setting. Make careful work and
reckless experimentation both entertaining choices.

Near-term success: **a player can make a small plan, try it with the boat and crane, get a
surprising but understandable result, and quickly try another idea.** Voluntary experimentation
is stronger evidence than repeating a lift only to overcome awkward controls.

## Two ways to use the same world

| | Creative | Working Harbour (working name for survival/progression) |
| --- | --- | --- |
| Boats and tools | Available immediately; spawn, borrow and swap supported boats | Start with a useful small boat and access to a shared quay crane; buy, rent or earn equipment |
| Cargo | Spawn supported loads and experiment freely | Carry owned goods, accepted jobs or salvage |
| Changing places | Place, move, remove and restore supported harbour pieces | Supply materials and earn access to improvements at suitable sites |
| Fuel | Unlimited by default; consumption can be enabled for experiments | Pay for fuel once the basic work loop is established |
| Failure | Recover objects, undo edits and reset the sandbox | Recover boat and cargo; bound losses and always retain a route back to useful work |
| Goals | Player chosen projects and experiments | Optional jobs, local projects and an eventual expedition boat/journey |

Creative and Working Harbour use the same cranes, ropes, cargo and placement rules. Creative
eventually exposes every supported interaction without earning gates; it does not promise
arbitrary terrain destruction, custom mesh import or changes to every existing landmark.
Prioritize moving cargo, arranging a working waterfront and changing usable infrastructure.
Offer projects, rather than requiring a first-person avatar, hunger, crafting trees or combat.

Keep today's unrestricted cruising available. A new progression save must not suddenly lock an
existing player's Explorer or make old free ride consume fuel. Start new Working Harbour worlds
explicitly. A Creative copy can be made from a progression world; sandbox spawning/undo must
not flow back into its economy. No proposed menu label is an implemented control.

## First outing: one load that matters

Use the existing `outer` harbour. Survey two nearby, sheltered working sites with clear
approaches, enough crane reach and no obstruction of ferries or marina entrances. Reuse the
marina's fixed/floating structure recipes, adding a modest service crane rather than turning
the huge Eriksberg landmark into a precision loading machine. Exact sites need a collision and
camera survey; do not assume a decorative quay is already a usable work surface.

The player borrows a compact flat-deck workboat, starting the first session already moored
beside its source load. At later visits they secure the boat,
enter a crane view, select a pallet's generous lifting target, slew/luff/hoist it onto the
deck, then explicitly lash it. They switch back to the helm for a short crossing. At the
destination a second crane unloads the pallet into a marked receiving area. Unloading the
supplies completes a small prepared landing with a working mooring point. Delivery commits
only after the hook is detached and the load is settled; entering a destination circle with
cargo aboard is insufficient.

That landing immediately becomes a recovery/loaner point or a nearer pickup for the next
outing. Demonstrate its usefulness on the following trip. A project completes once; repeatable
deliveries continue paying for ordinary work without rebuilding the same landing. Later jobs
can start near the last destination rather than requiring an empty return every time.

Aim for a **5–8 minute outing**, including handling and a brief water leg; this is an optimistic
playtest target, not a measured duration. Measure active handling separately from waiting,
searching and empty travel. Allow leaving halfway, dropping a load, retrieving it and
trying again. No hard timer or up-front cargo deposit in the introductory job. Include a
nearby reset/recovery action and a clear return to the helm from crane mode.

At game resolution, communicate hook target, deck footprint, legal load, lashing state and
delivery progress through silhouettes, colour plus shape/text, a landing shadow and short
context prompts. Crane view should keep hook and receiving surface visible. Support keyboard,
mouse and the existing touch flow before a public playable release; final bindings follow
the control experiment. Motor/rope/impact sounds and gentle load sway provide useful feedback.

Start with assisted mooring at authored work berths: constrain horizontal drift/yaw while
retaining bounded wave motion. Entering crane mode idles thrust and keeps the boat secured;
the world continues on the same tick clock, with solo pause available immediately. Returning
to the helm stops crane motors but retains a suspended load and its dynamics. Reject departure
while a hook remains connected to the boat or its load, with a clear release prompt. Test
crane-mode exit, premature unmooring and disturbance at the work berth. Physical multi-line
mooring can wait. Ordinary traffic wakes remain visual; initial tests use qualified ambient
water/player wakes and do not add physical ferry wakes.

## Cargo and crane physics

Start with a deliberately small simulation experiment, not a whole harbour of active rigid
bodies. Existing boats have heave/pitch/roll and planar contacts, but `BoatHandling` currently
contains thrust and a hull pointer: cargo mass, a working deck and crane constraints are new
work, not values that can simply be added to a scene definition.
`BoatHull` adjusts contact footprint/yaw inertia while buoyancy still uses speedboat-sized
support locations and shared normalized parameters. A wider visual hull is not automatically
a physically supported workboat; declare the workboat's deck/support and handling approximation.

Proposed approximation:

1. A crane has authored joints, limited motor speeds, reach and load limits. Its hook supports
   one load through a damped distance constraint with visible pendulum motion. Start with one
   rope segment; a rope mesh can suggest detail. Catch targets are generous and attachment is
   an explicit action. Do not require exact hook collisions to select an item.
2. Cargo uses boxes with position, velocity, orientation, simple contact shapes and modest
   restitution. Implement quay/deck contact first, then limited cargo/cargo contact. Qualify
   both a hanging load and transfer onto a moving, wave-posed deck. Restrict stacking until
   contacts are stable; a pile simulator is a separate ambition. For S0 the deck is a prescribed
   moving support: loose/suspended cargo does not feed reaction forces back into the boat.
   Only secured loads affect its authored handling. Do not count hook-supported weight twice;
   detaching cargo preserves its deck-point velocity, including the hull's rotation.
3. Loose cargo can shift or fall. Lash a settled item inside a supported deck footprint to
   attach it to the hull; unlash only at a safe speed. Cargo has one explicit state/owner:
   stored, suspended, loose, secured, floating/lost or delivered. Every transfer is atomic.
4. Secured cargo contributes bounded load and fore/aft/lateral balance to the boat model.
   Start with acceleration/braking and a readable trim/heel effect; tune larger stability
   consequences through play. Reject overloads clearly. Empty boats retain their existing
   handling. A hull without a cargo deck can carry only explicitly supported small loads.
5. A fallen introductory crate floats long enough to retrieve, using the existing water
   query with a few buoyancy samples. Other cargo can later have authored recovery properties.
   Never strand the player waiting for a drifting job object that cannot be found or reset.

Use the existing 120 Hz tick, bounded constraint iterations, speed limits, contact margins and
sleeping for settled objects. Proposed S0 budget: **16 active cargo bodies, one operated crane
and no arbitrary stacks** as a stress ceiling. Prove one load, then a few, before enabling
cargo/cargo contact or raising the budget. Settled distant cargo can retain stored
state; a held or falling load must not freeze because the camera turns away. Include capsize,
reset, hook release and a moving deck in the stability fixtures.

The [Box2D simulation guide](https://box2d.org/documentation/md_simulation.html) is a reference
for fixed stepping, kinematic/dynamic bodies and sleeping. This is our proposed 3D cargo
approximation; that reference does not select Box2D or establish a solver choice. S0 should
compare the smallest viable constraint/contact implementation against a small existing 3D
solver only if measured instability warrants a dependency. Do not replace boat physics.

## Boats, work and progression

Boats should open different play rather than form a single ladder of increasing speed.

| Boat/tool | Useful capability | Tradeoff to make interesting |
| --- | --- | --- |
| Small launch / existing cruising boat | Explore, carry a small supported parcel, visit nearby jobs | Limited load space; convenient and cheap to run |
| Flat-deck workboat | Carry several pallets, arrange weight, deliver building pieces | Slower with a load; requires access to quay cranes |
| Winch/tow fitting | Retrieve floating goods and assist disabled craft | Tow length and tension affect turns and docking |
| Tug or crane-equipped workboat | Move a barge, handle heavier loads or unload away from a crane | Higher cost, larger clearance and reduced agility |
| Explorer / expedition variant | Longer exposed outings, remote service or survey work | Range and weather suitability, not the best cargo carrier |

Introduce one physical new capability at a time. Rental/borrowing lets players try work before
buying a specialist boat. Separate hull/handling, equipment, ownership and appearance explicitly;
today's Explorer is a stronger solo engine selected through a livery. Keep that shortcut in
unrestricted free ride while defining a real boat identity for progression. Selling/trading
must preserve a starter/loaner option and must reject a sale with unresolved cargo or tow state.

Early work can be local deliveries and salvage. Then add one disabled boat and a simple tow
constraint; useful speed/length/tension cues matter more than a many-link rope. Rescue can
initially mean reaching an indicated casualty craft and bringing it to shelter, avoiding a new
swimming/ragdoll system. Later variations include fragile cargo, awkward dimensions, exposed
routes and urgent assistance. Let these combinations arise from shared tools, not separate
mission minigames for every job.

Use short, finite local projects: repair a landing, add a finger pier, install a fuel service
point, or bring equipment to a remote worksite. Supplied objects become persistent usable
infrastructure. Author valid sites and keep protected channel/bridge/traffic corridors clear.
Creative later permits freer arrangement within validated areas; progression earns the same
pieces through jobs/materials. Keep a few places densely interactive before enlarging the map.

The optional long goal is acquiring and fitting out an expedition-capable Explorer, then
making a prepared voyage toward Vinga when that geography is delivered. Give it useful jobs
after purchase. The harbour must already work as an open-ended game on the current map; neither
Vinga nor a final purchase should be necessary to make the first deliveries worthwhile.

## Money, fuel and recovery

Start with one currency, fixed quoted job payouts and posted purchase/rental/fuel prices.
Avoid commodity speculation or a simulated market. A completion can pay money **and** advance
a visible local project, giving the trip both a personal reward and a lasting result.

Add scarcity after a load feels worth carrying. Show estimated fuel needed, fuel available and
expected job income before departure; mark service points on the chart. Consumption depends
on engine effort and boat/load recipe, with deterministic ticks. Fueling happens alongside a
service point at rest; never require balancing a handheld nozzle to resume normal play.

Initial balancing hypotheses for an ordinary completed job: fuel takes roughly **10–20%** of
the payout and a meaningful early purchase takes **3–5 varied outings**. These are deliberately
unverified tuning targets. Include loading time and the return/next job leg in cost estimates.
Do not make every interesting boat expensive to operate or every departure a ledger exercise.

An empty tank, lost crate or capsize must not create a debt spiral. Provide starter fuel at a
home quay or a loaner/assistance path; cancellation returns job state to a known valid origin.
Recovery may forfeit the current payout or charge a capped fee only when affordable. No idle
offline fuel drain, mandatory maintenance timers or permanent loss of the player's only boat.
Display reserve warnings early enough to choose shelter. Judge whether fuel adds route and
load choices; reduce or disable it if players just avoid exploring.

## State, authoring and multiplayer

Keep immutable map/scenery and authored service/cargo/site definitions separate from mutable
world state: cargo poses/ownership, crane state, changed structures, jobs, boat inventory,
fuel and currency. Use stable IDs and bounded caller-owned arrays. Native code validates and
executes commands; Lua supplies supported data. Current `free` packages have no objective or
callback grant, so jobs and interactions require an explicit descriptor/runtime extension.
Existing voyage HUD objectives alone cannot implement cargo or money safely.

Put physical cargo/constraint stepping behind the core boundary. A small data-only sandbox
rules layer can own transactions; the application owns menus, input, file I/O and save commits.
Both renderers consume a shared evaluated view of cargo, hook, ropes and changed structures.
Extend authoring placement checks into runtime validation; free placement must update drawn
geometry, contact envelopes, camera exclusion and navigation clearance together. Plugin reload
and Undo need a stated reconciliation policy before mutable sandbox state is supported.

First deliver solo with explicit session state and reset. Add versioned local save/resume
before persistent progression: content identity, stable IDs, inventory, fuel, transaction IDs
and completed projects. A quit/save request must always finish promptly. Record each item's
last stable support (pad or secured deck); on save during a lift, store a recovery configuration
that returns the same cargo ID to that support and releases the hook on resume. Capsize or
unrecoverable job cargo uses the declared home/source recovery path. Preserve completed
deliveries, project completions and rewards; never rewind the entire outing. A full destination
pad requires moving its occupant or choosing another valid receiving pad, with cancellation
available. Test each recovery path for duplicate/missing items and blocked pads. Write outside
the tick with an atomic replacement/backup; reject incompatible saves with a clear recovery choice.
Qualify browser storage and failure reporting separately from native files.

Plan cooperative play, but do not make it a prerequisite for the first lift. Later, the host
owns cargo, money, fuel and world edits. Commands include intent/identity; the host checks
ownership, distance, crane control and placement. Duplicate/reordered requests must not pay
twice. Checkpoints include mutable objects and project/job state; content packages still only
describe their initial definitions. Resolve whether money belongs to a shared company before
cooperative progression; proposed first rule is one host-owned world and shared funds, with
no transferable cross-server wallet. Follow [plugin P5](PLUGIN_PLAN.md) and the existing
[protocol](MULTIPLAYER_PROTOCOL.md) for authority, late joins, prediction and compatibility.

## Playable slices and gates

S0 and S1 begin as small playable experiments. Later rows are milestones, not single-PR
commitments: Creative manipulation, placement/undo, save storage and boat trading each need
separate reviewable steps. Refine only the next milestone into implementation tasks. Each
implementation step uses a separate worktree/PR; dependent branches target their parent until
merged, following the repository stack workflow. No slice is implemented by this planning change.

| Slice | Depends on | Playable result and completion gate | Status |
| --- | --- | --- | --- |
| S0: handling experiment | Current main | One service crane, pallet, static pad and moving deck. Swing/drop/recover a crate; load two different deck positions and try a short turn. Observe voluntary experiments without rewards, stability and controls. Measure bounded tick cost. | Proposed; next |
| S1: useful crossing | S0 | Flat-deck loaner, assisted mooring, two working sites, one short delivery and a landing that helps the next outing. Optional variation: two light crates versus one awkward load. Empty/loaded boats feel distinct. Retry/capsize/reset leave a valid job. A player completes it without developer help and tries another idea. | Proposed |
| T0: towing toy | S1 | Borrowed tow fitting, one disabled craft and shelter handoff, without currency or a rescue countdown. Try turns, release, reset and recovery. Establish a second enjoyable activity before pricing work. | Proposed; before S3 |
| S2: Creative harbour | S1 | First expose boat selection, cargo spawning/removal and reset as a small toybox. Then add validated dock pieces, undo and layout export in separate steps. Reject obstructed channels/entrances atomically. Player builds a useful or amusing arrangement without a job prompt. | Proposed |
| S3: saved Working Harbour | S1 + T0; reconcile S2 edits | Versioned native/browser save path, a few varied local jobs, currency and buy/rent/sell boats, delivered in separate steps. Persistent project changes; preview a purchase through borrowing. Save/reload cannot duplicate goods or payouts; player has a choice beyond repeating one delivery. | Proposed |
| S4: fuel and services | S3 | Fueling, readable range/reserve, service points and recovery. Round-trip economics stay positive for introductory work; a zero-funds/empty-tank save remains playable. Test whether scarcity creates interesting decisions. | Proposed |
| S5: towing and assistance jobs | T0 + S3; S4 for priced fuel | Develop the proven tow toy into winch, tension/length cues and paid assistance. Break/release/reset safely; prove turns, wake crossings and capsize without solver explosions. Try a rescue and a barge-like cargo job. | Proposed |
| S6: harbour projects and expedition | S2–S5; adventure map for Vinga | Several persistent useful sites and boat capabilities; Explorer fitting-out goal. A short local expedition works before any Vinga expansion. Review progression pace and reasons to keep playing after the purchase. | Proposed |
| S7: cooperative working harbour | Stable solo state; plugin/network authority contract | Two friends can share helm/crane work, join late and reconnect. Host validates edits, cargo and spending; tests cover conflicting commands and duplicate rewards. Qualify native/browser pairs. | Proposed |

Review fun after every playable slice; these gates are observations to gather, not automatic
approvals or substitutes for automated checks. If S0 feels fiddly, simplify attachment, damp
sway or improve the camera before adding more physics. If S1 is dull, shorten travel and make
the destination change more useful before introducing money. Do not turn open play into a
required chain of chores merely because later slices are listed.

## Astra review, 2026-11-06

At the user's request, Astra reviewed this proposal against the existing boat/marina code and
suggested additions. Incorporated: voluntary toy experiments in S0; assisted mooring and explicit
crane-mode time; a landing useful on the next trip and finite versus repeatable work; an early
towing toy before money/fuel; explicit one-way moving-deck coupling and a one-load-first budget;
and short-session recovery/save rules. Creative starts as a small toybox, and later milestones
are split into separate implementation PRs. This review supports the design direction; it is
not physics verification, human playtesting or user acceptance of a future implementation.

## Verification and handoff

For implementation, test virtual-clock stepping, finite states, constraint limits, moving deck
contacts, legal state transitions, reset/capsize, deterministic job events, save failures and
exactly-once rewards. Preserve core/net boundaries and existing empty-boat traces. Capture
and inspect CPU/GPU frames for crane reach, load/deck clearance, placement previews and the
completed improvement. Run a windowed manual demo for motion/audio changes, then hands-on
crane, boat and touch controls; scripted screenshots cannot establish enjoyable handling.
Run the build and full CTest preset for each implementation handoff. Profile the maximum active
cargo case beside harbour traffic and wakes; record tick p95/p99 and frame cost on named
hardware before committing larger budgets. Qualify saves and controls on native and browser.

Planning verification, 2026-11-06, macOS arm64: Debug configure/build passed using the existing
pinned raylib/Lua/ENet source trees; `PROTEUS_GPU_TESTS=OFF`, retired wake-field tests off.
The build emitted one upstream `stb_vorbis.c` pointer-comparison warning. The first full CTest
run encountered sandbox socket restrictions; the complete rerun with local socket access
passed all **370 enabled tests** in 34.63 s (13 retired tests disabled). All **330 local
links/anchors** across the five changed Markdown files resolve; `git diff --check` passes.
Native GitHub build workflows skip Markdown-only changes; opening a PR does not imply those
build checks ran. This is a documentation-only proposal; no new audiovisual review is needed.
No cargo simulation, runtime performance, hands-on playback, balance or player acceptance is
established. Windows/Linux/browser builds and GPU tests were not run for this change.
Unresolved design details: exact working sites, cargo solver, boat dimensions/deck capacity,
control bindings, prices and cooperative ownership.
