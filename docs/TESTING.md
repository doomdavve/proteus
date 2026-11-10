# Test workflow and timing

`boat_sandbox_save` (SV1b, `cpu;quick`) checks the sandbox save's byte format, header-only discovery, corruption and
bounds, compatibility verdicts, the work digest and the pure autosave policy (SV1c) on byte buffers and virtual
ticks only. `boat_file` (SV1d, `cpu;quick`) checks atomic writes with `.bak`, failed writes, set-aside, bounded listing and
reads in a build-tree directory. `app_input` (AR2a, `cpu;quick`) drives the pause menu's `boat_menu_update` with synthetic `BoatAppInput` values (navigation wrap, per-row activation, steering/fullscreen turns, pointer hover/press, menu clock); no window or raylib input. `boat_work_menu` (SV1h, `cpu;quick`) checks the Single Player menu's save discovery in build-tree directories (none, one, corrupt, newer format, changed package, many files, ignored names, fixed time zone), set-aside, delete-only-that-package and the SAVE row's confirmations with no clock, plus the save identity's writer string. `wake_velocity_title_start.outer-working-harbour` (slow lane) starts the menu's content without a window. No title capture is a CTest (it needs a window): run `wake_velocity --title --screenshot FILE --content working-harbour --data-dir DIR [--save-mode new --ask]` by hand. `cargo_sandbox_save_*` and `cargo_sandbox_autosave*` (SV1d, slow lane) write, resume and
reject saves under `build/sandbox-save-tests` and drive the loop's autosave on a virtual clock; none touches the user's
data directory; each fails on the program's exit code (`--expect resumed|rejected`; a rejected fixture is moved back afterwards, so `ctest --repeat` can rerun it). `cargo_sandbox_scene_survives_reset`
(`--scene-check`) requires a session reset to keep the package's scene, with its custom notice. `cargo_sandbox_resume_<scenario>` (SV1c, slow lane, about 1 s each) stages a review scenario, saves it,
resumes it from the record and from its encoded bytes, and requires equal behaviour over 1200 scripted ticks.

`wake_velocity_work_*` (SV1f) render `--work` through the CPU view (`render`, slow lane): the scripted berth
at 3 s, the scripted landing delivery at 14 s, and a refusal of `--work --package`. `wake_velocity_work_gpu_screenshot` and
`wake_velocity_work_delivered_gpu_screenshot` are their GPU-lane twins. `wake_velocity_work_save_write` then
`wake_velocity_work_save_resume` (SV1g, slow lane) save a scripted delivery to `build/work-save-tests` and resume it
with a far camera that shows the built quay; both run through `cmake/run_expect.cmake`, which requires exit code 0 and an output
match. The windowed `--demo --frames` smoke run with `--data-dir` in the build tree is a developer command (windowed frames follow the wall clock, so it is not a CTest).

S0a/S0b `boat_cargo` drives deterministic 120 Hz cargo ticks: bounded crane motors/cable,
actual lifts onto both deck positions, release/settle/lash, moving deck attachment,
rotational point velocity on unlash, departure guards, dropped/floating recovery, capsize,
reset, translated-origin sway/mooring, floating cargo stopping at a mapped bank and unchanged
empty-load effects. It also checks pre-contact receiver footprints on a pitched/rolled deck,
clipping at deck edges, pad/wave height, assisted capture gates and bounded alignment,
cancellation, preserved wave pose and braking without reversal. Checks remain active in Release builds.
`./build/validate_boat_cargo --benchmark` additionally reports one hanging body's tick cost;
measurements are not pass/fail timing limits. `cargo_sandbox_left_screenshot` and
`cargo_sandbox_right_screenshot` additionally drive the application's assisted lifts onto a
wave-posed boat, lashing, departure and turn before a display-free basin CPU capture.
The two `cargo_sandbox_outer_*_screenshot` checks
repeat real transfers on `outer`; `cargo_sandbox_site` loads the bundled `proteus.outer-work` (the basin's `proteus.basin-work` is covered by the catalog and staging checks)
site and qualifies its footprint, sampled traffic envelope and initial camera against the
loaded map. `boat_plugin.work_sites` checks `scene.work` decoding, the staging wire and the
staging survey's rejections, including turned overlap; `boat_cargo` also checks that traffic
alone can block a site and that a turned receiving site moors, aligns and supports a load in
its own frame. `cargo_sandbox_crossing_screenshot` stages the helm part-way to the guided
next site; `cargo_sandbox_receiver-approach_screenshot` carries a lashed crate to the
package's receiving site, and `cargo_sandbox_delivered_screenshot` then moors and unloads it
onto the receiving pad with that site's crane, and requires the package's job to count it once,
build its hidden quay and jetty and supply load 2 on the quay's pad (`landing-built` captures
the result from the helm). `redelivered` sets load 2 on the landing, a native delivery the job
does not count, `job-reset` checks that R restarts cargo, job and the hidden sites together,
and `build-blocked` holds the build (keeping the job's checkpoint and event) while the boat sits
in the quay's envelope. S1d's second outing: `quay-pickup` moors at the quay beside load 2,
`jetty-approach` lifts, lashes and departs by G toward the job's guided jetty, and
`jetty-delivered` unloads there and requires `JETTY SUPPLIED: 1`. These job captures are in
the slow lane. `boat_cargo` checks the native delivery (once per load and receiver), loss,
recovery, hidden-site and supply rules, and that a crate at the deck's edge rests on it or is
pushed out of the hull's side, `boat_plugin.free_objective` the free job grant, and
`boat_plugin_state.work_commands`/`supply_commands` the `work.enable`, `cargo.supply` and
`work.guide` commands and the events' `load`. The `shadow-high`/`shadow-low`
capture cases stop actual lifts above the deck; `remoor` seeds a returning boat pose,
requests capture and advances the real application until moored, checking the terminal state.
CPU/GPU toy captures, windowed motion and
hands-on controls are separate checks; see the [owning sandbox plan](FREE_RIDE_SANDBOX_PLAN.md#s0a-try-the-cargo-toy).

`cargo_shadow_geometry` checks receiver-triangle clipping at a pitched/rolled bow,
footprint bounds, actual nonplanar water receiver planes, above-projector rejection and
empty results. `cargo_shadow_wave_cpu` drives 256 fixed virtual poses/phases and checks
that a visible footprint remains in the rendered image. Optional `cargo_shadow_wave_gpu`
(`PROTEUS_GPU_TESTS=ON`) repeats the checks on the real mapped GPU view and checks a
populated-to-empty dynamic mesh for stale vertices. These are visibility/state checks,
not performance targets. See the [wave-phase correction](FREE_RIDE_SANDBOX_PLAN.md#s0b-wave-phase-shadow-correction)
for baseline failures, verification evidence and the approval-before-PR workflow.

Marina M2: `harbor_geometry` checks appearance boundaries, canonical defaults, supported text,
glyph geometry and the unchanged triangle budget. `harbor_view` checks independent CPU mesh
owners, no-rebuild repeats, aborted candidates, 96 edit/remove cycles and customized contact/
camera bounds. `gpu_view_binding` also checks independent customized uploads, no-reupload
repeats, 32-slot capacity, removal and rollback after a forced second-upload failure.
`boat_plugin.scene_appearance` covers strict Lua decoding and PST9 round trips/rejection;
`boat_plugin_session.harbor_appearance_edits`, `boat_plugin_stage` and `wake_command` exercise
the actual session, isolated worker and console Apply/Undo paths. Room definition-size and
contact/prediction checks carry customized hulls; source/catalog equality covers bundled data.
The optional browser authoring checker now includes M2 name/dimension edits, Undo/export and
a large source reply. See [the owning result](MARINA_AUTHORING_PLAN.md#m2-implementation-status--2026-11-03)
for actual verification and remaining human/platform limits.

P4.9 `boat_plugin_eval` checks the bounded objective worker protocol against the native
transaction engine: canonical state, lossless wide integers, exact framing, capacities,
protected fields and stale/malformed replies. `web_plugin_author` includes virtual-time
objective transport reuse, cancellation, late watchdog/result and failure checks. The optional
three-engine `check_web_plugin_author.py` also runs `check_web_plugin_objective.py` for browser
init/update/timers, simulation gating, recovery events, migration/Undo/cancel/restart, failure,
timeout/freeze and repeated-session cleanup. Delivery timings and worker Wasm linear memory are
evidence, not asserted performance budgets; physical devices and process memory remain open.
See [P4.9](PLUGIN_PLAN.md#p49-result-2026-10-30).

P3.1 `boat_plugin_state.*` covers fresh-instance objective callbacks, canonical integer state,
atomic state/RNG/event/HUD commit, quarantine/reset, candidate migration, isolated checkpoint
replay, resource/event limits and budget failures. These are headless harness checks; they do
not assert a live objective or HUD integration. P3.2 adds `boat_plugin_session.lossless_events`,
`live_objective`, `objective_revision_failure`, `objective_overflow` and `objective_tick_parity`: simultaneous native
notices, exact T/T+1 output staging, optional arrival success/miss, recovery, immutable revision
source, source-only reload/Undo and quarantine/overflow without preventing native arrival.
A 1,200-tick repeated run also checks objective hash determinism and native boat/voyage/weather
parity with callbacks disabled. These use virtual ticks; HUD appearance needs a windowed review. See the
[objective API](PLUGIN_API.md#objective-transactions-p31--p32).

P3.3a adds `boat_plugin_state.timer_capabilities`, `timers_delivery`, `timers_atomicity` and
`timers_capacity_and_checkpoint`: grant rejection, deterministic due/ID ordering, checkpoint
resume, exact wide deadlines, migration retention, cancellation/re-arm, rollback and malformed
checkpoints. `boat_plugin_session.objective_timers_reload` drives production T/T+1 staging,
metadata rename, explicit restart/Undo and queue overflow with due timers. All use virtual time.

P3.3b adds `boat_plugin_state.schema_timer_retirement` and
`boat_plugin_session.live_migration`, `resource_composition`, `preview_seek`. They verify
schema/generation migration, candidate failure with pending output, HUD/timer retirement,
second-owner rejection, atomic namespaced composition and native checkpoint seek against
uninterrupted physics/presentation/player/traffic-wake and event/command traces. Seek equality
compares declared fields, excluding C struct padding; the fixture deliberately changes wake
padding and checks that an actual emitter-state change still fails equality. The developer
`boat_plugin_review` command exposes the same display-free checkpoint/inspection harness;
see the [API](PLUGIN_API.md#live-migration-and-review-p33b) for its configuration and limits.

P3.4 adds `boat_plugin_state.independent_budgets` and
`boat_plugin_session.memory_accounting`: separate entry/callback/migration instruction ceilings,
rollback/quarantine and full-source history/staging/Undo/eviction accounting. The explicit
`./build/boat_plugin_bench plugins/shelter-tour` experiment reports 100-sample staging/update
and failure-path costs; it is outside CTest and has no timing assertion. Native inspection
reports fixed owner reservation, retained source payload and allowed Lua heap separately.
See [runtime qualification](PLUGIN_API.md#native-runtime-qualification-p34) for commands and limits.

P2 plugin checks: `boat_plugin_race.*` covers descriptor parity/rejections, revision/Undo,
real-file trial controls with virtual time, and tick-for-tick four-boat fleet/event parity.
`boat_race_plugin.*` runs the existing eleven native race cases against the loaded Lua
reference. `boat_race_plugin_preview` renders a display-free CPU frame. See the
[plugin API](PLUGIN_API.md#river-race-authoring-p2).

`release_command` runs `tools/test_cut_release.py` in the full CPU suite on every platform.
It uses temporary local Git repositories and mocked external commands to check immutable
tags, detached worktrees, hosting baseline/push recovery, archive identity/integrity and
failure gates. Workflow polling uses a virtual clock. These tests never publish a release,
contact Winterfell or require controller/browser credentials. The
[release guide](RELEASES.md#release-command) owns the real operator command and live evidence.

`boat_time_control` advances the same 32 simulated ticks at all six live time rates and
compares the tick count and complete physical boat state with a fixed-tick reference; it also
checks rate bounds and the real-frame stall clamp before scaling. `boat_replay` covers practice
finishes with/without an existing best, preserved export files, replayable last runs and eligibility
after restart. All clocks are virtual. The [boating plan](BOATING_PLAN.md#play-testing-time-controls--2026-10-28)
owns implementation status and manual review limits.

The Linux, macOS and Windows Actions builds run on pull requests and pushes to `main`, skipping
events that change only Markdown files or `README.txt` files (including packaged READMEs).
Other changes run the bounded routine build/test lane, including mixed documentation/code pull requests.
The filter deliberately names `README.txt` rather than all `.txt` files so `CMakeLists.txt`
and other build inputs still trigger CI. Manual workflow runs and the macOS and
Windows release builds remain available regardless of these filters. GitHub does not create a
successful check for a skipped workflow; do not require these checks for documentation-only pull
requests covered by these filters.

### Routine CI budget

Routine native PR and `main` jobs have a **three-minute job timeout**, including checkout,
dependency setup, configuration, compilation and tests. Browser authoring PR jobs have the
same cap. GitHub queue/runner allocation time is outside the job timeout. Each native test
step has a one-minute cap, fails on the first failure, and writes JUnit results. Older PR
revisions are cancelled when a newer revision is pushed; release jobs are not cancelled.
A timeout is a failing check, not permission to retry or increase the budget automatically.

Build the same lane locally:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target ci -j
ctest --preset ci
```

The `ci` target builds the game and selected validators, then checks the actual discovered
CTest lane. `cmake/CI.cmake` owns the selection: fast boat handling, codec/session protocol,
Lua descriptor/state/catalog, room-package, cargo, save/file/input and lighting checks;
source/library boundaries; and one basin plus one mapped CPU asset-loading capture.
Only quick cases from the selected validator families are promoted. The guard rejects fewer
than 50 or more than 150 checks, missing essential checks, and disabled/GPU/exhaustive cases.
Its own test covers those failure paths. The local preset's ten-second timeout is CTest's
fallback; the hosted one-minute test-step cap is the total test budget.

Routine jobs skip exhaustive route/physics runs, repeated captures, Python authoring preview
integration, benchmarks, desktop-startup qualification and package/relocation/signing checks.
Those tests remain in local `ctest --preset full`. Native manual dispatch runs the existing
broader qualification paths; tagged release calls retain macOS/Windows packaging and their
existing longer timeouts. Linux manual dispatch retains the full CPU suite, benchmarks and
Wayland/X11 smoke checks. Browser manual qualification keeps its three-engine checks and
30-minute limit. PR/main jobs no longer produce native download packages; use release or
manual qualification when a package is needed.

Windows routine builds use Ninja with the same MSVC compiler; manual/release qualification
keeps the Visual Studio generator. Windows routine Release builds use `x64-windows-static-release` to avoid compiling unused
Debug curl/zlib variants. ABI-checked vcpkg binary archives are restored/saved around dependency
installation, keyed by runner image, vcpkg revision and triplet contents. They are saved after
installation even if later tests fail. Manual/release packaging retains the standard
`x64-windows-static` triplet. Cold dependency/setup timings still need hosted validation;
the timeout alone does not prove every cold run fits. Keep setup/build costs inside the
three-minute budget when changing dependencies. Implementation/evidence: [CI plan](CI_PLAN.md).

The full/manual-only `boat_plugin_author` deterministic-preview integration check runs each package twice
for 360 virtual ticks. Each preview child has a 30-second failure deadline, and the whole
CTest has a 180-second deadline to allow Debug runs alongside numerical validators. Package
subtests identify failures. These limits do not advance simulation time or change the
production authoring deadline; result and artifact equality remain required.

### Lua source availability

The checkout includes the unmodified Lua source archive in `third_party/lua`. The shared
`cmake/LuaSource.cmake` declaration extracts that local archive with the published SHA-256
check, including in fresh native and browser builds. Lua requires neither lua.org access
nor an Actions cache. Existing `FETCHCONTENT_SOURCE_DIR_LUA` overrides remain available.
The archive's provenance, license and upgrade procedure are in
[the vendor guide](../third_party/lua/README.md).

Raylib/ENet Git fetches, libcurl and SDKs still require their existing setup/network access.
Older release tags retain their original Lua fetching behaviour. The browser authoring path
filter includes both the Lua declaration and archive so dependency updates trigger its build.
Implementation/evidence: [dependency availability plan](DEPENDENCY_CACHE_PLAN.md).

2026-11-03 workflow filter follow-up: PR #135 included `tools/windows/README.txt` and therefore
started all three native builds under the former Markdown-only filter. The push/PR filters now
exclude plain-text READMEs too. Completed: aligned Linux/macOS/Windows filters; release, manual
and reusable triggers remain unchanged. Next: verify skipped native CI on a subsequent docs-only
PR. Local trigger checks cover the #135 file list, root/nested Markdown and READMEs, mixed code,
`CMakeLists.txt`, script and workflow changes. On macOS, Debug configure/build and
`ctest --preset full` passed all 351 enabled tests (13 disabled, `PROTEUS_GPU_TESTS=OFF`),
with socket access for network tests. GitHub skip behavior awaits a new docs-only event.

Multiplayer M2's local authority and M3's protocol and transport have display-free checks
(see [below](#core-boundary-gate) and [network checks](#network-checks)). The M3 suite covers
the codec, sessions over a seeded in-memory network (including profile F one way) and a real
ENet loopback process test. Still [planned](MULTIPLAYER_PLAN.md#verification-matrix-and-telemetry):
the full impairment matrix, an OS-level impairment lane, prediction/collision feel gates and
long-session/four-player stress.

Status, 2026-10-22: the user requested a quick routine suite and suspension of the
retired physical wake-field tests until that research resumes. Arcade wakes, handling,
course/replay, environment, audio and renderer checks remain active.

The cosmetic player skin check `boat_skin` runs `validate_boat_skin`: matching geometry
across liveries, valid triangles, custom palette generation, footprint and ID fallback;
flag field/cross colors (the explorer's too), Norway's white border and independent render storage.
It is device-free; visual review commands live in [the skin guide](BOAT_SKINS.md).
`boat_fleet_render` runs `boat_fleet_review --check`: a four-boat fleet drawn from slot 0 with
three rivals through the shared scene call; each rival must change the CPU image, a repeat must
be identical and drawing must not change the fleet or presentations. For a look, run
`./build/boat_fleet_review --renderer gpu --waves --time 4 --screenshot build/fleet.png`.

`boat_handling` (`validate_boat`, `cpu`) also checks view-owned water-mesh caches
(adventure A1.3). Two river caches at different patch centres and a basin cache are held
together, one is destroyed and rebuilt, and a cache rebinds between the river and a synthetic
pond map. It also checks the water index (A2.4): indexed patches equal full-scan ones around
index seams and corners, and a view-bounded CPU surface is an ordered subset of the full one.
Every mesh must match its reference built alone, and the 5.99/6.01 m snap-boundary
pair must not disturb another cache's borrowed meshes.

`boat_world_isolation` (`validate_boat_world`) also checks CPU view binding (adventure A1.4):
two river views and a view of a synthetic harbor map draw interleaved, and destroying one view
or rebinding another reproduces every frame exactly. `gpu_view_binding`
(`boat_water_review --check-gpu-views`, `gpu`) does the same with two GPU views,
including destroy and recreate with the context alive.

Adventure A1.5 adds the lifecycle gate. `boat_world_isolation` runs five scripted owners
(basin, two river owners with different water, camera and steering, the synthetic harbor and a
second basin), each with its own world resources, session, CPU view and recorded run. Each runs
drive, respawn, restart (the basins start the course first) and a replay viewed from a playback
world on the same owner, with the live boat as its ghost. Every phase's physics and frame
hashes must match the owner's solo reference while one slot reloads basin → river A → basin,
river B and the harbor run alongside, and A is released mid-run. A failed init over a stale
owner and double destroys are checked too. `boat_map` releases a fleet's owner and checks that
a fleet on a surviving owner continues like one restored onto a fresh owner, and `boat_net`'s
`prediction_owners` releases one room's owner while the other prediction replays exactly.
`gpu_view_lifecycle` (`boat_water_review --check-gpu-lifecycle`, `gpu`) renders two
private river owners (one with an arcade world, so both patch caches build), the harbor and the
basin. Shared views must reproduce the fresh-view references interleaved, after the first
owner and its view are destroyed, and over three view recreate cycles with the context alive.
It also resizes a view through portrait, wide and default targets with smoothing off/on;
returning to the default target must reproduce the exact original pixels. Invalid dimensions
must leave the existing renderable targets intact.
`tile_culling` (`boat_water_review --check-culling`, `gpu`, about 9 s; adventure A2.3)
renders 144 river poses (512 m and 1 km tiles, six route stops, four headings, default, close
and overview cameras) culled and unculled, on the GPU with waves and on the CPU on flat water.
The CPU pixels must be identical. The GPU images may differ by at most 1/255 in at most four
pixels, the noise measured between two identical culled draws (6 of 48 runs, one pixel, 2026-11-01).
Every pass must cull at least one tile.
`detail_transitions` (`boat_water_review --check-detail`, `gpu`; graphics G2b) drives a river view
through the distance-detail threshold. Orbiting the boat must change no level, a cell that just
switched must hold under small camera movement, repeated out-and-back trips must end alike, and
every cell with windows must keep them far. It prints the switch counts and near/far cells.
`instancing_parity` (`boat_water_review --check-instancing`, `gpu`, about 6 s; graphics G1) draws
every G0 review pose and six stress frames scalar, instanced and instanced with a replay every
three objects, with AA off and 4x4. The main image, reflection and both shadow depth maps must be
byte-identical, with equal instances and triangles and fewer submissions.
`wake_stamp` (`boat_water_review --check-wake-stamp`, `gpu`, about 3 s; graphics G3a) reads
the stamped wake field back at three moments of the saturated four-boat fixture. It compares the
field with the CPU pieces at 200,000 fixed world points against the
[predeclared bounds](BOATING_PERFORMANCE.md#predeclared-accuracy-bounds), and checks that the
listed outside pieces equal both pools beyond the field. Presented stamped and analytic
captures must stay within the image bounds. It also checks that an identical redraw reuses the
field, and that a teleport, rewind and new context each rebuild a correct one. The analytic
switch must draw no field.
`water_history` (`boat_water_review --check-water-history`, `gpu`, about 3 s; graphics G3b)
drives the foam history and ripples with fixtures on irregular frame times and reads the state
back. Two histories given the same inputs must match bit for bit, and so must a reset history
and a fresh one. A repeated time must run nothing and leave the state alone. A quarter-second
gap may run only the catch-up limit; a larger jump, rewind, teleport or content change must
reset with pre-roll. The shore mask must be open over a water rectangle and closed beyond it,
with no foam or height on closed texels or inside the hull disc. Stamped foam must halve within
1.5 times its half-life and fall below 5% within 5 s. Scrolling one block must move foam exactly
with the world and leave the new strip empty. A minute of rain, a bobbing contact and a fast
hull must stay finite and within the height limit, with peak energy in the last 15 s at most 1.5
times that of the 15 s before. Without float targets nothing runs. In the stress scene it checks
resets and substeps, that history changes the image, and that drawing leaves the physical state
unchanged.
`scene_depth` (`boat_water_review --check-scene-depth`, `gpu`, about 1.5 s; graphics G4) draws
the outer dock with and without the opaque depth copy. The copy must be taken inside the render
texture and skipped when disabled or drawing to the default framebuffer; the outer map must bind
its water atlas. The quay band must change between 100 pixels and 5% of the frame, and almost
only lighten.
`presentation` (`boat_water_review --check-presentation`, `gpu`, about 3 s; graphics G5a) draws
the river at low and high resolution with and without HDR. Daytime HDR stays within one 8-bit step
of the 8-bit scene; repeated preset and window-size changes, a second view and the rear-view
mirror reproduce their frames; a same-wave-time light seek refreshes actual CPU and GPU mirror
pixels and returning restores them; failed HDR allocations present the 8-bit frame exactly; a
synthetic 3.5 spot blooms only a few pixels and clamps; and the HUD overlay composites like
drawing over the scene. `boat_screen` and `waterfront_grade` (quick) check the layout sizes and
the grades.
`light_clusters` (`boat_water_review --check-light-clusters`, `gpu`, about 1 s; graphics G5b)
draws the river at dusk, without and with 600 review lights, at low and high resolution. Each
clustered frame must equal `--light-reference` (every light tested at every pixel) byte for byte,
also after a rear-view mirror pass, with no dropped, clipped or overflowing lights; the daytime
fallback frame stays within one 8-bit step of the clustered one; and the capped fallback must
account for every light it leaves out. `waterfront_lights` (quick) checks the cluster slots, rank
order, submission-order independence, overflow, capacity, packing and, at 20,000 points, that
each slot list finds exactly the lights reaching the point.
`water_atlas_river` and `water_atlas_outer` (`water_atlas_bake --check`; graphics G4) rebake each
map's water atlas and require the committed sidecar byte for byte. `validate_boat_scenery`
also rejects malformed, truncated, foreign-map and out-of-range atlases.
`ambient_river` and `ambient_outer` (`ambient_bake --check`; graphics G5c, slow: about 1.5 s and
20 s) rebake each map's ambient occlusion and require the committed sidecar byte for byte;
`validate_boat_scenery` rejects malformed, truncated, stale-key, foreign-map and wet-corner
sidecars and one loaded without its scenery, and `gpu_view_lifecycle` requires the river's
occlusion to reach its uploaded vertices. `boat_handling.particle_lifecycle` (slow) pins the
cube foam and spray signature through emission, a fast turn, a 2 m drop and expiry within
declared ranges, checks that posing is pure and that the boat is identical whatever the
presentation holds.
`boat_water_review --geometry` prints each map's static GPU bytes (graphics G2a) and
`--compare A B` checks two `--review` directories against the G0 image bounds. Neither is a CTest:
review captures compare only within one platform and build.

The original HUD/control worktree checks below remain legacy numerical fixtures; live pedal
controls are verified in [the current controls section](#pedal-controls-verification).
It also checks all 49 transitions between the seven thrust detents
through the real integrator in `boat_handling`, and forward/reverse target settling on both
the authority and predicted client in `boat_net`. Mouse steering adds 49 wheel transitions
with both float and quantized rates, sustained target holding and virtual network target
checks. Relative mouse mapping covers accumulated movement and lock reversal; scroll thrust
covers fractional/multiple-step input, neutral crossing and overscroll reversal; held-key thrust
covers the 0.5 s repeat, re-press and limits. See [the HUD guide](BOAT_HUD.md) for power
settings, visual fixtures and the remaining manual checks. When Node.js is installed,
`web_mouse_controls` runs the browser event-contract tests without a browser.
`web_console_controls` (`cpu;quick`) checks console clipboard writes, paste events, focus and
queued Tab/Shift+Tab input; run it directly with `node tools/test_web_console_controls.cjs`.
`wake_command` covers the headless editor, completion and command semantics. Real Wasm
checks use the opt-in [Playwright command](WEB_BUILD.md#mouse-and-scroll-qualification-2026-10-24).
`web_rtc_transport` checks browser network bounds and reconnect callbacks; `boat_net_mux`
checks colliding peer IDs, routing and disconnect isolation across two transports. Browser
title/persistence and mixed WebRTC/ENet gameplay use the opt-in
[title and multiplayer checks](WEB_BUILD.md#webrtc-rooms), with the optional native endpoint.
`web_server_directory` checks fetched known-server schema, version matching, refresh and
failure behavior under Node. The opt-in [directory browser check](WEB_BUILD.md#known-browser-servers-2026-10-26)
joins the named room without typing and can simulate a failed list with manual fallback.

## Lanes, cases and timing

Every test carries exactly one lane label. `quick` tests take under about 100 ms on their own;
`slow` holds everything heavier: long sweeps, soaks, the process smoke and every GPU test.
Use the quick lane while editing and the full suite before handing off:

```sh
cmake --preset dev            # Same as: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --preset quick          # Under 1 s: 140 tests on an M1 Max (2026-10-26)
ctest --preset full           # About 11 s without GPU tests; everything enabled
ctest --preset full -L core   # Presets combine with the usual filters and -j N
```

Both test presets use 8 jobs; pass `-j N` to change that. Plain `ctest --test-dir build` still
works. GPU tests share `RESOURCE_LOCK gpu`: they run one at a time, but CPU tests keep running
beside them. CTest also runs the fixture setups a selected test requires, so a quick run can
include a slow setup such as `boat_replay`.

**Case tests.** The heavy validators are tables of small cases (`tools/boat_test_cases.h`).
`proteus_add_case_tests(prefix target LABELS ...)` in `CMakeLists.txt` lists a target's cases
after each build (`target --list`) and registers each as its own test, `prefix.case`, so cases
run in parallel and a failure names the case. A case table entry is `TEST_CASE(fn)` or
`SLOW_CASE(fn)`, where `fn` returns 0 on success. The binaries keep their old command line:
without arguments they run every case and print their `=ok` line. Each case prints
`case=<name> ms=<t>`.

```sh
./build/validate_boat_banking --list
./build/validate_boat_banking --case sides
ctest --test-dir build -R '^boat_banking\.' --output-on-failure
```

Case tests: `wake_objects` (`wake_objects_review`), `boat_handling` (`validate_boat`),
`boat_banking`, `boat_world_isolation` (`validate_boat_world`), `boat_map_index`, `boat_clock`,
`boat_net`, `boat_environment`, `boat_fleet`, `boat_race` and `boat_map`. Their tests are named
`<prefix>.<case>`; for example, the former `wake_objects_contracts_jumps` is now
`wake_objects.contracts`, `wake_objects.jumps` and so on. Whole-program tests heavier than the
quick budget are listed in `PROTEUS_SLOW_TESTS`; configure warns about a name that matches no test.

**Keeping cases fast.**
- Physics-only checks use `physics_session()` or `physics_basin_session()`
  (`tools/boat_test_session.h`), which turn presentation off. The flag cloth was about 75–90% of
  a session tick. `boat_world_isolation.presentation_toggle` proves that physics stay
  bit-identical without presentation.
- `test_world(id)` prepares a location on first use, so a basin-only case never builds a mapped
  world. `TEST_RIVER` (`tools/boat_test_location.h`) selects the Eriksberg river crop, which is no
  longer a location (retired 2026-11-04) but remains the small mapped fixture for map, physics,
  scenery, harbor, traffic and water-review checks; race, room and network checks use `outer`.
- A sweep can stop once its asserted end state holds. It keeps a hard cap and a fixed length
  wherever the length is the contract.
- A large sweep gets a sampled quick case and full slow cases, for example
  `boat_map_index.sampled_queries` and `boat_map_index.lattice_queries`.

### No wall time

No test may wait for time to pass. Code that depends on time takes `now` as an input, and its
test drives a virtual clock: the room harness steps `boat_net_server_service(server, now)`, and
sessions step whole ticks. Sleeping, fixed-duration runs and decisions on elapsed time make
tests slow and flaky, and usually mean state that has not been extracted yet. Wall-clock limits
appear only as failure deadlines (a subprocess `timeout`, or a wait that returns as soon as its
event arrives). Measured costs may be printed but never asserted.

`test_time` (`tools/check_test_time.py`) enforces this. It scans the validators, the test
headers, every script CMake registers as a test, and the `add_test` commands. It rejects
sleeps and waits, wall-clock reads in test scripts, and `--seconds`/`--frames` runs. A wait
bounded by a deadline must say so in a `deadline` comment on its line. `*_rejected` argument
checks are exempt. The one listed exception is `wake_velocity_host_gpu`, whose windowed frames
follow the display.

**Measuring.** `tools/test_timing.py` ranks a CTest JUnit report. It prints buckets (≤20 ms,
≤50 ms, ≤1 s, >1 s) and the quick tests over a budget (`--budget`, default 0.1 s). `--enforce`
makes it exit non-zero for those. It is not a CTest test, so machine load cannot make the
suite flaky; times under `-j` contention run high.

```sh
ctest --test-dir build -j 8 --output-junit test-speed/run.xml
python3 tools/test_timing.py build/test-speed/run.xml --top 20
```

On macOS, the first launch of a freshly relinked binary can take 0.3–0.7 s longer while the
system scans it, so the first run after a build is slower. Adding the terminal app under
System Settings → Privacy & Security → Developer Tools removes that delay.

## Pedal controls verification

The isolated controls branch adds all eight pedal/precision key combinations, 0.25-second
command response, precision transitions, coasting versus reverse braking, reset cancellation,
one-trigger-per-hold and invalid/stalled-frame cases to `boat_handling`. `boat_net` verifies
quantized target settling within one command quantum and deliberately seeds an inverted
checkpoint so safer steering does not remove capsize replay coverage. The older calm-water
reference scales its authored rate input to preserve its physical travel and hydrodynamics.
Existing detent/scroll helpers remain numerical/review fixtures, not live controls.
[Browser pixel checks and integration evidence](CONTROLS_PEDALS_HANDOFF.md).

## Routine checks

Boating music checks are `boating_xm_is_valid` (A little boat) and `acid_xm_is_valid`
(Acid Wake): fresh decoder startup, seeking, finite unclipped PCM and two loop boundaries.
The Acid Wake check renders 235 seconds at 132 BPM. These do not establish listening
acceptance or menu interaction; see [boating music evidence](BOATING_PLAN.md#selectable-soundtracks--2026-10-24).

The [arcade stability candidate](ARCADE_STABILITY.md) extends the `wake_objects` cases
with held-turn automatic stabilization, neutral/held recovery equivalence, dry-flight momentum and inverted
capsize checks. Its encounter envelope covers both the earlier 1.8 m stress setting and
the new 1.2 m default; the old 1.8 m geometry/derivative fixtures remain explicit stress tests.
`boat_clock` retains per-tick equality across start times, with emission/landing/expiry
milestones rather than a physical-recipe-specific birth count. `boat_net` reports smooth
and contact-adjacent remote errors separately: below 5 cm in smooth motion, 15 cm within
four ticks of a recorded pair impact, and below 5 mm mean. Snapshot interpolation cannot
reproduce the precise tick of an impulse; [candidate evidence](ARCADE_STABILITY.md) records
the observed outliers. This does not change remote rendering or protocol bytes.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --preset full
```

`PROTEUS_WAKE_FIELD_TESTS` defaults to `OFF`. Fifteen registered experiments are marked
disabled, covering the owned field, refinement, cadence, candidate sources/boats/GPU,
compact packets and Python prototypes. CTest lists them as disabled, not failed. Their
sources, targets and manual diagnostic commands remain available. All arcade-object tests
stay enabled. With `PROTEUS_GPU_TESTS=ON` and Node.js available, this HUD/control worktree runs 103 active
checks as of 2026-10-24 (including the browser event contract; counted by the current CTest run;
historical, before the case split). GPU cases run one at a time under `RESOURCE_LOCK gpu` while
independent CPU checks run concurrently; fixture dependencies still govern capture/replay comparisons.

The river frame benchmark has two checks in `tools/validate_boat_water_benchmark.py`.
`boat_river_benchmark_options` (`cpu`) checks option and output-path rejection without
opening a window. With GPU tests enabled, `boat_river_benchmark_contract` (`gpu`;
about 32 s in Debug on an M1 Max) runs a hidden `--waves --aa off` voyage. It checks row
counts, warmup flags, tick continuity, recomputed percentiles, deadline counts, finite values,
route completion and exact final-state parity with `--check --waves`. Since A2.1 (schema 2)
it also checks that the per-pass and per-group submission/triangle columns partition the
totals, apart from the two sky quads. Since A2.3 (schema 3) it also checks the default
`tile_size` (1,024 m then, 2,048 m since the outer follow-up), the per-pass `*_tiles_culled`
columns and that the shadow pass culls tiles on every row (on more than three quarters of rows
since the 512 m detail cells). Since G0 (schema 4) it also checks the `stress` scenario: the display-free
`--stress-state` fixture must match the benchmark's exact `final_state`, and both wake pools
must stay at capacity with reported emitter/piece counts. It asserts no timing thresholds.
The `outer` scenario (2026-11-04) drives the river route on the outer map and continues across
the estuary; its GPU run is not in this contract (about 12,000 frames), but `boat_outer_route`
(`cpu`, about 1.7 s in Release) drives it display-free with `--check --scenario outer --waves`,
checking shore clearance on every tick. The benchmark's `final_state` matched it on the run
that introduced the scenario.
See [the benchmark guide](BOATING_PERFORMANCE.md#river-frame-benchmark--2026-10-25) and the
[G0 budgets](BOATING_PERFORMANCE.md#graphics-g0-measurement-harness--2026-11-01).

`boat_lighting_qualification_contract` (`cpu;slow`, about 11 s in Debug on an M1 Max) runs the
small deterministic `--lighting-qualify` stress audit in `tools/validate_boat_lighting_qualification.py`.
It rejects out-of-bounds sample counts, CPU high/HDR, `--daylight`, `--aa`, review/check mixing
and unusable prefixes before a window. The 25-sample run must exclude eight warmup rows, span a
virtual 900 s cycle with every hour bin and midnight wrap, hold the fixed hour, match the two
runs' final physical state, and produce a parseable JSON/CSV with recomputable finite rows. It
asserts no frame-time threshold and does not replace supervisor fixed/cycle Release, GPU and
visual measurements.

`boat_capture` (`cpu`) owns the bounded live-capture contract: ring ordering/eviction, the
retained window's span, segment separation, the percentile convention, missing GPU samples and
directory/ZIP export without overwriting. `performance_analyzer` (`cpu`) runs the stdlib-only
Mac comparison tool over synthetic fixtures; it covers compatible/incompatible settings,
ZIP round-trips, schema/sample/NaN bound rejection and unsafe archive members. The fixtures
contain no hardware or personal capture. Neither test is timing-sensitive.

`frame_pacer` checks the window frame pacer's deadline chain on synthetic clocks: exact
period boundaries despite oversleep, repayment of a late frame, restart after a stall, and no
waiting under a VSync-rate backstop. Option checks also reject a stray or unknown
`--pacing`. Real cadence needs a visible run; see
[frame pacing](BOATING_PERFORMANCE.md#frame-pacing--2026-10-25).

Every active test carries one of three labels: `cpu` (display-free), `render` (CPU
captures and checksums) or `gpu` (graphics context). Skin geometry carries both `cpu` and
`render` labels. The `core` checks are also
`cpu`:
- `boat_core` runs `validate_boat_core`, which links only the boating core library.
- `boat_plugin` runs `validate_boat_plugin` (Lua plugins P0, twelve quick cases, about 0.5 s
  together in Debug). The reference package `plugins/langedrag` must decode field for field and
  bit for bit to `boat_voyage_outer`, with the same digest. It also checks a definition computed
  with the allowed libraries, syntax, runtime and bytecode rejection, and every forbidden
  facility. Invalid fields must report their path, as must bad manifests and host limits.
  Memory and instruction exhaustion must stop with zero leaked bytes, and repeated failures
  must leave a last good definition unchanged with identical diagnostics. Package files are
  read from a build-tree scratch directory, where missing files, an escaping entry, a symbolic
  link, a directory and oversized input are refused. Finally the decoded shelters must lie in
  the outer map's water. Instruction budgets are counted by a hook, never timed.
- `boat_plugin_session` runs `validate_boat_plugin_session` (Lua plugins P1/P3; bounded quick cases
  of about 37 ms together in Debug, plus a slow parity case of about 0.2 s). It covers staging
  against the outer map with field paths, and the classification of renames, live route/layout
  changes, water changes and other plugins. It applies each policy to a live `BoatSession`: a
  route change maps progress by shelter ID with the boat kept, while a removed reached shelter
  falls back to the start; a water change still restarts. Broken stages must leave the active
  revision byte-identical, and Undo is checked over the eight-deep history. The save poller runs
  on a virtual clock. `game_controls` drives the game's `--plugin` F5/F6/F7 and polling glue on
  files in a build-tree scratch directory. `compiled_parity` steps a compiled and a plugin
  session with identical input to Långedrag and compares the boat, the water and every voyage
  field at each tick. The `wake_velocity_plugin_*` tests render a CPU frame with the plugin and
  check that `--plugin` is refused with a room, the course, another location or a missing package.
  `wake_plugin_objective` runs `validate_wake_plugin_objective`, which compiles the game's browser
  objective policy natively (`WAKE_PLUGIN_ISOLATED_OBJECTIVE=1`) over a C model of the page
  bridges and checks, on virtual time, a reply first accepted exactly at its 5 s deadline
  (rejected) and just before it (accepted, published at T+1), stale and failed receipts, BUSY
  controls, and reset/close cleanup over repeated sessions.
- `boat_plugin_catalog` (PD1/PD5) decodes every generated entry and proves it equals evaluating
  its embedded source, that the basin-free, outer-voyage and time-trial defaults equal
  the shipped `proteus_boat_fixtures` values, and that catalog decoding creates no Lua state (the
  `boat_plugin_lua_states` counter is unchanged). `boat_plugin_session.catalog_runtime` opens the
  outer voyage and Harbor life entries and checks that no Lua state, watch, staging worker or
  authoring panel appears. `boat_plugin_session.default_sessions` opens every default through the
  game's catalog selection and compares its route, course, rules and scene with the fixtures.
  `boat_plugin_catalog_invalid` runs the build-time generator on objective, bad-schema and
  duplicate packages and expects its normal diagnostics. `compiled_selection_guard` rejects the
  removed compiled symbols and the fixture library in production sources, CMake targets and the
  built `wake_velocity`/`boat_server` symbols; `compiled_selection_guard_detects` is its negative
  control, where a validator linking the fixtures must trip the executable check. `boat_replay`'s legacy-best case proves
  `course-calm-physicsN-wakes.run` is the best under `BOAT_COURSE_STANDARD_DIGEST` and is not
  overwritten by an equal or slower run, and that a plain-water best under the older
  `course-calm-physicsN.run` name is left on disk and never selected. The catalog
  also includes the outer free room, `proteus.deathmatch` (whose rules and combat values equal
  the fixtures at every location) and the two harbor room packages.
- `boat_room_package` (PD2/PD3) evaluates edited race and deathmatch packages, builds rooms from
  their definitions and compares every client's mirror hash with the authority at snapshot
  ticks; predicting clients must correct and stay within the prediction bounds. It also loads
  every bundled free/match/race package through the authority path, validates the authored scene
  against the room's spawn and course, and measures each room definition and a maximum scene
  against the 16 KiB cap. `boat_net.room_definition_codec` covers the fieldwise definition,
  truncation, trailing bytes, non-finite values, mode/location/rule faults and paging.
  `boat_room_course_source` rejects any compiled `boat_race_river_course` reference in the
  networked client files.
- `boat_map` runs `validate_boat_map` (adventure A1.1/A1.2). It checks explicit map
  descriptions, caller-owned collision caches, failed/zeroed preparation, two river owners and a
  synthetic map alive together, and destroy/recreate. It also matches a river query digest
  recorded before the ownership change. `BoatWorldResources` lifecycle and validation are
  covered too. Worlds on separate owners step interleaved with bit-identical results, and
  fleet copies and checkpoint restores keep borrowing their owner.
- `boat_map_index` runs `validate_boat_map_index` (adventure A2.2). It
  compares the indexed collision queries at 8, 16 and 32 m cells with the exhaustive scan,
  bit for bit, on about 3.6 million queries. These cover a cell-seam lattice, shore offsets,
  tile seams one float either side, tile corners, random and far points. It checks the 512 m
  and 1 km tile catalogues' lists, land ownership and bounds, and reports index, tile and
  candidate counts. Since A2.3 it also checks that the renderers' land-only catalogue equals
  the full one, and that land-only construction rejects bad input. The quick
  `sampled_queries` case compares every 512th query of all families (about 70 ms); the slow
  `lattice_queries`, `shore_queries`, `seam_queries` and `random_queries` cases compare all of
  them (about 13 s together in Debug).
- `boat_map_scaling` runs `validate_boat_map_scaling` (adventure A2.5, about 5 s in Debug, `slow`). It
  copies the river into synthetic corridors of up to 16 copies (25 km at 5), and requires the
  first copy's collision answers, tiles, water patches and windowed CPU water to be identical
  at every size, visiting the same candidates, with a water cache that does not grow. Run it
  with `--report` for build costs, per-operation times and memory per owner.
- `boat_outer_map` runs `validate_boat_outer_map` (adventure A3.1, about 1 s in Debug). It loads
  the baked first outer-river slice (`assets/geometry/outer/`, regenerated offline by
  `tools/prepare_outer_map.py`), requires every A0.3 route waypoint and sampled leg to be water
  with the 8 m envelope, compares the indexed queries with the exhaustive scan on 20,000 points,
  and checks that points outside the region and on the quay are not water. Since A3.1b it
  also requires real relief: land tops never below 1 m, hills between 60 and 100 m, and at
  least 100,000 triangles.
- `boat_scenery` runs `validate_boat_scenery` (adventure A3.5i, about 0.3 s in Debug, `slow`). It
  checks the scenery packs under `assets/geometry/`: nothing is drawn before a load; cut packs,
  wrong magic or version, wrong counts, a moved bridge range and the river pack offered as
  `outer` are all refused; a good pack loads once with water identical to the compiled
  `water.h`; `outer` needs the river pack for its kit ground, and loading the outer location loads
  that pack (without the crop's sidecars); and unload clears them all. The
  map validators and review tools load the packs from the source tree
  (`tools/boat_test_scenery.h`).
- `map_pack` runs `tools/map_pack.py --self-test` (quick): float32 rounding, the leading-water
  count and a pack round trip.
- `boat_minimap` runs `validate_boat_minimap` (quick). It requires a chart for `outer`
  and none for the basin, checks that both frames fit the 320×200 scene (the chart
  above the instruments), that the spawn, every voyage shelter and the destination are water
  pixels and inland Hisingen is not, that composition marks the boat and destination without
  touching the trim, and the N cycle. With arguments it writes review captures
  ([HUD guide](BOAT_HUD.md#review-commands)).
- `boat_voyage` runs `validate_boat_voyage` (adventure A3.4, under 1 s in Debug). It checks that
  the outer voyage's shelters and harbour are navigable, that the exposed patch crosses the A0.3
  route's southern leg while a coastal line, the shelters and the harbour stay outside it, the
  shelter/exposure/arrival/recovery events, the bearing cue's sign against real right steering,
  and two autopiloted sessions (coastal and direct, from the fjord turn) that must arrive at
  controlled speed. It prints both times, the exposed seconds and any capsizes.
- `boat_core_boundary` runs `tools/check_core_boundary.py` over the core sources and archive,
  and `boat_net_boundary` does the same for the core and net libraries together.
- `platform_bridges` runs `tools/check_platform_bridges.py`: inline JavaScript (`EM_JS`,
  `EM_ASM`, `emscripten_*run_script*`) lives only in CMake-selected `*_web.c` backends or in the
  listed legacy files, each at exactly its recorded count; files migrated onto an application
  service (title, game, input, console, plugin glue) do not reach its backends directly; and
  `CMakeLists.txt` names `*_web.c` sources only in EMSCRIPTEN branches or in its allowlisted
  native test targets (`validate_boat_plugin_stage_web`). `platform_bridges_detects` is its
  negative control.
- `plugin_stage_backend` runs `tools/check_plugin_stage_backend.py`. With comments and string
  literals blanked, common plugin staging (`boat_plugin_stage.c` and its headers) includes no
  OS/browser header and names no listed process, handle, Emscripten or platform-macro
  identifier. Each platform backend includes none of a listed set of other platforms' headers,
  only staging, its backends and `validate_boat_plugin_stage_web` include the private backend
  header, and `proteus_plugins` lists each backend once. `plugin_stage_backend_self_test` (slow
  lane) plants each violation in a temporary copy and checks that it is caught.
- `boat_fleet` runs `validate_boat_fleet` (multiplayer M2 fleet step, contacts, spawns and
  checkpoints), and `boat_server_scripted` runs the scripted authority for 2400 ticks.
- `boat_race_trial` runs the [R0 ferry fixture](RACE_PLAN.md#r0-prototype--2026-10-24):
  source derivatives, bounded support/clock translation, source hull clearance/contact,
  four-emitter fleet restore, speed/angle/player-wake overlap and timed route/arrival sweeps.
  `boat_race_trial_gpu` samples both production GLSL source recipes against CPU height,
  slopes and vertical velocity when GPU tests are enabled, then verifies the mapped ferry
  contributes pixels to the main, reflection and shadow passes with identical physical water.
  It also samples both production storm recipes against CPU height, gradients and vertical velocity.
  The fixture also checks independent physical scenery emission/restore and visible trail
  bounds; `wake_objects_gpu` checks both object pools beyond slot 127, independent lifetimes,
  exact uploads and 4/8 KiB texture-resize round trips.
- `boat_race` runs `validate_boat_race` ([race R1](RACE_PLAN.md#r1-race-core--2026-10-24)):
  skill-gate endpoints and centre/outer-lane shoreline clearance, a full-width finish,
  mandatory line changes through consecutive openings (including edge cuts), compact storm
  support, analytic derivatives (including patch translation), clock translation and storm
  rendezvous/speed, swept/reverse/lateral/multi-gate crossings, skipped-gate
  finish rejection, held grid through a full-throttle countdown and a same-tick release,
  same-tick places and exact ties,
  recovery behind the last cleared gate, joiners, leavers, abandonment, deadlines and grid
  rotation, and finisher parking (R2). It ends with a four-bot ferry race with a mid-race
  checkpoint restore; finishers must take 45–85 s, and one bot may DNF (see below).
- `boat_match` runs `validate_boat_match` ([deathmatch](DEATHMATCH_PLAN.md) credit window,
  self-capsize, grace, respawn, bailout, leavers, rounds and planted-wake capsizes), and
  `boat_server_scripted_deathmatch` runs a scripted 10 s round through its intermission.
- `boat_net`, `boat_net_enet` and `boat_net_process` are the
  [network checks](#network-checks).
- Fourteen argument checks expect a failure: `boat_server --scripted` with a room option or
  `--until-empty`, an invalid `--port` and an invalid `--bind`, `--round-seconds` without
  `--mode deathmatch`, `boat_bot` without a port, `boat_bot --discover` with `--connect`,
  `boat_bot --snapshots` with `--seconds`, `wake_velocity --connect` combined with `--waves` or `--skin`, `--name`
  without `--connect`, `wake_velocity --host race --location basin`, `--host` with `--connect`, and
  `--port` without `--host`.
- `wake_velocity_serve_scripted` runs the scripted authority through `wake_velocity --serve`, the
  game's own room mode.
Select a group with `-L`, for example `ctest --test-dir build -L core`; CTest adds fixture
setup tests that a selected test requires, whatever their label.

The former demo's `cube_render_checksum` (an exact opening-frame CPU baseline) was removed
with the demo on 2026-10-25; no test now pins a rendered image hash.

`PROTEUS_OPTIMIZE_TESTS=ON` compiles the long numerical validators and the shared boating
libraries (core, render, session and drive) at `-O2` even in Clang/GCC Debug builds. The
validators are boat handling, banking, turn entry, environment, replay, clock, world isolation,
core, fleet, scripted server, network library and validator, water routes and object-wake
review. Debug symbols and assertions remain enabled; there
is no fast-math, reduced simulation duration or sampled-down scenario coverage. Because
validators and applications link one copy of each library, Debug `wake_velocity` also runs this
optimized boat code; the GPU library and application sources keep the selected build type. For
stepping through these validators or library code:

```sh
cmake -S . -B build -DPROTEUS_OPTIMIZE_TESTS=OFF
cmake --build build -j
```

Clang/GCC builds, raylib included, compile with `-ffp-contract=off` (since 2026-10-26), so no
compiler fuses multiply-adds on its own and the `boat_map` and `city_dressing` digests match
between Apple Silicon macOS and x86-64 Linux. Explicit `fma()` calls are unaffected. MSVC does
not contract by default.

The optimization override currently applies to Clang/GCC only. MSVC keeps its selected
configuration; Windows timings have not been measured. GPU checks are opt-in through
`PROTEUS_GPU_TESTS=ON` and need a desktop context. A display-free suite with GPU checks
off has less coverage; report that omission.

CPU lighting checks are display-free. The quick `waterfront_lighting` test covers presets,
continuity, reference repaint/restore, window occupancy and parsing. The static cloud
mask checks require all three terrace bands, valid band values on covered samples and
zero terrace values outside the underside footprint. Its exhaustive
`--sky-check` mode is the separate slow `waterfront_lighting_sky` test: it compares the
visible-sample draw with the full panorama (separate storage/traversal, shared texel recipe)
across legacy/full hours, sizes, cameras and a squall, and uses `waterfront_sky_stats()` to
prove fixed-light/time cache reuse and bounded work on alternating/seeking lights.
Explicit wind cases compare the timed reference and visible draw at forward, backward,
wrapped and long-session times, require changed clouds at fixed light and byte-identical
restoration after seeks, and check the bounded offset across the wrap. The saved
parent captures remain the independent recipe baseline.
`boat_world_isolation.cpu_basin_lighting` paints the full reference and is therefore slow; it
checks the basin sky, hour wrap/direct/backward seeks and both the basin and mapped flat-water
tint ordering. `renderer3d_is_valid` checks textured-material ambient/diffuse/tint, point light
facing, emissive tint independence and the navigation-lamp tint exemption while still checking
that wakes follow the unlit tint, alongside the existing mapping and clipping checks. These are
work/pixel checks, not timing budgets.

`boat_title_settings` (`cpu;quick`, 0.01 s in the 2026-11-08 full run) builds the production
title with the native networking definition but opens no window or network. The `validate_boat_title` executable
overrides the settings directory with an isolated build-tree path and checks actual save/load
roundtrips: all 27 fixed lighting fixtures, invalid versions/non-finite/out-of-range values,
missing and old settings, unrelated fields, and the launch-argument handoff/precedence. It does
not wait for wall time. The user accepted the T3 controls with “Works” on 2026-11-08; exact
hours, night and platform coverage remain open.

`boat_work_clock` (`cpu;quick`) links the work session and plugin catalog and uses the bundled
basin/outer work resources without a window or files. It drives virtual frames through the same
scheduler as the game, then checks the session clock: noon/900 defaults, 1× and 8× both reaching
a whole day in 108,000 ticks, continuous rate changes, wrapping and direct/backward seeks,
invalid seeks leaving state unchanged, load/crate/job reset and recovery preserving the hour and
period, a fresh session resetting to noon, focused suspension gaps over one second discarded
with the world, exactly one clock increment per physical tick, v2
save/resume, rejection of invalid clock data, and clock-only dirtying that triggers periodic,
pause and leave autosaves.

Focused lighting checks also cover the analytic glitter integral (noon zero, recipe boundaries,
seam continuity, day shifts, seek order independence and invalid input) and the view-props clock
helper (missing/invalid props, normalization, noon-zero glitter and seeks).
`validate_boat_sandbox_save` covers the v2 clock tail and a reconstructed valid v1 record
decoding to noon/900 s.
`validate_cargo_decal` covers the work-only ambient floor, including the unchanged Day case.
The [lighting owner](SUNSET_PLAN.md#t4-implementation-2026-11-08) records the final native GPU
suite, actual windowed 8× cycle, captures and platform limits.

## GPU checks

`gpu_fixture --city --check` (CTest `gpu_surface_materials`) also verifies reflection waterline
clipping: above-water scenery remains visible, submerged scenery cannot change the reflection,
and clipping is reset before the main pass. It also compares the uniform-shaded GPU sky with
the CPU panorama at selected legacy/hour values and low/high sizes, checks direct/backward
seeks in lighting and cloud time (including the wind period), and independent scene recreation, and observes native GL texture calls across 24 hours
plus a bounded 241-sample virtual cycle that crosses midnight and alternates the above-city and
azimuth-seam views, also advancing wind across its bounded period. A separate probe texture proves the upload spies work before the sky's
zero-write measurement.
The original target/overlay/squall/pass checks remain. This requires `PROTEUS_GPU_TESTS=ON`
and a desktop graphics context; detailed evidence and limits live in the
[lighting plan](SUNSET_PLAN.md#t2a-implementation-2026-11-07).

GPU checks are opt-in and require a desktop graphics context. Configure and run the
labelled cases on a machine that can create a raylib/OpenGL window:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_GPU_TESTS=ON
cmake --build build -j
ctest --test-dir build -L gpu --output-on-failure
```

On macOS, a sleeping display can make pinned GLFW report no monitor even though AppKit still
lists a screen. For an unattended GPU run, keep the display awake for the command with
`caffeinate -d -i -u ctest --test-dir build -L gpu --output-on-failure`; the assertion ends
with the command and does not change saved power settings. The 2026-11-07 lighting run
confirmed this with CoreGraphics (`active=0 asleep=1`) and GLFW (zero monitors), then passed
with a temporary awake assertion. See the lighting plan for the verification record.

The default display-free suite omits these checks. Report that omission when recording
verification. The `gpu_surface_materials` case also probes the production city shader
with controlled near/far depths: near-map precedence, distant shadow coverage, blended handover, outer fade
and disabled-far fallback. Fixture capture commands live in the [renderer guide](3D.md#gpu-review-captures).

## Network checks

`boat_local` (AR6 ledger rows L0, L6 and L2, labels `core;cpu`; quick except the slow-lane
scripted drives and time trials) runs
`tools/validate_boat_local.c`, which links `proteus_local_drive` (the game's own local-session
glue, `tools/wake_velocity_local.c`, over `proteus_boat_net` and `proteus_boat_drive`). Every
scenario drives the game's direct free-boating path (a `BoatSession` stepped by `timed_boat_step`
with the arcade wake pool) beside a private local session (`src/boat_local.c`: one authority and
one predicting client over the memory transport, driven by host grants) fed the same scripted
frames through the glue, and requires bit-identical boats, presentations and wake pools at every
predicted tick and at each frame's authority tick. Cases: `basin_1x`, `basin_quarter`, `basin_8x`, `outer_1x` and `outer_8x`
(outer with waves, current and crossing water); `grant_96` (0.1 s frames and 0.25 s stalls
clamped to 0.1 s at 8x, after a short first frame, so a grant of 96 starts mid-cadence and takes
9 chunks); `grant_cap` (a frame refuses 97 ticks; the glue grants 96 and carries the rest);
`odd_chunk_sizes` (uneven frame times: partial first chunks and odd remainders);
`explorer_handling`; `pause_and_focus` (30 s of paused frames: no timeout, resync or close);
`restart_generation` (a restart recreates both endpoints with the next generation; an
old-generation command batch and READY are counted stale and change nothing); `repeated_sessions`
(100 restarts); `startup_failure` (invalid configuration and a refused water recipe release
everything); `queue_saturation` (an unreliable or reliable queue overflow stops the session
instead of dropping a command batch silently); `resync_escalation` (an injected wake mismatch that
one resync repairs, then one whose checkpoints are lost, which fails the session after three
served resyncs); `resync_8x` (two quick mismatches at 8x with an advancing host clock are both
repaired: the authority rate-limits private resyncs in granted ticks); the L6 cases
`scripted_demo` (`--demo` through the glue's scripted frames: uneven frames at 1x, then 8x, past
the drive's helm changes at ticks 420 and 1440, every predicted tick equal to `demo_step`'s),
`scripted_capture` (a capture's 1501-tick drive in grants of at most 96 ends on `demo_step`'s
session and pool, again after a restart) and `scripted_needs_script` (a scripted frame without a
script is refused); `private_range` (the private codec round-trips
full precision; a hosted room counts the private type as malformed and rejects the sender); and
the L2 time-trial cases, where both paths take a course autopilot that tracks the gates from its
own boat: `course_trace` (three attempts at 1x with 0.1 s frames: course progress, splits,
recorded run, best, ghost, notices and save requests equal the direct session's after every
frame; a first finish is a new best, an equal time is not, beating a slower best is; the frame's
flush fails the save on an empty best path with the same notice), `course_practice` (8x in waves
latches practice: the run is recorded, never saved or made best; the next attempt is not
practice), `course_restart_mid` (a restart after the second gate starts a new generation with no
finish, save or practice carried), `course_practice_mid` (one 8x frame after the first gate
latches practice for the rest of the attempt) and `course_saved_ghost` (a new best saved by the
frame's flush under the working directory's `boat-local-saves`, selected on a fresh launch and
raced as the ghost; an equal finish is not a best), all slow; `course_state` (the time trial's
state codec and the PrivateState message, which the v14 and input decoders reject); and
`private_state_rejected` (a hosted server counts PrivateState as malformed and rejects the
sender; a playing hosted client counts it as a decode error and keeps no state). Each
drive also requires zero rate-limited, late, future, duplicate or stale-life commands, every tick
integrating a received command, no resync and zero prediction correction error. Frame time is a
scripted value, never wall time. `./build/validate_boat_local --bench` (not a test) reports
processor time per frame through the glue (with presentation) at 8x on `outer` for 16-tick frames
and 96-tick grants, and the direct path with presentation for both.

`boat_net` runs `validate_boat_net`, which links only the core and net libraries and takes
a few seconds. It covers:

- golden bytes and pinned snapshot layout, all truncations, invalid fields, and checkpoint
  round trips that resume bit-identically;
- seeded in-memory rooms: handshake rejections, the input-loss policy, command idempotence,
  sequence wrap, a flood against the bounded queue, profile F one way for 60 s, blackouts and
  timeouts, and wake resync (including a checkpoint at the mirror's own tick repairing it);
- client mirrors that reproduce the authority's `boat_fleet_hash`;
- client prediction: replays from mid-flight, landing and capsized checkpoints that match the
  authority bit for bit, and predicting rooms (steady 30 ms, profile F both ways with smooth
  and keyboard-style bots) that print correction sizes and replay cost;
- wake reconciliation: digest-confirmed carry of missing births, held and incomplete
  corrections, early births by identity, and gated wake-change counts in the predicting rooms;
- remote view: interpolation, life and teleport rules, extrapolation limits and proximity
  hysteresis, plus the drawn delay and position error against the authority in the rooms;
- LAN discovery: query and answer bytes, truncations and invalid answers, and the search
  list's merge and expiry;
- a deathmatch room: every client, including a mid-round joiner, holds the
  authority's scoreboard through capsizes, a round end and a leaver, and names both boats of a
  credited capsize in its kill feed;
- M5 four-player profile F (seed 51), impaired from the handshake onward: bootstrap arriving
  before Accept, late join, room-full rejection, simultaneous resets, inactive input, 1 s
  blackout recovery, 6 s timeout, slot reuse and stale-life rejection. A lossless tail requires
  all surviving mirrors to match the authority at their snapshot ticks exactly.

**Real transport, virtual clock.** `boat_net_enet` runs `validate_boat_net_enet`: the same
room harness, built with `BOAT_NET_TEST_ENET`, over real ENet sockets on ephemeral 127.0.0.1
ports, while the room keeps its virtual clock. Loopback delivery is asynchronous, so after
every service the room calls `boat_net_enet_settle`. It services every host in the process until
each datagram sent has been received, keeping the events for the next polls. Its 5 s limit is
only a failure deadline. Cases:
- `enet_loopback`: two bots, a wrong-token client refused, a second listen on the port
  refused. Over 4 s of room time it checks snapshots, one bootstrap, no resyncs or decode
  errors, applied births and boats that moved, plus message ≤1100, datagram ≤1200 and MTU
  ≤1200 bytes. It also checks graceful leaves, no faults, and that every mirror matches the
  authority's hash.
- `enet_lan`: a loopback LAN answer reaches a search socket with its mode, players, token,
  compatibility and endpoint.
- `enet_lifecycle` (slow): three predicting bots stay while a fourth leaves and a fifth is
  refused as full. Another bot falls silent, times out on the room clock after
  `BOAT_NET_TIMEOUT`, and a replacement takes its slot.
- Unchanged room cases over UDP: `rejections`, `resync`, `resync_same_tick`, and `deathmatch`,
  `race_room` and `mapped_rooms` (all three slow). `mapped_rooms` checks that clients take the
  server's location.

**Processes.** `boat_net_process` runs `tools/boat_net_process.py` (about 0.25 s). It ends on
events, not elapsed time:
- `boat_server --until-empty` closes the room once every accepted player has left.
- `boat_bot --snapshots 3` leaves after three snapshots.
- `boat_bot --discover --first` stops at the first answer.

It checks a port clash, a LAN search with the room's flags, two token-holding bots and a
wrong-token refusal, bot summaries and bounds, and the server's accept/leave/rejection counts
without faults. Its timeouts are failure deadlines. Run it alone with:

```sh
python3 tools/boat_net_process.py --server build/boat_server --bot build/boat_bot
```

`boat_host` runs `tools/boat_host_check.py` (about 0.3 s): `wake_velocity --serve --exit-with-stdin`
on free ports and one `boat_bot --snapshots 3` session. It then closes the room's standard input,
as the hosting game does when it leaves or dies. The room must report it, keep
`accepted=1 left=1` and exit 0 within a 2 s deadline. With GPU tests enabled,
`wake_velocity_host_gpu` hosts a windowed outer deathmatch with `--host` on port 24779 for 120
frames and requires the room to close after the session. It is the one listed exception to
[the no-wall-time rule](#no-wall-time): windowed frames follow the display.

**Manual soak.** `tools/boat_net_session.py` is not a CTest test. It runs real processes on the
wall clock: three predicting bots stay while a fourth leaves, joins in progress, crashes, times
out and is replaced. Use it for long churn runs:

```sh
python3 tools/boat_net_session.py --server build/boat_server --bot build/boat_bot --seconds 1800
```

`--peer-timeout` (1–60, default 5) sets the room's silence timeout through
`boat_server --peer-timeout`. `--seconds` (default 30) accepts one churn cycle plus a second up
to 7200. It does not measure memory plateaus or CPU/bandwidth budgets, or apply OS-level
impairment. The 30-minute, one-hour and two-hour M5 gates still require recorded runs and
resource measurements. `boat_bot --predict`, `--reset-every S`, `--inactive-every S`, `--hunt`
and `boat_server --mode deathmatch|race --round-seconds S` remain available for manual runs.

`boat_net` also runs four predicted clients on the outer map: shoreline-safe spawns,
a deathmatch round, starts >200 m from the dock and >50 m apart with 35 m shoreline clearance,
a snapshot outside the former basin coordinate envelope, a local loading pause, reset and exact
mirrors at a birth-batch snapshot. The protocol-v8 codec rejects invalid map IDs and reserved
bytes and checks the RaceState/RaceEvent rules; a race room on the in-memory transport checks
predicted holds released at GO, ferry-exact mirrors, a mid-race joiner and results, and a
prediction replay through GO matches the authority; discovery uses the complete compiled-map bundle digest.

None of these tests impairs a real network interface, and none replaces the
[planned matrix](MULTIPLAYER_PLAN.md#verification-matrix-and-telemetry).

## Core boundary gate

`proteus_boat_core` holds physics, pose, locations, world and shoreline queries, water queries,
wakes, course crossing, harbor/traffic contacts and boat/city geometry data. The boundary check
rejects any core include outside the core headers, `raylib.h`, `raymath.h`, compiled map
geometry and a short system-header list without `<stdio.h>`. It also rejects raylib calls
other than raymath and C library console, file and process calls. When `nm` is available
(not MSVC), every undefined symbol in the core archive must be defined in core or be raymath,
libm, libc memory/sort/assert, `clock()` or the Accelerate DFT backend. The script first runs
a self-test that each rule rejects a known violation:

```sh
python3 tools/check_core_boundary.py --self-test
```

Add a new core source and its header to `PROTEUS_BOAT_CORE_SOURCES` in `CMakeLists.txt`.
Presentation, GPU, session/replay and the wake_velocity drive/profile code live in the
`proteus_render`, `proteus_gpu`, `proteus_boat_session` and `proteus_boat_drive` libraries.
Network codec, session and in-memory transport sources go in `PROTEUS_BOAT_NET_SOURCES` and
obey the same rules (`boat_net_boundary`); only `src/boat_net_enet.c` may use ENet and sockets.
The script accepts `--archive` more than once and resolves symbols across the listed archives.

The Lua plugin host (`proteus_plugins`, [plugin API](PLUGIN_API.md)), its Lua-free data library
(`proteus_plugin_data`) and the pinned interpreter (`proteus_lua`) sit outside both boundaries.
`plugin_data_boundary` (`tools/check_plugin_data_boundary.py`) checks that the data library and
its data-only consumers (sandbox save, bundled catalog, `validate_boat_plugin_data`) include no
evaluation, I/O or worker header and name no Lua, file, process or platform identifier, that
their CMake targets link neither `proteus_lua` nor `proteus_plugins`, and that the
`proteus_plugin_data` source list matches the checker's files. With nm, the data archive's and
each `--consumer-archive`'s undefined symbols must resolve to symbols defined by the listed
archives or allowlisted libc/libm/raymath symbols.
`plugin_data_boundary_self_test` (slow lane) plants each violation. `boat_plugin_data` runs
`validate_boat_plugin_data`, which links only the catalog and plugin data, decodes every bundled
entry and round-trips an objective checkpoint. Configure fails if `proteus_boat_core` or
`proteus_boat_net` links either of them. The self-test also rejects a core `#include "lua.h"`
or `"boat_plugin.h"`, and the archive check rejects any `lua_*` reference. Strict JSON reading
for the server directory and plugin manifests lives in `proteus_json` (`src/boat_json.c`).

## Long-session clock gate

`boat_clock` checks direct-time cases (one day, 30 days) without simulating the preceding
ticks; `boat_clock_soak_smoke` runs a 60 second wave soak. The actual one-hour soak is opt-in
and should use a Release build; it took about 95 s CPU on an M1 Max on 2026-10-23:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target validate_boat_clock -j
mkdir -p build/bench/c3
./build/release/validate_boat_clock --soak 3600 build/bench/c3
./build/release/validate_boat_clock --soak 3600 build/bench/c3 --waves
```

The soak accepts 60..86400 seconds and writes CPU frames at one minute, halfway and the end.
Its printed hash is same-build evidence, not a cross-platform contract.

## Revisit the physical field

```sh
cmake -S . -B build -DPROTEUS_WAKE_FIELD_TESTS=ON
cmake --build build -j
ctest --test-dir build -L wake_field --output-on-failure
# Return to the normal suite afterwards:
cmake -S . -B build -DPROTEUS_WAKE_FIELD_TESTS=OFF
```

The label includes only retired research tests, not the active arcade model. This is
suspended verification of the opt-in field implementation, not evidence that the field
meets its original performance or numerical acceptance goals.

## Measurements and verification

### Fast lanes, 2026-10-26

M1 Max/macOS, Debug, `PROTEUS_OPTIMIZE_TESTS=ON`, GPU tests off unless noted. Each step was a separate commit on the `test-speed` branch.

| Run | Tests | Wall | Summed test time |
|---|---|---|---|
| Baseline, `-j 4` | 98 | 71.6 s | 285.8 s |
| After the case split and presentation-free physics, `-j 10` | 171 | 36.2 s | 216.5 s |
| After shortening the ENet tests, `-j 10` | 171 | 19.0 s | 144.8 s |
| `ctest --preset full` (`-j 8`) | 220 | 18.7 s | 145.4 s |
| `ctest --preset quick` | 133 | 0.8–1.1 s | 5.6 s |
| `ctest --preset full`, network on a virtual clock | 227 | 11.2 s | 88.7 s |
| `ctest --preset quick`, network on a virtual clock | 140 | 0.75 s | – |
| `ctest --preset full`, `PROTEUS_GPU_TESTS=ON` | 246 | 60.7 s | 207.7 s |

With GPU tests on, the GPU tests run one at a time, so they bound the wall time. The largest is
`boat_river_benchmark_contract` at 27 s. The last full GPU run before this work (2026-10-25)
took 183 s at `-j 4`.

Before the virtual-clock change, the five real-time ENet tests (9–16 s each) bounded the full
suite's wall time. They are now `boat_net_enet` cases and a 0.24 s process smoke, and three
oversubscribed `-j 40` runs passed. The longest tests are now sweeps:
`boat_world_isolation.lifecycle` and `boat_map_index.lattice_queries` (about 6 s each) and
`boat_map_scaling`. Largest per-test changes:
- `wake_objects_contracts_jumps`: 35 s became 0.12 s of quick cases plus 1.7 s of slow ones.
- `boat_banking`: 22.3 s became 0.3 s quick plus 1.5 s slow.
- `boat_world_isolation.view_binding`: 3.9 s → 73 ms; `city_dressing`: 4.2 s → 0.24 s. Both
  come from the scenery ground index in `src/waterfront_corner.c`, which keeps the pinned
  dressing digest and identical captures.
- `boat_environment`: 2.9 s → 0.3 s.
- `boat_net_session` and `boat_net_river`: 36 s → 16 s each, then replaced by
  `boat_net_enet.enet_lifecycle` (0.34 s) and `boat_net_enet.mapped_rooms` (0.2 s).

The state hash moved to `hash_revision` 2 (see [the benchmark guide](BOATING_PERFORMANCE.md)).
For banking, world isolation and the scenery index, outputs, captures and the banking CSV
were compared byte for byte with the previous binaries. `boat_environment` printed the same
values. The other conversions were checked by their own assertions passing.

Not done yet:
- `boat_map_scaling` (5 s), the XM checks (1.4 s each) and `boat_clock_soak_smoke` (1.8 s)
  are still single slow tests.
- `boat_river_benchmark_contract` (27 s) and `boat_race_trial_gpu` (12 s) are unsplit GPU
  tests.
- The CPU water surface, the map build and in-game flag cloth cost remain product follow-ups.

### Routine-suite reduction, 2026-10-22 (history)

2026-10-22, M1 Max/macOS, Debug, GPU tests enabled. The original serial suite passed
all 98 tests in **748.21 s (12 min 28 s)**. The fifteen retired checks used **434.33 s**;
`wake_candidate_moving_source` alone used **247.92 s**. The remaining checks summed to
313.90 s, including 83.91 s banking and 40.15 s handling in unoptimized Debug builds.
This explains why disabling only the field would still leave a five-minute serial suite.

The revised routine command passes **all 83 active tests in 97.40 s (1 min 37 s)**,
including **20 active GPU checks**: an **87% wall-time reduction** from the old serial
suite. This combined improvement includes field suspension, optimized numerical tests
and four CPU jobs; it is not an isolated compiler benchmark. Banking drops from 83.91 s
to 23.00 s and handling from 40.15 s to 11.01 s despite concurrent CPU work. The largest
remaining check is `gpu_review` at 23.99 s; active GPU checks total 60.31 s. These are
single local runs, not cross-platform guarantees.

Debug configure/full build passes without project warnings. Registration checks confirm
exactly fifteen disabled research cases and all 83 remaining checks enabled; a separate
configure with `PROTEUS_WAKE_FIELD_TESTS=ON` enables all fifteen research cases. The
CTest GPU label summary counts three disabled field cases too (23 listed), so the actual
active GPU count is twenty. CMake-generated validator flags retain `-g` with `-O2`,
without `NDEBUG` or fast-math. No wave recipe or gameplay physics changed here.

Next: keep the routine suite active; revisit field verification only when physical-field
work resumes. Timings and suite logs are in `build/test-speed/`. The prior default-promotion
run also completed before the suite policy changed, so its all-98 result remains valid
historical evidence. New manual motion/audio review was unnecessary for these test-only
changes; the preceding default switch has its own capture/desktop evidence.

## Deathmatch weapons (protocol v8)

`boat_combat` verifies cannon ascent/apex/descent, water landing, three-dimensional descending
hits/overhead misses and trajectory replay, the shared muzzle attachment on a pitched/rolled
boat, 10 m/45 m capsize targets, progressive capsizes/recovery, torpedo ammo/arming/serial wrap,
actual damage/overkill, rearward grenades with a 4 m peak/70 m range/seven-second lifetime
and four-player ring reuse at recharge, deterministic cadence, overheating/release cooling, grace,
numbered throws and replay, pool limits, fuses, relative moving-hull sweeps, water blocking,
life fences, one lethal defeat, respawn, radial derivatives and physical self-waves. It also
round-trips combat checkpoints, checks the compact combat payload over 2000 real fleet ticks,
malformed pages and the 1100-byte ceiling. `boat_net` adds four predicted deathmatch clients,
late joins, loss/duplication/reordering, a one-second outage, complete page-set recovery,
snapshot liveness/resync and departure generations. Free/race continue
through the existing trace/replay cases. `boat_race_trial --gpu-check` now compares both
production radial shader recipes against CPU height/gradient/vertical velocity.

```sh
./build/validate_boat_combat
./build/boat_combat_review --time 2.3 --screenshot build/combat-cpu.png
./build/boat_combat_review --gpu --play --screenshot build/combat-gpu.png
./build/boat_combat_review --gpu --close --time 1.025 --screenshot build/cannon-close.png
./build/boat_combat_review --gpu --mirror --play --screenshot build/rearview.png
./build/boat_combat_review --gpu --torpedo --time 1 --screenshot build/torpedo.png
./build/boat_server --scripted --mode deathmatch --boats 4 --ticks 7200
```

`boat_fleet_render` also probes the actual transformed hull triangles beneath every corner
of the cannon mounting foot on all multiplayer room-slot skins and checks that the housing overlaps that foot.
This guards against the skin mesh's normalization cancelling the hull pose scale. The same
gate renders the aft camera at three headings, checks that only trailing boats appear, verifies
starboard placement after mirroring and confirms read-only fleet state.

The review fixture captures the shared scene at 320×200. CPU screenshots are display-free;
GPU and `--play` require a desktop and include the production weapon HUD and procedural audio.
`--mirror` adds a pursuing boat, rear grenade/wave and the production mirror (initially shown;
V toggles it during `--play`, whereas the game defaults to hidden); it also requires
a graphics context for HUD compositing when the scene renderer is CPU.
`--torpedo` is a close presentation view that hides the HUD so the nose and tail are visible.
For a display-free WebGL2 comparison of both production radial shader recipes:

```sh
python tools/check_web_combat_wave.py
```

This optional command needs Playwright and Chrome; its float probes independently check height,
gradient and vertical speed. `check_web_multiplayer.py --mode deathmatch` also exercises gun and
numbered grenade commands, page reception and captures the weapon HUD and burst over WebRTC.

Current automated/manual evidence and unavailable platform checks belong to the
[deathmatch owner](DEATHMATCH_PLAN.md#weapon-milestone--2026-10-26-superseded-rules-historical-evidence).


`boat_plugin_author` (native, slow CPU lane) checks the P4.1/P4.2 JSON validator/preview tools and disposable
worker: voyage/race/objective examples, descriptor/init evidence, malformed framing, restricted
Lua failures and budgets, immutable snapshot pins, input type/size rejection, and virtual-time
cancellation/deadline cleanup. Cancellation lifecycle and malformed output use a fake process; an intentionally stuck real
process also verifies kill/reap at a virtual deadline. Normal validation uses the native worker. No test waits for wall-clock time to pass.
See [native authoring tools](PLUGIN_API.md#native-authoring-tools-p41).

P4.2 extends `boat_plugin_author` with JSON Schema discovery and every numeric descriptor
bound checked against the native decoder, repeatable voyage/race scenario artifacts and
preview callback rejection. `boat_plugin_stage` (native, slow CPU lane) checks checked-data
handoff, native stepping during staging, held Apply/rebind, repeated candidates, same-number
stale epochs after Undo, virtual deadlines against a stuck process, F6 cancellation,
malformed/oversized output, syntax failure and the live F5/completion adapter. Its process
completion loops await an external event; CTest supplies the failure deadline. No simulated
clock is advanced to wait for a child. See [live descriptor staging](PLUGIN_API.md#live-descriptor-staging-p42).
It also covers console queries: answer, syntax error, the exact five-second deadline, cancel and
worker faults. `boat_plugin_stage_web` (quick) compiles the real common staging and browser
backend natively against a C model of the page transport, runs workers in process and uses
virtual time. It covers the empty-host launch receipt, rejected race/match packages, the 15 s
query deadline message, job release after failed or unreadable queries, and replies at the
160 KiB bridge limit. It does not run the page's JavaScript or a real Web Worker.

P4.5 adds `boat_plugin_capture` (native, slow render lane): the same script with `--game`
runs real CPU captures of every outer anchor, repeatable pinned evidence, a rejected candidate and
the CLI's atomic `--output`. Its child waits are failure deadlines; the capture lifecycle uses
the fake process in `boat_plugin_author`. See [frame capture](PLUGIN_API.md#frame-capture-and-anchors-p45).

P4.6 adds `wake_velocity_bridge` (native, CPU lane): `validate_wake_bridge` drives the game's
bridge over request files on a virtual clock (authentication, pins, deferred isolated staging,
Apply/Undo/Discard, rejection, restart, cancellation, save-poll agreement, and the PC `console`
operation). `boat_plugin_author`
gains client checks against a fake game and an end-to-end CLI run against
`validate_wake_bridge --serve` (`--bridge-server`). Its loops await external process/file events;
CTest supplies the deadline. See [live tool bridge](PLUGIN_API.md#live-tool-bridge-p46).

PC adds `wake_command` (native, CPU lane): `validate_wake_command` drives the console's input,
history and bounded scrollback headlessly; composes edits as one entry and checks the `console N`
attribution; stages a far berth edit through the real isolated worker and proves the boat and
voyage trace match a control tick for tick and that repeating the same edit at the same tick
hashes identically; holds a berth moved under the hull, commits it at the first clear boundary
without displacing the boat, and Undo restores the prior envelope with the boat kept. It also
covers a failed edit keeping the active revision, `/capability scene.define`, `/save` reading back,
a bounded query, and that no Lua state is created on the game thread. The child waits are failure
deadlines, not simulated time.

P4.7 adds the `Packaged` case to both `boat_plugin_author` and `boat_plugin_capture`: it installs a
release layout with `tools/package_plugin_tools.py` in a temporary path containing spaces, then runs
the copied tool from another directory (capabilities, anchors, validate of every example, preview,
no bytecode cache, refusal of an unknown layout). Only the render lane, with `--game`, adds a
capture. See [packaged authoring tools](PLUGIN_API.md#packaged-authoring-tools-p47).

P4.3 adds `web_plugin_author` (Node, CPU quick lane): portable bundle/UTF-8 byte caps, immutable
framing, bounded worker lanes, one-use receipts, stale epochs, virtual deadlines, cancellation,
late output and worker/exit/framing faults. PD4 adds `web_plugin_wire` (quick), which fails if
`web/plugin_author.js`'s wire magic differs from `BOAT_PLUGIN_WIRE_VERSION`, and extends the Node
contract with scene and free/course imports plus room-kind rejection. `tools/package_web.py`
runs `tools/check_web_wasm.py` on every packaged site: the game Wasm must lack Lua parser/VM
strings, with `plugin_stage.wasm` as the positive control. The opt-in
`python3 tools/check_web_plugin_author.py --browser chromium --url http://127.0.0.1:8000/ --output build-web/plugin-browser-check`
(also `firefox` or `webkit`) drives an actual served Wasm build through bundle export/import,
capability rejection, scene/free/course staging and launch, repeated worker validations, launch,
live rename and syntax failure, held water Apply/Undo, stale output, cancellation, navigation
mid-validation, repeated source restore/revalidation and storage denial. On Chromium it also
freezes the tab mid-validation. The title check now covers the bundled Harbor life catalog session,
the desktop console query/export/live-edit path and the absence of a worker in a normal session.
It awaits application/worker conditions; the only fixed waits are the freeze durations themselves.
The browser PR workflow runs the Node contracts and packages the Wasm build. Full three-engine
qualification is manual-only; dispatch it for browser/plugin lifecycle changes and before a
release, using `gh workflow run web-plugin.yml --ref BRANCH`. It is not run on ordinary pushes
to `main`. Superseded PR builds are canceled. The [browser guide](WEB_BUILD.md#plugin-source-bundles-p43) owns setup/limits.
