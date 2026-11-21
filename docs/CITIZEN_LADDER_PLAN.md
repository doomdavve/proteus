# Citizen ladder: becoming part of the harbour

Status, 2026-11-21: **proposed, nothing implemented; no pull request is authorized.** The
working harbour ([sandbox plan](FREE_RIDE_SANDBOX_PLAN.md)) is one menu entry with one
delivery job, automatic save/resume and a day/night clock, and no money, fuel, boat choice or
progression. This plan makes the working harbour what a new player gets by default and gives it a
ladder: start on the lowest rung with a small boat and free local errands, then earn capability
and freedom (more places, heavier work, faster hulls).
Decided 2026-11-21: [boat switching](#boat-switching-decided-2026-11-21) is at a quay, and
[the first-purchase model](#the-first-purchase-decided-2026-11-21) is standing, then a free first boat.
Done when: a new player lands in the harbour, completes errands without money, earns a first
purchase, and reaches a second rung that plays differently, all surviving save/resume, with the
user's acceptance. Next task: CL0 (R0 boat and reach, progression state sketch).
This plan owns the player-facing ladder; the sandbox plan keeps cargo physics, S2 Creative,
S3 economy detail and S4 fuel. It supersedes the sandbox plan's S3 wording where they differ
(see [Relation to S0-S7](#relation-to-s0-s7)).

## Intent

The player is a **citizen** of Gothenburg Harbour, not a visitor with an unlimited toy. They
begin small and local, do useful errands for the people and places they can see, and are trusted
with more over time. Progress is mostly **capability**: what new thing can I do (tow, carry a
pallet, work in exposed water). **Speed is secondary** and varies inside each hull class (a quick
launch and a slow launch exist in the same class), so a faster boat is never a strict upgrade
that retires the earlier ones. This is the user's decision of 2026-11-21 (capability first, a mix
with speed within a class).

Unrestricted free ride and the existing menu entries stay as they are. Nothing here locks an
existing player's Explorer or makes free ride consume fuel. The game is experimental, so
the Working Harbour entry is a normal menu item, never a hidden flag.

## The ladder

Each rung adds one physical capability and one new kind of place or errand, reviewed for fun
before the next is started (the sandbox plan's rule). Names are working names.

| Rung | Boat and tools | New capability | Errands and places | Freedom |
| --- | --- | --- | --- | --- |
| R0 Dockhand | Rowing boat with a small attached outboard; no crane | Carry a parcel by hand-off at a quay; learn mooring | Free local errands between two or three sheltered quays near Eriksberg | The sheltered reach only, advised by chart marks; no hard walls |
| R1 Deckhand | Flat-deck workboat, the shipped crane and crate | Crane loading and lashing, as S1 | The existing landing, quay and jetty deliveries as a few varied errands | The river reach and the marina |
| R2 Hauler | Faster launch within the cargo class, plus a tow fitting (T0) | Tow a disabled craft to shelter | Casualty errands, parcels with deadlines but no countdown penalty | Open river up to the bridges |
| R3 Skipper | Crane-equipped tug or heavy workboat | Barges and heavy loads, work away from a crane | Projects that change places (a finger pier, a repaired landing) | The whole `outer` map in suitable conditions |
| R4 Navigator | Explorer variant | Range and exposure, remote worksites | Prepared outings; Vinga only once its map exists | Exposed water; the optional long goal |

Rules for the ladder:

- **Freedom is advice, then fit, never invisible walls.** The lowest rung is steered by chart
  marks and by a boat's own limits (range, exposure, load), not by barriers. The boat's
  suitability is the gate, as the sandbox plan says ("range and weather suitability, not the best
  cargo carrier").
- **One capability per rung.** Do not add speed tiers as a substitute for a capability.
- **Speed within a class**: a class offers a couple of hulls whose speeds differ and whose
  handling differs in feel (planing, wake, load response), chosen by taste and errand.
- **Always a way back.** A starter boat stays available; a stuck or wrecked player is re-lent a
  boat. No debt spiral, as in the sandbox plan's recovery rules.
- **Errands come from plugin data.** The errand board, rewards and unlock conditions are package
  content; native code keeps mechanics and validation (the plan's content/mechanics split).

## Boat switching (decided 2026-11-21)

Decision (the user, 2026-11-21): **switch at a quay**, option 1 below. A boat is a moored
object at the player's home quay or any quay with a berth. Switching is a menu action available
only while moored and at rest with no cargo or tow, and keeps one active boat. Options 2-4 are
recorded as rejected alternatives. Facts that constrained it:

- Playable hull recipes need separately tuned handling data, support dimensions from the hull
  and a physics revision bump before they can be selected ([boat docs](HARBOR_BOATS.md#future-playable-boats)).
  Today the Nordic liveries share handling; the working harbour has one registered workboat.
- Cargo, mooring and the crane are tied to the hull's deck and berth; switching mid-job must
  reject or resolve unresolved cargo, tow and lashing state (the sandbox plan's sale rule).
- Saves must not duplicate boats or goods, and the day/night session persists across switches.

Candidates considered:

1. **Switch at a quay (chosen).** A boat is a moored object at the player's home quay (or any
   quay with a berth). Walk-up switching is a menu action available only while moored and at
   rest with no cargo or tow. It reuses the berth system, keeps one active boat, and makes
   physical ownership visible in the harbour. Cost: it needs moored player boats in the scene.
2. **Switch from a boat menu anywhere safe.** Fastest to build, weakest as a citizen fantasy,
   and open to abuse (swap to dodge a job).
3. **A job brings its boat.** Each errand lends the right boat and returns it afterwards. Cheap
   and tidy, but the player owns nothing; it conflicts with purchases.
4. **A rental/borrow step first, quay ownership later.** Start as 3 to teach capability, grow into 1
   when the first purchase arrives.

Consequence: the scene needs moored player boats, and CL4 must render and contact-test them
through the harbour's berth system.

## R0 boat (decided 2026-11-21)

Decision (the user, 2026-11-21): R0 is **a rowing boat with a small attached motor**. Notes for
CL0, not yet designed:

- The visual starting point is the existing `BOAT_MODEL_SKIFF` recipe (4.5 x 1.9 m, open hull and
  benches), whose `engines` field allows one outboard; no new mesh family is needed to try it.
  Oars and a rowing animation would be new work and are not assumed.
- Handling is new data: slow, light and responsive, with a small motor (a few knots to low
  teens, to be tuned by driving), a weak wake and strong sensitivity to waves and passing
  ferries. This is the playable-hull work in [HARBOR_BOATS](HARBOR_BOATS.md#future-playable-boats)
  (separate handling data, support dimensions from the hull, a physics revision bump).
- Rowing is **only the look** (decided 2026-11-21): no oar mode, animation or separate handling.
  A near-empty tank instead allows a "precision" speed: a slow, steady, still-controllable
  pace, so an empty tank never strands the player (the no-debt-spiral rule). Realism is not a
  goal. Fuel itself is S4; CL0 only reserves the precision-speed hook in the R0 handling data, and
  S4 decides the threshold and whether it applies to the later boats.
- Parcels are small enough to hand over at a quay without the crane; the workboat and crane
  arrive at R1 as capability, so R0 teaches mooring and reading the harbour.

## The first purchase (decided 2026-11-21)

"Free errands until the first purchase" needs a trigger when there is no money. The user
confirmed this reading on 2026-11-21:

- **Errands pay nothing visible.** Completing them records a count of finished errands per
  place ("standing"), not money.
- **The first boat comes from standing, not a payment**: after a set number of errands the
  harbourmaster offers a loan or a first purchase, which the player accepts at no price. This
  is the "borrowing preview" the sandbox plan already wanted.
- **Money starts after that**, with the first paid errand, posted prices and the S3 rules (single
  currency, fixed quoted payouts, no market).

Rejected: money that exists from the start but stays hidden, which would read as a hidden
feature.

## Steps

One branch and one pull request for the whole track once the user approves a PR, built from small,
focused commits (one per step). Each step ends with its plan status, evidence and limits updated.
Playable steps need the user to play; the plan never infers acceptance.

| Step | Result | Needs |
| --- | --- | --- |
| CL0 | Specify the R0 rowing boat (see [R0 boat](#r0-boat-decided-2026-11-21)), the sheltered reach and a parcel hand-off; write the progression state sketch | | None; decisions made |
| CL1 | The working harbour becomes the default new-player path (title/menu), still reachable and still leaving free ride untouched; first-run flow lands on R0 with no HUD help text over the screen | CL0 |
| CL2 | The errand board: three to five varied R0/R1 errands as package data, a local standing counter, errand chaining that reuses S1's delivery events; free, no money | CL1 |
| CL3 | Progression state in the save (versioned, migrating the existing S1 save): standing, owned boats, unlocked reaches, completed projects; exactly-once for rewards; autosaves as today | CL2 |
| CL4 | The first boat offer and the chosen switching mechanism, with a second playable hull (handling data, support dimensions, physics revision bump) | CL3, boat work |
| CL5 | Money and the first paid errand and purchase, with posted prices; S3's rules | CL4, user fun check |
| CL6 | Rung R2: T0 towing toy as the next capability | CL5 |
| CL7 | Closing step: move the ladder's contract into a spec (`docs/WORKING_HARBOUR.md`, already a listed candidate), drop or schedule leftovers, mark this plan done or history | all |

Later rungs (R3, R4) and S4 fuel are scheduled only after R2 is judged fun.

## Relation to S0-S7

This plan reorders the sandbox plan's later slices around the player's role, not their number:

- CL0-CL2 build on S1 (merged) and need nothing from S2.
- CL3-CL5 are S3 (saved Working Harbour), split by ladder rung; the sandbox plan's S3 text on
  currency, prices and exactly-once remains the contract.
- CL6 is T0 and the front of S5; S4 fuel follows R2.
- S2 Creative harbour stays a separate toybox and is not on the ladder's critical path.
- S7 cooperative play stays deferred to the plugin P5 follow-up.

## Defers (Open loops board check, 2026-11-21)

Named so this plan does not quietly absorb them: S1 fun review and S0c qualification stay with
the sandbox plan; the structured HUD proposal ([BOAT_HUD](BOAT_HUD.md#structured-contextual-information-proposed-2026-11-07))
stays proposed but CL1 should not add help text; the user's night/cargo/cycle acceptance of T4 and
the DAMAGED hint check stay open; sandbox authoring leftovers stay unscheduled; marina resident
routes and collision avoidance stay with the marina plan; Vinga and the Gothenburg landmarks stay
on the adventure plans; multiplayer sandbox (S7) is out of scope.

## Risks

- **Boat work is the long pole.** A second playable hull needs handling, support and physics
  revisions before any rung beyond R1 works.
- **Free errands may feel aimless** without a reward; standing and unlocking places must show
  visible change in the harbour (a quay that opens, a boat that appears at the berth).
- **Default entry changes first impressions**; keep the controls teachable without on-screen help
  and keep free ride one click away.
- **Save migration**: the S1 save must load into the ladder without a reset or duplicated goods.
