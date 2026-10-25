# Accepted speedboat refinement: integration handoff

Integrated into main on 2026-10-25 on top of A2.3 `76d3883` ([record](#integrated-into-main--2026-10-25)); the candidate was first verified against `642ca78`. The user accepted the white hulls, distinct Nordic
liveries, deep open cockpit, sleeker
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
- Prepared and verified branch/worktree: `boat-white-hulls-integration`,
  `/private/tmp/proteus-boat-integration`.
- Accepted source commits `4010f37` and `f5450fe` applied cleanly as `066f0e8` and
  `61cfb2d` on top of main `642ca78`; the final handoff commit changes documentation only.

The main checkout was clean at preparation start and has not been edited by this work.
During verification, concurrent uncommitted renderer edits appeared in main, including
`src/renderer3d.[ch]`, `src/renderer_gpu.[ch]`, `tools/boat_water_review.c`,
`tools/validate_boat_water_benchmark.py` and `tools/validate_renderer3d.c` at the last check.
Those edits are outside this package and were left untouched. `src/renderer_gpu.c` overlaps
the boat package, so a direct merge must wait until that renderer work is reconciled.
Candidate verification covers committed main `642ca78`, not those concurrent working-tree edits.
Preserve all
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

Combined-main verification on macOS, 2026-10-25, at candidate code revision `61cfb2d`:

- Fresh Debug configure/build passed with `PROTEUS_GPU_TESTS=ON` and
  `PROTEUS_WAKE_FIELD_TESTS=OFF`. Cached pinned dependency sources were copied into this
  worktree's ignored build directory; no main-checkout cache was written. The build emitted
  one warning in raylib's bundled `stb_vorbis.c:1404` (tautological pointer comparison),
  and no project-source warnings.
- All 116 enabled CTests passed in 192.90 seconds; 15 retired experiments were disabled.
  GPU view binding/lifecycle, CPU lifecycle, the river benchmark contract, multiplayer
  loopback and skin/mount checks all passed with desktop graphics and localhost access.
- All 16 deterministic captures matched the accepted source worktree byte-for-byte:
  four liveries × three CPU angles, the CPU turn, river/basin CPU follow views and the
  Norway GPU turn. Representative integrated frames were inspected. A 180-frame Sweden
  GPU turning demo completed successfully.
- The diff against main touches only the ten refinement/validation/documentation files;
  no map/world/scenery ownership implementation or physics code changes are introduced.
- Formatting, changed local documentation links, gallery image links and
  `git diff --check` passed. No manual driving/listening, Windows or browser check was added.

Combined logs are `build/integration-{configure,build,ctest,gpu-demo,gpu-capture}.log`;
`build/integration-visual-parity.txt` records the capture hashes. The accepted comparison
page is also available at `build/boat-color-review/twin-outboards/comparison.html` in the
integration worktree. Build artifacts remain uncommitted.

## Integrated into main — 2026-10-25

The concurrent renderer work landed as A2.1–A2.3. The three candidate commits then
cherry-picked onto main `76d3883` without conflicts, followed by the separate `web-parity`
browser title/WebRTC commit, which overlaps this package only in `docs/README.md`. On macOS,
the combined Debug build was warning-clean in project sources and all 120 enabled CTests passed
in 183 s (`-j 4`, GPU tests on, localhost access). An earlier run of the same suite failed the
four real-time ENet session tests, which then passed alone and in the full rerun; the cause is
unconfirmed (load or a concurrent suite sharing the LAN discovery port). CPU river and basin
captures were rendered and inspected. No manual driving, Windows or browser check was added.
The procedure below is kept as history.

## Integration procedure

The candidate is already prepared and verified against main `642ca78`. To reproduce the
combined verification in the integration worktree (a normal configure may fetch the pinned
dependencies), run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_GPU_TESTS=ON
cmake --build build -j
ctest --test-dir build -j 4 --output-on-failure
./build/wake_velocity --renderer cpu --skin sweden --location river --screenshot build/river.png --time 3
./build/boat_flag_review --renderer gpu --skin norway --angle rear --scenario turn --frames 180
```

Next, finish/reconcile the concurrent renderer work in its owning track. If it advances
main, update the candidate onto that new committed tip, preserve the twin-motor draw loops
alongside its renderer changes and repeat combined verification. Main currently has an
overlapping uncommitted `src/renderer_gpu.c`; the merge command below is for after that work
is reconciled, not for overwriting it.

Then fold the prepared branch in from the main checkout:

```sh
git status --short
git merge --ff-only boat-white-hulls-integration
```

Preserve all concurrent renderer edits. If main's committed tip advances beyond `642ca78`,
update and verify the candidate against the newer target first. Rebuild the destination after merging and qualify any concurrent local edits
separately. This preparation does not itself merge main or change its working files.
