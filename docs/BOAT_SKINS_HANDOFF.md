# Accepted speedboat refinement: integration handoff

The user accepted the white hulls, distinct Nordic liveries, deep open cockpit, sleeker
speed profile and twin outboards on 2026-10-25, then requested preparation for main.
This handoff supersedes the 2026-10-24 Nordic multiplayer handoff, whose original package
was already integrated into main. [The boating plan](BOATING_PLAN.md#speedboat-skins--2026-10-24)
owns acceptance and dated source-worktree evidence; [the skin guide](BOAT_SKINS.md) owns
usage and rendering APIs.

## Branches and scope

- Source branch/worktree: `boat-white-hulls`, `/private/tmp/proteus-boat-white-hulls`.
- Source base: `d73688a`, the completed A1.3 water-mesh ownership step.
- First refinement commit: `4010f37` (white hulls, cockpit cutout/depth and review camera).
- Source tip: accepted sleeker hull, swept windscreen, tapered stern/cowl and twin outboards,
  with steering-mount validation and updated documentation.
- Integration target: main `642ca78`, following A1.4 (`853f25d`) and A1.5 (`642ca78`).
- Prepared branch/worktree: `boat-white-hulls-integration`,
  `/private/tmp/proteus-boat-integration`; combine and verify there before changing main.

The main checkout was clean at preparation start and is left unchanged. Preserve all
A1.4 renderer/map bindings, view-owned scenery, lifecycle gates and adventure planning.
Do not copy the older source worktree's shared files over newer main files wholesale.
Apply only its refinement commits, resolving shared documentation around main's status.

The package changes presentation and validation only. All four hulls remain white, with
the accepted country decks/stripes, matching flags and shared hull geometry. The cockpit
cutout and lowered seats remain; the rejected helm and upper-bow-rake trial are absent.
Each outboard keeps its own hull-relative mount and the original steering rotation.
CPU/GPU player, rival, replay and ghost paths draw both motors; GPU boat passes include
reflection and shadow rendering. The flag attachment and simulation are unchanged.
No thrust, mass, handling, collision, wakes, audio, protocol or recording changes are included.

`BOAT_SKIN_ENGINE` remains one immutable motor mesh. Callers draw it twice using
`boat_skin_outboard_pose(hull, engine_center, side)` for sides -1 and +1. Offsets follow
the hull rotation, so steering does not swing the mounts around a shared pivot.
The skin validator checks both mounts under heading, roll, pitch and three steering angles.

## Verification

The source package passed a warning-clean macOS Debug build and all 91 enabled CTests
in 66.69 seconds with localhost access; 13 retired experiments were disabled. Representative
CPU captures across the liveries, a turning capture and river/basin follow captures were
inspected. A 180-frame GPU turning demo completed and the Norway GPU turning capture was
inspected. These are scripted smoke checks, not manual driving or listening. User acceptance
covers the look; Windows and browser review remain unverified.

Source artifacts remain under `build/boat-color-review/` in the source worktree. The latest
page is `seat-depth/comparison.html`; the accepted single/twin comparison is retained in
`twin-outboards/comparison.html`. Build artifacts and machine-specific caches are not committed.

Combined-main verification: pending in the separate integration worktree. Record actual
results here after applying both source refinement commits to main `642ca78`.

## Integration procedure

Prepare the candidate in its own worktree, applying the source commits after `d73688a` in
order. Resolve conflicts without replacing main's map/scenery ownership work. Then run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build -j 4 --output-on-failure
./build/wake_velocity --renderer cpu --skin sweden --location river --screenshot build/river.png --time 3
./build/boat_flag_review --renderer gpu --skin norway --angle rear --scenario turn --frames 180
```

Once the prepared branch is verified and main still points to its recorded target, fold it
in from the main checkout with `git merge --ff-only boat-white-hulls-integration`. If main
advances, prepare and verify against the newer target first. This preparation does not itself
merge main or change the user's main checkout.
