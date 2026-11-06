# Free ride sandbox: a working harbour

Status, 2026-11-06: **S0a basic toy accepted; S0b harbour loading exercised; depth/mooring fixes await review.**
The standalone `cargo_sandbox` now defaults to a temporary service platform in the real
Eriksberg inlet on `outer`; `--location basin` retains the controlled reference. This is
still a handling experiment, without deliveries or an addition to the normal free ride menu.
This plan owns free ride's
interactive cargo, player changes to places, Creative/Working Harbour rules, boat ownership and
economy. The [adventure plan](FREE_RIDING_ADVENTURE_PLAN.md) retains geography, weather and the
journey toward Vinga. [Marina authoring](MARINA_AUTHORING_PLAN.md) owns static scene recipes and
scheduled harbour life; [plugins](PLUGIN_PLAN.md) own authoring/runtime contracts;
[room content](ROOM_CONTENT_PLAN.md) owns loading and distribution.

Completed: design and requested Astra review; S0a's one-crate crane, pad, wave-posed deck,
explicit catch/release/lashing, assisted berth, load balance, recovery and native CPU/GPU view.
Also completed: clearer helm/crane control switching, S0b's first mapped work site,
receiver-projected landing footprints and assisted return mooring.
Next: **retest hanging-load depth and the assisted return**, then author a
second receiving site and the useful crossing (S1), with S0c qualification still open.
The user reached loaded, lashed and unmoored state and accepted this as a solid base;
the original “Tab View” label obscured that Tab also switches input controls. No earnings
or progression is implemented. The user reports that harbour loading works as well as
the basin but found the shadow unclear and returning very hard. The revised depth cues,
throttle/brake display and mooring assist need hands-on review; automated checks do not
establish acceptance of those fixes.

## S0a: try the cargo toy

Build and run from the implementation worktree root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/cargo_sandbox
```

This native experiment defaults to `outer` and GPU; `--renderer cpu` uses the reference
rasterizer. `--location basin` selects the original isolated fixture. It starts with a
borrowed deck boat moored beside one crate and a small service crane in the Eriksberg inlet.
There is no save, money, job or timer. **R resets both boat and crate immediately**;
**P pauses the simulation**, including a suspended load. Escape closes the experiment.

![S0b Eriksberg service crane and moored deck boat](evidence/sandbox/2026-11-06/harbour-crane.png)

First lift:

1. Press **F** to catch the crate. Press **2** or **3** for the left/right deck target.
   The optional aim assist raises and slews the crane using its ordinary limited motors;
   it does not teleport the load. Wait for the hook to stop above the deck.
2. Hold **E** to lower. A dark footprint follows the receiving deck before contact,
   and **DECK GAP** reports the remaining height. The prompt changes at touchdown.
   Press **F** to release the hook, wait briefly for settling, then **L** to lash it.
   A green crate and two straps confirm it is secured.
3. Press **G** to unmoor and **Tab** to activate helm controls (and change camera). Use **Up/Down** to adjust throttle,
   **Left/Right** to steer and hold **Space** to idle the throttle and brake. Try a short turn through the yellow
   markers. Reset and compare the other deck position, or leave the crate on the pad
   and take the boat out empty.

For manual crane experiments, **A/D** slew, **W/S** extend/retract and **Q/E** raise/lower.
Manual motor input cancels aim assist; **1** aims at the source pad. Mouse buttons expose
the crane motors and actions; driving currently uses the keyboard. The Tab button names its
destination, **Helm** or **Crane**, and the header names the active controls. Tab switches
controls as well as camera, returning to the helm
while a load hangs, stopping the motors while physics continues. Departure rejects a
connected hook or loose deck cargo. To return, use the berth distance/turn cue and approach
the boat-shaped outline at low speed. Hold Space to stop; when **BERTH READY** appears,
press **G** to stop and align automatically. Press G again to cancel the assist. R remains
the immediate recovery if docking gets in the way.
Unlash only at low speed; a floating/dropped crate remains simulated and can be caught again
if in reach. Reset is always available when it is out of reach.

Useful feedback: does catching, lowering and lashing read clearly; is the aim assist helpful;
does sway invite another attempt; can you see the receiving deck; are empty/left/right turns
different enough to explore? No reward loop is needed to answer these questions.

### S0 steps and current limits

| Step | Status | Next gate |
| --- | --- | --- |
| S0a controlled handling toy | Implemented as native `cargo_sandbox`; basic toy accepted by the user; control labels clarified | Review the updated labels in the harbour experiment |
| S0b harbour placement | First Eriksberg work site implemented, with native CPU/GPU scenery and boat/shore/camera contacts | Review projected landing footprints and assisted return; author the second receiving site before S1 |
| S0c broader qualification | Not started | Multiple loads and measured 16-body stress ceiling, browser/touch, audio feedback and harbour traffic/wake performance |

The original S0a test basin preceded harbour site selection; it remains an explicit
reference mode. One upright 0.8 m crate is the
entire active-body budget in this step. Four bounded cable projections, one support contact
and fixed 120 Hz stepping provide pendulum motion, deck sliding and water recovery; there
is no cargo/cargo contact, stacking, free angular rigid-body solver or dependency addition.
The pad and deck catch contact use bounded support regions; a fallen crate has simple
surface recovery rather than multi-sample buoyancy. No solver stability claim extends beyond
these fixtures. The boat has a 4.6 × 2.6 m contact/visual recipe but still uses the existing
speedboat's six buoyancy supports. Secured cargo reduces speed changes and adds bounded
trim/heel; loose/hanging cargo exerts no reaction forces. This is an authored workboat
approximation. The temporary work platform uses a conservative circular boat exclusion;
the `outer` boat
also uses the existing mapped shoreline, berth and moving-traffic contacts. Dropped floating
cargo stops at the compiled bank. The platform is a new temporary native prop, not an
existing decorative quay assumed to be a load-bearing surface.

Both renderers consume the same evaluated box/beam meshes, including the hook, cable,
crate, straps and deck markers, at 480 × 300 with nearest-neighbour integer presentation.
The basin retains its simple test view; `outer` uses the production scenery, harbour actors
and water meshes (including the GPU near-water patch). The custom boat/crane/cargo pass has
simple lighting and does not yet participate in GPU reflections or cast directional sunlight
shadows. A shared vertical landing footprint is projected onto the deck, pad and water
before contact to provide a depth cue; it is not a sunlight shadow-map implementation. The CPU
reference limits draw distance to 240 m to bound displaced-water work; the GPU uses 4500 m. CPU
display-free captures contain the 3D view; windowed CPU and GPU captures include the HUD.
There are no new sounds. Browser/touch, normal-game integration, persistence, multiple cargo
IDs, progression and multiplayer remain outside these S0 steps.

Review and measurement commands:

```sh
./build/validate_boat_cargo --benchmark
./build/cargo_sandbox --survey
./build/cargo_sandbox --location basin --renderer cpu --screenshot build/cargo-basin-cpu.png
./build/cargo_sandbox --renderer cpu --screenshot build/cargo-origin-cpu.png
./build/cargo_sandbox --renderer gpu --scenario swing --screenshot build/cargo-swing-gpu.png
./build/cargo_sandbox --renderer gpu --scenario loaded-left --screenshot build/cargo-left-gpu.png
./build/cargo_sandbox --renderer gpu --scenario loaded-right --screenshot build/cargo-right-gpu.png
./build/cargo_sandbox --demo --frames 360
```

`swing` advances actual crane physics; the two loaded review fixtures use the application's
aim assist to lift onto the wave-posed deck, release, settle, lash, depart and drive a short
turn. They fail if a transfer cannot complete. The display-free CTests
`cargo_sandbox_left_screenshot` and `cargo_sandbox_right_screenshot` exercise the basin paths;
`cargo_sandbox_outer_left_screenshot` and `cargo_sandbox_outer_right_screenshot` repeat them
in the harbour;
`boat_cargo` separately checks the core state/constraint transitions. Automated fixtures do
not establish a successful human lift or enjoyable controls. `--benchmark` measures a single
hanging cargo tick with timer overhead; it does not establish the harbour/16-body budget.

## S0b: first Eriksberg work site

The experiment's default mooring is simulation **(10, 245), heading 0** at the north end
of the Eriksberg inlet, outside the marina entrance and registered traffic paths. The
3 × 6 m temporary fixed work platform sits five metres toward simulation +X from the
boat, with its original 1 m pad surface and 5.5 m crane tip height. It is an explicit test
prop, not a claim about existing quay strength, seabed depth or real crane engineering.
One cargo/crane state is translated to this origin; the normal game's map and packages are
unchanged. This step does not yet author a second work site, unload at a destination or
complete a persistent landing.

`--survey` loads the actual scenery pack and checks the site's water and berth clearance
on a 1 m grid over local X = -3..11, Z = -5..12, with a 1 m shoreline margin. It checks a
larger conservative traffic rectangle (centre +2,+2, half beam 10 m, bow/stern 12 m) at
0.25 s intervals through the longest retained actor period. This covers each compiled
actor's cycle; it is sampled clearance evidence, not a proof about future authored routes.
The launch runs the same check. Initial and live cameras check loaded scenery, berths,
traffic, and the temporary platform/tower/boom along the viewing segment.

Try the same lift, then drive out towards the marina at low speed, steer around the
moored boats and return. The boat outline now marks the actual final berth position,
with a bow arrow for heading 0. Enter the larger assisted capture zone, roughly facing
the arrow, at no more than 1.5 m/s; **BERTH READY** signals that G can take over alignment.
Hold Space to slow down, and use G again to cancel an active capture. R remains the quick reset. This tests whether loading still reads clearly against a real waterfront and whether
a loaded boat is interesting to take out and dock again. The broader map retains its
ordinary visual traffic wakes; no physical ferry wakes were added.

Two other candidate footprints, **(-105, 0)** and **(-200, -150)**, passed the preliminary
water/berth/traffic survey but have not been camera-reviewed, authored or connected as
receiving sites. They are candidates only. Next: select a receiver, qualify its support and
approaches, and implement the first useful crossing before adding earnings.

## S0b: depth cues and returning to the berth

User feedback, 2026-11-06: harbour loading works as well as the basin, but the shadow did
not communicate depth and returning was very difficult. Confirmed causes: the old landing
marker stayed at water height until physical deck contact, hiding it beneath the boat; the
green lines were behind the actual berth, and the G gate required an unmarked point within
0.6 m, speed below 0.5 m/s and heading within about 9 degrees. Throttle was not displayed.
This feedback establishes a working harbour loading path, not acceptance of the return UX.

The shared CPU/GPU landing footprint now projects onto the wave-posed deck **before**
contact, clips at the receiver edges, and darkens close to touchdown. The static pad and
water have their own projected footprints. The DECK GAP readout measures crate-bottom
clearance from the actual deck plane. These are explicit vertical depth cues; the custom
boat/crane/cargo pass still has no directional sunlight shadows. The whole temporary
platform's supported crate-centre area now agrees with its drawn length.

The return outline marks the final hull pose, with a bow arrow and water-following lines
raised above the waves. The HUD reports distance and a turn cue: toward the berth when
away, then toward the bow-arrow heading in the capture area. It also shows throttle
FWD/REV/IDLE, a signed bar, speed and engine response. Up/Down sets persistent throttle;
releasing the arrow does not idle it. Holding Space cuts the command and adds a bounded
2 m/s² stopping aid, only in this experimental tool. Ordinary game handling is unchanged.

G becomes available when the boat centre is in local renderer X = -0.6..2.4 m and
Z = -3.5..5 m, speed is at most 1.5 m/s, heading is within 45 degrees of the bow arrow,
and the hull is upright. The asymmetric zone stays on the open-water side of the platform
and inside the surveyed work envelope. The outline is the **final** boat pose; BERTH READY
appears before exact alignment is needed. G starts an explicit alignment assist (at most
1.5 m/s translation and 0.6 rad/s yaw), idles/centres controls and retains wave heave, pitch
and roll. It ends at the original berth; G cancels without an additional snap and ordinary movement
resumes. Departure
still rejects a connected hook or loose deck cargo, and a capsize cancels capture.

Review commands (`berth-approach` is an explicit return pose; `mooring`/`remoor` then use
a crate obtained by a real lift/lash and real G/capture ticks, not a seeded moored state):

```sh
./build/cargo_sandbox --scenario shadow-high --screenshot build/shadow-high.png
./build/cargo_sandbox --scenario shadow-low --screenshot build/shadow-low.png
./build/cargo_sandbox --scenario berth-approach --screenshot build/berth-approach.png
./build/cargo_sandbox --scenario mooring --frames 180
./build/cargo_sandbox --renderer cpu --scenario remoor --screenshot build/remoor-cpu.png
```

Next: review these depth and return controls by hand, then continue with the receiving site
and first useful crossing. No new boats, rewards, persistence or physical ferry wakes are
part of this correction.

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
merged, following the repository stack workflow. S0a implements the first controlled test;
the remaining gates below are still open.

| Slice | Depends on | Playable result and completion gate | Status |
| --- | --- | --- | --- |
| S0: handling experiment | Current main | One service crane, pallet, static pad and moving deck. Swing/drop/recover a crate; load two different deck positions and try a short turn. Observe voluntary experiments without rewards, stability and controls. Measure bounded tick cost. | S0a basic toy accepted; S0b first harbour site implemented; receiving site and S0c qualification remain |
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

### S0a implementation verification, 2026-11-06

Apple M1 Max / macOS arm64, Debug, `PROTEUS_GPU_TESTS=OFF`, retired wake tests off:
configure and complete build passed using the existing pinned dependency source trees.
The clean build emitted the existing upstream `stb_vorbis.c` pointer-comparison warning;
the added C code built without warnings. The final `ctest --preset full` with local socket
access passed **373 enabled tests** (13 retired tests disabled); the earlier sandboxed run
failed the existing socket tests. Core/net boundaries and the existing game checks pass.
Targeted Release builds of `validate_boat_cargo` and `cargo_sandbox`, cargo checks and both
actual lift/departure captures also pass; no Release full-suite run is claimed.

Inspected CPU/GPU origin, suspended swing and left/right loaded-turn captures. Windowed
GPU (360 frames) and CPU (180 frames) scripted crane demos exited 0. One initial sandboxed
graphics launch could not access desktop services; a later attempt found the display asleep.
The final desktop checks ran with desktop access and the display awake. No new sound was
implemented or reviewed, no GPU CTest matrix was enabled, and no hands-on keyboard/mouse,
touch, human motion/audio review or user acceptance is claimed.

The single hanging-body measurement reported approximately **0.15 µs mean / 1 µs p95 /
1 µs p99** in Debug and **0.08 / 1 / 1 µs** in Release, including timer/check overhead;
the 1 µs timer quantization limits interpretation. This is cargo stepping alone, not frame
cost, multiple-body capacity or harbour traffic/wake qualification. The real application
lift/departure CPU capture checks each took about 0.05 s in the targeted CTest run.

![Scripted real lift, lashing and loaded turn](evidence/sandbox/2026-11-06/loaded-turn.png)

All 268 local links/anchors across the four changed guides resolved before adding these two
evidence links (both image paths checked separately); `git diff --check` passed. Windows,
Linux and browser builds were not run locally. Native CI and hands-on feedback remain
separate gates. Next task and known limits are listed in the S0a section above.

### Historical planning verification

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
At that planning checkpoint, unresolved design details were exact working sites, cargo solver,
boat dimensions/deck capacity, control bindings, prices and cooperative ownership. S0a now
provides experimental solver/deck/control choices; their qualification and harbour sites
remain open as described above.

### S0a control clarity follow-up, 2026-11-06

The user successfully loaded, lashed and unmoored, and called this a solid base to build on.
They understood “Tab View” as a camera-only change, so the button now names the destination
controls (Helm/Crane), the header names the active controls, and the unmoored crane prompt
explicitly explains how to start driving. Secured/unmoored state guidance takes precedence
over an old hook message. This records acceptance of the basic toy, not broader S0
qualification or acceptance of the updated UI. Next: S0b harbour site survey/integration.

Parent CI: macOS and browser authoring passed; Linux's existing `boat_plugin_stage` timed out
and Windows could not download Lua. The cargo checks passed on Linux. These failures were
inspected and are not attributed to cargo portability; they remain unverified until rerun.

Control follow-up verification, 2026-11-06, M1 Max/macOS arm64: Debug configure/full build
and `ctest --preset full` passed all 373 enabled tests (13 retired tests disabled; GPU CTest
matrix off, local sockets enabled). GPU departure/helm captures were inspected and a 120-frame
windowed scripted demo exited 0. `--scenario departure` reproduces the user's unmoored crane
handoff after a real lift/release/lash. No hands-on acceptance of these revised labels is
claimed. Next: harbour site survey. The [departure capture](evidence/sandbox/2026-11-06/departure-controls.png)
records the new prompt and destination button.

### S0b first-site verification, 2026-11-06

Apple M1 Max/macOS arm64, Debug, pinned local raylib/Lua/ENet sources,
`PROTEUS_GPU_TESTS=OFF`, retired wake tests off: configure/full build passed, followed by
**376 enabled CTests passing** in 36.07 s with local sockets enabled (13 retired tests
disabled). The clean build emitted the existing upstream `stb_vorbis.c` warning; added code
built without warnings. Targeted Release builds, core cargo checks, the site survey and
both real harbour lift/departure CPU captures passed. No Release full suite is claimed.

The validator now checks translated-origin sway/mooring and floating cargo stopping at a
real mapped bank. Translation initially exposed sway drift from reconstructing velocity
using global float position differences; applying constraint corrections directly to velocity
fixed the regression. Basin and mapped actual lift/release/settle/lash/departure tests pass.
The surveyed site and initial camera are clear; nearby shore, compiled marina and retained
traffic remain enabled. These checks do not prove seabed support or future route clearance.

Inspected CPU/GPU harbour origin, swing, departure and left/right loaded-turn captures, plus
a normal-game GPU capture confirming the ordinary player remains visible. The shared scene
can retain its water/focus anchor while hiding the built-in boat for this custom workboat
pass; normal scenes retain the default behaviour. Windowed GPU scripted runs completed,
including a final 240-frame run in 5.61 s end to end (startup included). This is not a frame
p95/p99 measurement. The final 120-frame CPU demo completed in 19.85 s, exit 0.

CPU limitation: the first 4500 m CPU water view was unexpectedly expensive; two long CPU
demo runs were stopped after diagnosis rather than counted as passing. A 3 s Xcode
Instruments Time Profiler trace was recorded at `build/cargo-cpu.trace`; a simultaneous
[stack sample excerpt](evidence/sandbox/2026-11-06/cpu-water-sample.txt) places the sampled
main-thread work in displaced-water evaluation (1.4 GB footprint, 1.8 GB peak in that
pre-limit process). The CPU reference now caps its draw distance at 240 m, but remains
about six frames/s in the recorded scripted run. **Use the default GPU renderer for play.**
Far-water/CPU optimisation and maximum-load/traffic frame budgets remain open S0c work;
this result does not establish a CPU performance target or a 16-body budget.

![S0b actual harbour lift and loaded turn](evidence/sandbox/2026-11-06/harbour-loaded-turn.png)

No human acceptance of the harbour site, revised control labels or numerical sway fix is
claimed. Windows/Linux/browser builds were not run locally; the GPU CTest matrix, touch,
new audio, custom-object reflections/shadows, a receiving site and persistent delivery
remain open. Original S0a PR CI passed native macOS/Linux/Windows and browser authoring
(after retrying unrelated Linux stage and Windows dependency-download timeouts); the
control follow-up's macOS/Windows checks passed, with its Linux stage timeout retried.
The new harbour branch requires its own CI. Next: try a loaded outing and return, then
qualify/author a receiver for the useful crossing, without adding money yet.

Documentation handoff: all 402 local links/anchors across the seven changed guides resolve;
`git diff --check` passes. The overview records the first mapped site; detailed remaining
steps stay in this plan. No generated build caches, profiling trace bundle or save data are
committed; the stack-sample excerpt and two review screenshots are intentional evidence.

### S0b depth/return verification, 2026-11-06

User report: harbour loading works as well as in the basin; landing depth is unclear and
returning is very hard. Confirmed and corrected the below-deck pre-contact marker, the
unmarked exact mooring point, missing throttle feedback and lack of a stopping aid. This
records the reported loading path and usability problems, not acceptance of the fixes.

Apple M1 Max/macOS arm64, Debug, pinned local dependencies, `PROTEUS_GPU_TESTS=OFF`:
complete build and `ctest --preset full` passed **379 enabled tests** in 41.02 s (13 retired
tests disabled; local sockets enabled). The initial clean build emitted the existing
upstream `stb_vorbis.c` warning; the added C code is warning-clean. Release cargo checks
and the high/low hanging-load and loaded-remoor CPU review scenarios also passed; no
Release full-suite claim is made.

`boat_cargo` now checks a landing footprint before contact on a pitched/rolled deck,
receiver-edge clipping, pad and moving-water heights, capture rejection reasons, bounded
translation/yaw, preserved wave pose, secured cargo following the assist, cancellation,
capsize cancellation, blocking unlash during capture and braking without reversal. The
three new application checks stop real lifts at about 1.2 m / 0.2 m deck clearance and
complete the real G/capture path with cargo obtained through an actual lift/lash. The
return pose is an explicit fixture; it does not establish a human-driven round trip.

Inspected CPU and GPU high/low landing footprints, ready-to-moor, mid-capture and completed
loaded-return frames. GPU windowed smoke runs completed for mid-capture (180 frames) and
the crane demo (240 frames); a 12-frame CPU mid-capture windowed run also exited 0. These
are scripted motion/input-path checks, not hands-on keyboard/mouse or user acceptance.
No sound changed. Native Windows/Linux/browser checks and a GPU CTest matrix were not run
locally for this follow-up; its own PR CI remains separate from parent #161's successful CI.

![Landing footprint before deck contact, with gap readout](evidence/sandbox/2026-11-06/landing-shadow.png)

![Loaded boat inside the assisted capture zone](evidence/sandbox/2026-11-06/berth-assist.png)

Known limits remain the slow CPU reference, approximate workboat/load handling, vertical
landing cues rather than directional sunlight shadows, no custom-object reflections,
keyboard helm input, a single source site and no delivery or save. Next: hands-on depth
and return review, then the receiving site/useful crossing. The area overview remains at
its first-harbour-site milestone; these per-step details stay in this owning plan.

Documentation check for this follow-up: all 97 local links/anchors across the four changed
guides resolve, command examples match `cargo_sandbox --help`, and `git diff --check` passes.
