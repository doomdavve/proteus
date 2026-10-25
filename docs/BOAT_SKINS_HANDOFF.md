# Nordic speedboat handoff to multiplayer

Ready for integration, 2026-10-24. The user accepted the speedboat look, then the four
Nordic country versions ("Great!") and requested this handoff. The changes are isolated
on `boat-speedboat-skins` in `/private/tmp/proteus-speedboat-skins`. No main-checkout files
were edited. [The boating plan](BOATING_PLAN.md#speedboat-skins--2026-10-24) owns detailed
status and evidence; [the skin guide](BOAT_SKINS.md) owns usage and rendering APIs.

## Bring the change across

Branch base: `c85bf66` (`Add multiplayer M4 step 1: client prediction`). At handoff,
committed main is `6f26862` (M4 step 3), following `55c1ce1` (M4 step 2). This branch
contains the complete cosmetic implementation, validator and documentation in one commit.
From your integration worktree, cherry-pick the branch tip:

```sh
git cherry-pick boat-speedboat-skins
```

Do not replace the destination's multiplayer files or documentation wholesale. The main
checkout also has unrelated uncommitted documentation work, which this branch does not
contain. Inspect/preserve that work in its own checkout. No `boat_net_*`, `boat_prediction*`,
`boat_remote_view*`, `wake_velocity_net.*`, protocol bytes or physics constants were changed here.

Files edited by both this branch and main since the branch base are `CMakeLists.txt`,
`README.md`, `docs/README.md` and `docs/TESTING.md`. Preserve main's M4 code, targets, test
entries and status while adding the skin entries. The skin validator is a device-free render
test; `boat_skin.[ch]` belongs in `proteus_render`, not the simulation/network libraries.
Do not carry this branch's historical test count into a claim about the combined suite.

A `git merge-tree --write-tree --messages --name-only 6f26862 boat-speedboat-skins`
preview completed without conflicts on 2026-10-24; all four shared files auto-merged. This
checks the committed trees only, not main's uncommitted documentation edits or the behavior
of the combined program. Neither branch was merged/rebased and neither checkout was changed
by the preview. Reassess if multiplayer advances beyond `6f26862`.

## Integration contract

| Presentation ID | Enum | Skin and flag |
| --- | --- | --- |
| 0 | `BOAT_SKIN_SWEDEN` | Blue/yellow; default and invalid-ID fallback |
| 1 | `BOAT_SKIN_FINLAND` | White/blue |
| 2 | `BOAT_SKIN_DENMARK` | Red/white |
| 3 | `BOAT_SKIN_NORWAY` | Red/white/navy; white-bordered navy flag cross |

These are local presentation IDs, not new protocol values. All four hulls have identical
geometry, and all flags share the accepted cloth simulation and attachment. Colors never
enter `Boat`, collision bounds, wake generation, handling, audio tuning or authoritative state.

Assign `BoatViewState.skin` **after** `boat_session_view`, `boat_replay_sample`, or any compound
literal that reconstructs a view. Those producers initialize skin to zero. The existing
`--skin` option applies to the local boat/replay; it does not communicate a choice to peers.

`boat_player_pose` copies a validated ID into `WaterfrontPlayerState.skin`. CPU drawing reads
that ID; GPU uploads immutable hull/cockpit/engine meshes for all four IDs and selects one per
actor. The legacy cabin transform now carries the cockpit. The existing ghost/nearest-other
path also supports a separate skin: `boat_gpu_draw_with_ghost` copies the ghost view's ID into
`WaterfrontSceneFrame.ghost_skin`. No pose, interpolation or collision change is needed.

`boat_flag_skin_mesh` needs caller-owned `BoatFlagMesh` scratch. The cloth snapshot is read-only;
retain separate scratch if retaining multiple returned mesh views. GPU flag vertex, normal and
color buffers refresh each frame, so changing the actor/skin reusing a mesh changes its flag too.
The legacy `boat_flag_mesh` returns only Sweden; new fleet drawing must use the skin-aware API.

## Suggested next implementation

1. For the requested four-country lineup, map stable fleet slots 0–3 to the presentation IDs
   above on every client. Current main exposes the local slot as `net->client.slot` and the
   nearest remote slot as `net->other_slot`. Assign the local and remote view IDs at their
   construction sites in `tools/wake_velocity.c`, without changing the interpolation in
   `wake_velocity_net.c`. An agreed slot mapping needs no new wire field. In connected mode,
   use that shared mapping instead of a private CLI override so observers agree.
2. If players should choose a country independently of their slot, add validated player/room
   cosmetic metadata through the multiplayer owner's protocol/version workflow. Do not put
   cosmetic choices into physics snapshots merely to get them onto the wire. This selection
   and persistence policy is not implemented by this branch.
3. Carry each actor's skin through the planned full fleet renderer. Main at `6f26862` still
   draws only the nearest other boat using the ghost path; this branch does not turn that
   into simultaneous four-boat rendering. Preserve each actor's cloth history and selected
   flag through nearest-player changes, reset, departure/rejoin and slot reuse.

## Verification and acceptance

The Nordic implementation passed warning-clean macOS Debug build and all 102 active CTests
with GPU tests enabled and retired wake-field experiments disabled (15 disabled). This
includes core/network boundaries, skin/flag checks and ENet loopback. CPU stills for all four
countries were inspected at 960x600 and native 320x200, as were GPU Finnish/Norwegian flag
captures and a normal follow view. Sweden GPU+AA/waves and Norway turning-flag playback ran
as 180-frame scripted smoke checks. User acceptance covers the look; hands-on driving,
Windows/browser checks and multiplayer skin synchronization remain unverified.

Final handoff recheck on 2026-10-24: Debug configure/build passed without warnings and all
102 active CTests passed again in 80.70 seconds with the same GPU/wake options. The
15 retired experiments stayed disabled. Log: `/private/tmp/proteus-handoff-ctest.log`.
Documentation file links and `git diff --check` passed. This turn changed documentation
only; audiovisual evidence above is from the preceding implementation review.

Before merging the combined implementation, run its own full checks:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_GPU_TESTS=ON
cmake --build build -j
ctest --test-dir build -j 4 --output-on-failure
./build/wake_velocity --skin sweden --location river --waves --gpu-aa
./build/boat_flag_review --skin norway --renderer gpu --scenario turn --frames 180
```

GPU checks need a graphics context; loopback needs permission to bind localhost. Also check
that two clients agree on country assignments, a remote skin remains attached to its stable
player/slot as nearest-player selection changes, and the flag updates with the hull. Compare
same-input simulation results across cosmetic choices and review all four simultaneously when
the fleet renderer exists. These combined multiplayer checks have not been performed here.

Local review artifacts, not committed: `build/nordic-{sweden,finland,denmark,norway}.png`,
the matching `-320.png` captures, `build/nordic-{norway,finland}-gpu.png`,
`build/nordic-follow.png`, and `build/nordic-{demo,motion}.log`. Recreate them with the commands
in the skin guide; the worktree's build cache reads existing pinned dependency sources from
the main checkout, so another machine should configure normally rather than copy that cache.
