# Documentation and current plans

Current status reconciled on 2026-10-26. The main program is the boating game `wake_velocity`
(formerly `boat_study`; [rename record](MULTIPLAYER_PLAN.md#host-from-the-game--2026-10-25))
(Wake Velocity), with quiet engine/water audio and a choice of the gentle A little boat loop,
Acid Wake techno or Off. It provides free boating in the basin and on the river, a basin time
trial and multiplayer rooms. It defaults to GPU rendering; the CPU reference supports
display-free captures. The four-minute Eriksberg demo (`proteus`), its soundtrack, the separate
Eriksberg boating location and their plans were removed on 2026-10-25; they remain in the git
history.

## Current work

Released internet hosting: the [Winterfell operator guide](MULTIPLAYER_HOSTING.md) owns the
v0.0.18 native/browser room deployment and its verification status (2026-10-26). This hosting
slice is pulled forward from M6; native/browser play and browser reconnect passed over a
separate internet path. Both weapons are now deployed through the `davve-srv` baseline
role; named joining, weapon inputs and combat pages passed automation. Human weapons play
and full internet-play qualification remain pending.
The browser and native clients fetch the known-server list and default to its public rooms;
see the [hosting guide](MULTIPLAYER_HOSTING.md#native-directory-client-2026-10-26). The public
`wake.davve.net` endpoint is deployed. PR #14's release-pinned workflow republished the
v0.0.18 Pages launcher; a fresh Chrome browser joined from the live Pages URL without
local-network permission. Human gameplay and broader browser/network qualification remain open.
The native client connection check is fixed locally; a live server handshake now reports a
map/content mismatch for this checkout, so a compatible live join remains to be verified.
Details and current verification limits stay in the guide.

| Area and owning document | Implemented | Remaining / next task |
| --- | --- | --- |
| [Free-riding adventure / Vinga](FREE_RIDING_ADVENTURE_PLAN.md) | A0.1 original river [inventory and reference](adventure/baseline.md), A0.2 [pinned coastal sources](adventure/README.md#coastal-sources-a02-2026-10-25) (OSM corridor, land polygons, Vinga A storm slice; terrain not acquired), A0.3 [route/coverage survey](adventure/README.md#route-and-coverage-survey-a03-2026-10-25) (19.6 km candidate route to Vinga, 10.9 km² first slice to Långedrag, 512 m tile experiment) and A0.4 [controlled river baseline](adventure/baseline.md#a04-controlled-river-baseline--2026-10-25) (M1 Max frame p95 ≤5.7 ms; paced display 58.6 Hz, since 60.00 Hz with the [frame pacer](BOATING_PERFORMANCE.md#frame-pacing--2026-10-25)) completed 2026-10-25, closing A0 with its [decision record](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a0-decision-record--2026-10-25); A1.1 [explicit map description and caller-owned collision cache](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a11--explicit-map-data-and-collision-cache-construction) completed 2026-10-25 with unchanged river queries and captures; A1.2 [caller-owned world resources through worlds, sessions, fleets and networking](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a12-result-2026-10-25) completed 2026-10-25 with the global wrapper removed and traces, captures and content digest unchanged; A1.3 [view-owned water-mesh caches](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a13-result-2026-10-25) completed 2026-10-25 with mesh topology and all CPU/GPU review captures unchanged; A1.4 [explicit CPU/GPU scenery binding](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a14-result-2026-10-25) completed 2026-10-25 with all river captures unchanged; A1.5 [lifecycle/parity gate](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a15-result-2026-10-25) completed 2026-10-25 (reload, release, failure and GPU recreate parity; ASan/UBSan clean; frame p95 within the A0.4 baseline), [completing A1](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a1-completion--2026-10-25); A2 [tiled current river](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a2--tiled-current-river) split into A2.1–A2.5, with A2.1 [submission and triangle counters](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a21-result-2026-10-25) (pixels and traces unchanged) and A2.2 [tile catalogue and indexed collision](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a22-result-2026-10-25) (16 m grid; no mismatch in 3.6 M comparisons with the full scan) and A2.3 [per-tile land culling](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a23-result-2026-10-25) (1 km tiles; median map triangles 0.86 M → 0.14 M, GPU p50 −14%) completed 2026-10-25, and A2.4 [indexed water](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a24-result-2026-10-26) (256 m grid; CPU captures identical, mean CPU triangles 2.34 M → 1.30 M; GPU waves captures identical to main; GPU CTests passed) completed 2026-10-26, and A2.5 [same-view scaling](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a25-result-2026-10-26) (synthetic corridors to 25 km: queries, patches and visible water identical and constant-cost; CPU water tessellated lazily, 218 MB to 112 MB; GPU timing not run) completed 2026-10-26, completing A2; detailed [execution ledger](FREE_RIDING_ADVENTURE_FIRST_STEPS.md) | A3.1 [baked first-slice map](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a31-result-2026-10-26) (16.5 km² region; the A0.3 route is water under collision queries; not yet a playable location) and A3.1b [Baskarta land heights](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a31b-result-2026-10-26) (CC0 contours, 20 m inland / 5 m near shore, 165 k triangles, heights to 78 m; datum unconfirmed, GPU tests not run) completed 2026-10-26, and A3.2 [mapped `outer` location](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a32-result-2026-10-26) (`--location outer`; CPU and GPU captures, full suite passed; frame cost unmeasured) the same day, and A3.3 [river buildings and bridge baked into the outer map](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a33-result-2026-10-26) (240 k triangles; flat 3.5 m ground inside the river crop; CPU and GPU captures under the bridge; GPU frame time unmeasured); A3.4 [Långedrag voyage](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a34-result-2026-10-26) (solo `outer`: destination cue and controlled-speed arrival, shelters with recovery, fixed exposed rough water; autopilot-verified, unplayed; GPU captures partial) completed 2026-10-26; after the [first playtest](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a34-playtest-2026-10-26) (long run, barren past the bridge), A3.5a [dressing past the bridge](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a35a-result-2026-10-26) (1,956 OSM buildings on the terrain, container cranes, channel marks, 3,050 authored trees; 307 k triangles; unplayed, GPU frame time unmeasured) completed 2026-10-26, and an [explorer boat](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#explorer-boat-2026-10-26) (`--skin explorer`: about 30 m/s when free boating solo; courses and rooms unchanged; unplayed) the same day; A3.5 weather transition and bounded loading next ([plan](FREE_RIDING_ADVENTURE_PLAN.md)); A2.5's GPU scaling timing is still unmeasured; then the staged adventure to Vinga |
| [Boating title and main program](TITLE_SCREEN_PLAN.md) | **Wake Velocity** selected; prototype logo over a real GPU river sample; title in `wake_velocity`: single player, LAN/join by address, settings for nick, livery, steering and soundtrack, return to title (2026-10-24, agent-verified) | Native release naming uses Wake Velocity / `wake_velocity` with versioned artifacts ([release status](RELEASES.md#status-and-verification)); user playtest of title, soundtrack and join flow; `boat-marine-hud` merged into `main` 2026-10-24; in-game Escape menu (resume, steering, controls, leave) implemented 2026-10-25, BACK TO TITLE user-checked; location, display/volume settings and live backdrop deferred |
| [Multiplayer](MULTIPLAYER_PLAN.md) | Native-first authoritative four-player design, protocol/wake strategy and verification gates; M2 local four-boat authority (`boat_fleet`, scripted `boat_server`) implemented and user-played; M3 [protocol v1](MULTIPLAYER_PROTOCOL.md), ENet transport, `boat_server` rooms, `boat_bot` and diagnostic `wake_velocity --connect` implemented; M4 step 1 client prediction (`boat_prediction`, checkpoint/input replay) implemented | M3 and M4 step 1 automated and agent-run loopback checks pass on macOS; M4 step 1 user-played beside `boat_bot` (2026-10-24); M4 steps 2 (wake reconciliation), 3 (remote view) and 4a (fleet rendering, Nordic skins by slot) implemented and automatically verified; 4a user-played with several boats and bots, 2 and 3 casually played alongside (2026-10-24); step 4b (reset feedback) implemented and automatically verified, not yet played; Mac/Windows packages carry `boat_server` for LAN rooms (Mac package checked locally; Windows CI passed 2026-10-24; three macOS CI test failures fixed, CI not yet rerun); Windows/Linux multiplayer qualification remains outstanding; browser evidence is recorded under M8; user played a two-machine LAN room (Mac host + client, Windows client) 2026-10-24 ("works great"); [LAN discovery](MULTIPLAYER_PROTOCOL.md#lan-discovery) lists rooms on the title (single-Mac checks only); M4 acceptance checks deferred by user direction (2026-10-24); [hosting from the game](MULTIPLAYER_PLAN.md#host-from-the-game--2026-10-25) (title HOST A ROOM, `wake_velocity --host`, the game as its own `--serve` room; packages ship one executable) implemented 2026-10-25, user-played on macOS ("Works great!"), Windows uncompiled; executable rename to `wake_velocity` planned there; M8 local browser slice implemented ([WebRTC evidence and limits](WEB_BUILD.md#webrtc-rooms)); M5 active: four-player lifecycle validator and bounded ENet churn harness implemented; fixed-map river hosting pulled forward from M7 (protocol v3, free/deathmatch; v4 adds race rooms; v5 drops the Eriksberg location); next: broader impairment matrix, resource measurements and long soaks ([M5 status](MULTIPLAYER_PLAN.md#m5--four-playersession-and-impairment-hardening)) |
| [Deathmatch](DEATHMATCH_PLAN.md) | Machine gun and physical wave grenades implemented after D1/D2; protocol v6, checkpoints/prediction, bounded paging, HUD and bots | Deterministic combat and impaired four-client tests pass; CPU wave/sinking captures inspected; WebGL2 recipe parity and mixed Chrome/native combat smoke pass; native GPU/window/audio, other browsers and human balance checks pending; side-ram/surge/mine/arena deferred |
| [River sprint race](RACE_PLAN.md) | R0 implemented 2026-10-24: playable shortened 957 m Eriksberg/bridge route, independently owned physical ferry wake, CPU/GPU recipe parity, visible ferry plus physical passing-boat wakes; R1 race core (`boat_race`: phases, held grid, swept gates, places, recovery, DNF, deadlines) implemented 2026-10-24 on that route, four bots finishing in 46–52 s with 6–10 jumps; R2 network race (`boat_server --mode race`, protocol v4, predicted hold/ferry, finisher parking, markers, HUD, results, race bot) implemented 2026-10-24; [R3 skill layout](RACE_PLAN.md#r3-skill-gate-layout--2026-10-24): six alternating 32–40 m openings, chicane, retained ferry, 8 m/s moving physical storm with irregular directional waves, high cloud, varied rain and lightning; bots finished in 63–71 s before landing weight, now 64–76 s in Debug, and a storm-hit bot may DNF in Release ([margin](BOAT_PHYSICS_PLAN.md)) | R0 and R1 user-approved 2026-10-24; R2 automatically verified with bots over ENet; [first two-human river race](RACE_PLAN.md#first-human-race--2026-10-24) played 2026-10-24 ("successful and fun", improvements to collect); first broad layout user-played and rejected as too easy; storm/course baseline user-accepted 2026-10-24; [crest shading tweak](RACE_PLAN.md#crest-shading-tweak--2026-10-24) user-approved; [three-crest ferry wake](RACE_PLAN.md#three-crest-ferry-wake--2026-10-24) implemented and user-approved; [closer camera and finer rain](RACE_PLAN.md#closer-camera-and-finer-rain--2026-10-24) implemented and user-approved; next: list the race improvements, assess 63–71 s pacing, then full course; full downtown start needs map coverage; [R2 status](RACE_PLAN.md#r2-network-race--2026-10-24) |
| [Boating foundations](BOATING_FOUNDATIONS_PLAN.md) | P1 instrumentation, P2a benchmark, P3a arcade baseline, C1 integer clock/replay revision C2 double-time water/traffic/shader consumers, and C3 long-session gate/replacement baseline implemented and played; basin W1/W3 slice (location definitions, explicit world/session) implemented and played; basin S1/S2 slice (shared pose math, cosmetic presentation split) implemented; basin S3 slice (session-owned course/runs, replay view state, finish saves outside the tick) implemented; basin S4 slice (static core library, core validator, boundary check, test labels) and S3 `BoatInput` type implemented; [report contract/evidence](BOATING_PERFORMANCE.md) | Mapped W1–W4, shared CPU/GPU scene frame, validator splitting, S5 and full P2/P3 workload/renderer matrix remain pending |
| [Marine HUD and controls](BOAT_HUD.md) | `controls-pedals-stability` implements held W/S pedals, Shift precision, 0.25 s command travel, safe menu/held reset, and automatic aerial stabilization; mouse hold and keyboard spring return retained; physics 9 | Isolated from base `2b55994`, pending integration and human feel review; [verification, conflicts and handoff](CONTROLS_PEDALS_HANDOFF.md) |
| [Boating design](BOATING_PLAN.md) | Basin and river locations (Eriksberg location removed 2026-10-25); timed S course, recordings, persistent bests, ghosts, replays, full-window presentation with modal `?` help, quiet engine/water audio with a joyful repeating XM, a user-accepted wind-driven Swedish ensign, and a user-accepted speedboat shape with [four Nordic skins and flags](BOAT_SKINS.md) implemented in an isolated branch | Nordic skins/flags user-accepted; [sleeker speed profile](BOATING_PLAN.md#speedboat-skins--2026-10-24) in `boat-white-hulls` user-accepted; twin outboards user-accepted; [integration branch](BOAT_SKINS_HANDOFF.md) folded into main 2026-10-25 after A2.3 (with the browser title/WebRTC work); river close-camera default implemented and user-approved; score-loop remains proposed; staged archipelago exploration now owned by the [free-riding adventure plan](FREE_RIDING_ADVENTURE_PLAN.md) |
| [Boating music](BOATING_PLAN.md#selectable-soundtracks--2026-10-24) | Saved native soundtrack choice and browser selector: A little boat (default), original Acid Wake techno (132 BPM), or Off; new XM decoder/loop check and native Wayland menu/playback checks passed | Listen to Acid Wake and play the selector; connected, browser and packaged playback pending; Fedora suite retains the earlier CPU checksum failure |
| [Boat physics](BOAT_PHYSICS_PLAN.md) | Accepted H3 grip and turn banking retained; [arcade stability study](ARCADE_STABILITY.md) adds recovery and steering air roll (revision 7), integrated into main | User requested heavier, more satisfying landings (2026-10-24); [landing candidate L1](BOAT_PHYSICS_PLAN.md#l1-candidate--2026-10-24) (landing speed scrub, revision 8; splash, hull slap) played 2026-10-25: the user liked the impact feel after the camera dip was removed; stability candidate needs play feedback; combined multiplayer feel and platform qualification pending |
| [Wake rewrite](WAKE_REWRITE_PLAN.md) | Bounded arcade objects remain default; [stability study](ARCADE_STABILITY.md) lowers default crests to 1.2 m while retaining signed overlap and jumps | Play the stability/roll candidate; tight-turn readability and platform/fleet costs remain open |
| [Living city](LIVING_CITY_PLAN.md) | 01–05: city dressing, materials, berths, shared actors; travelling boats and a compact ferry brought forward; 32 reactive boating gulls | 06 people/cyclists; varied road traffic, building activity, ferry polish, perched gulls, and final review remain |
| [River / Älvsborgsbron](river/README.md) | Separate source/crop, navigable bridge blockout, blocking tower legs, and grounded road approaches | Bridge/shoreline/building refinement; human handling and camera review |
| [macOS app builds](MACOS_BUILD.md) | Apple Silicon action, relocatable app bundle, preferred river launch, ad-hoc signing, ZIP/checksum; title backdrop packaging fix added | Verify macOS packaging CI and launch the downloaded app from Finder; macOS 13 execution pending |
| [Windows builds](WINDOWS_BUILD.md) | Portable x64 action; reversal and basin impact-bound corrections, bundled music, relocated CPU check; ENet/`boat_server` MSVC build and packaged server passed CI (run 40083439355, 2026-10-24); title backdrop packaging fix added, pending CI | Verify the current Windows workflow and launch the downloaded app; then physical mouse/scroll, GPU/audio and connected controls |
| [Linux builds](LINUX_BUILD.md) | Native Wayland enabled with X11 compatibility; Weston/Xvfb CI checks configured; bundled GLFW no-seat startup crash backport implemented | Rerun Linux CI to verify the Weston crash fix and X11 startup, review CPU screenshots; GNOME/KDE physical input, display scaling, audio and connected play pending |
| [Automated releases](RELEASES.md) | Version-tag/UI release workflow reuses both platform builds, uploads ZIPs/checksums, then publishes; offline publication tests passed | `v0.0.18` release and Pages succeeded; Mac/Windows ZIP checksums verified and Winterfell baseline cutover completed (2026-10-26); launch both packages ([status](RELEASES.md#status-and-verification)) |
| [Browser boating](WEB_BUILD.md) | Shared title, saved setup/settings, Escape/return-to-title; optional WebRTC client and native endpoint alongside ENet | Chrome/SwiftShader title/persistence/repeated-session and mixed native/browser free/deathmatch/race automation pass (2026-10-25); next: real impairment, physical devices, Firefox/Safari, tab suspension and public WSS/ICE/TURN qualification; [status and limits](WEB_BUILD.md#webrtc-rooms) |

These are separate work tracks, not a single execution queue. A next task in one plan
does not authorize advancing every plan. Use the requested track's scope and acceptance checks.

## Guides and evidence

- [Boating performance](BOATING_PERFORMANCE.md): headless arcade benchmark contract, P2a verification, P3a baseline and C1 clock comparison.
- [Multiplayer protocol](MULTIPLAYER_PROTOCOL.md): protocol v6 byte layout, channels, validation
  envelope, authority rules carried on the wire, version policy and fixtures.
- [Architecture review](ARCHITECTURE_REVIEW.md): 2026-10-19 assessment of ownership, performance,
  iteration, and growth risks; the [foundations plan](BOATING_FOUNDATIONS_PLAN.md) owns the
  selected implementation sequence. These do not replace the gameplay plans.
- [Project README](../README.md): build, launch, controls, renderer options, course/replay storage.
- [3D renderer and scene guide](3D.md): current scene evaluation, CPU API, GPU materials, geometry.
- [Water review](WATER_REVIEW.md): dated 04e baseline followed by implemented 04f/04g records.
- [Generated boats and berths](HARBOR_BOATS.md): visual parameters, mooring/wave evaluation,
  contact envelopes, review commands, and the boundary for future playable boat variants.
- [Marine HUD](BOAT_HUD.md): instrument semantics, layout, integration handoff and capture commands.
- [Player speedboat skins](BOAT_SKINS.md): liveries, customization API, review commands and gameplay boundary.
- [Player flag](BOAT_FLAG.md): breeze, apparent wind, cloth, playback, and close review commands.
- [Harbor gulls](HARBOR_GULLS.md): boating flocks, social cues, vessel reactions, and review commands.
- [Harbor traffic](HARBOR_TRAFFIC.md): travelling craft, ferry, routes, moving contacts, and visual wakes.
- [Music workflow](MUSIC.md): boating XMs, generators, validation and runtime selection.
- [Windows builds](WINDOWS_BUILD.md), [macOS app builds](MACOS_BUILD.md),
  [automated releases](RELEASES.md), [browser boating](WEB_BUILD.md),
  [dance study](DANCE_STUDY.md), and [Emacs setup](emacs.md).
- The original waterfront plan, Eriksberg survey, living-city baseline audit, GPU migration plan
  and soundtrack plan described the removed demo and were deleted on 2026-10-25; read them in the
  git history. Dated progress entries elsewhere retain results for their original revisions.

## Verification baseline

Eriksberg location and demo removed, 2026-10-25, M1 Max/macOS 26.6.2, Debug: build
warning-free; all 110 active CTests passed (15 retired disabled). River CPU/GPU captures and a
windowed profiler run inspected; protocol v5; other platforms unchecked.
[Evidence](BOATING_PLAN.md#eriksberg-location-removed--2026-10-25).

Host from the game, 2026-10-25, M1 Max/macOS 26.6.2, Debug: build warning-free; all 124
active CTests passed (15 retired wake-field checks disabled, 147.94 s), including the new
`wake_velocity_serve_scripted`, `boat_host`, `wake_velocity_host_gpu` and three argument rejections.
Agent-run windowed host sessions, a killed host, a busy port and the relocated single-executable
Mac package checked; title panels inspected. User-played on macOS ("Works great!"); Windows
code uncompiled.
[Evidence](MULTIPLAYER_PLAN.md#host-from-the-game--2026-10-25).

Landing weight L1, 2026-10-24, macOS Debug: landing speed scrub (handling revision 8),
splash, camera dip and hull slap (tonal thump replaced after user feedback). Build warning-free; all 118 active CTests passed (15 retired
disabled); CPU/GPU landing captures inspected; race bots finish in 63.96–75.65 s. 2026-10-25:
camera dip removed (118 CTests passed); the user played it and liked the impact feel. [L1 evidence](BOAT_PHYSICS_PLAN.md#l1-candidate--2026-10-24).

Race smoke-test timing fix, 2026-10-24, macOS Debug: the bot now measures held simulation
ticks instead of service-loop iterations. Both v0.0.14/v0.0.15 contain the faulty counter.
Slow-polling reproduction fails before the fix and passes after it; the normal race check
also passes. All 118 active CTests then passed at `73e2c4f` (15 retired checks disabled); the v0.0.16
release and macOS/Windows/Linux/web runs there succeeded (checked with `gh`). [Evidence and next check](RACE_PLAN.md#race-smoke-test-timing--2026-10-24).

Closer camera / finer rain, 2026-10-24, macOS Debug: build and all 117 active CTests
passed; 15 retired wake-field checks stayed disabled. CPU/GPU storm captures inspected;
a 4,200-frame scripted windowed preview exited 0. User-approved ("Looks good. Commit.").
[Evidence and remaining checks](RACE_PLAN.md#closer-camera-and-finer-rain--2026-10-24).

Historical three-crest ferry wake, 2026-10-24, macOS Debug: project build and all 117 active CTests
passed (22 active GPU checks); 15 retired wake-field checks stayed disabled. Verified the
three-crest count/taper, CPU/GPU recipe parity, encounters and ENet race. CPU/GPU captures
inspected; a 1,800-frame scripted windowed run exited 0. User visually approved ("Looks great!").
Four bots finish in 63–71 s with 6–10 jumps and no recovery; human multiplayer handling
remains pending. [Detailed evidence](RACE_PLAN.md#three-crest-ferry-wake--2026-10-24).

Historical crest shading tweak, 2026-10-24, macOS Debug: warning-free project build and all 117 active
CTests passed (22 active GPU checks); 15 retired wake-field checks stayed disabled. CPU/GPU
captures inspected across gentle/default/double storm scales plus the ferry; a 4,800-frame
scripted windowed GPU run exited 0. Shading subsequently user-approved ("Much better"); see the
[owning record](RACE_PLAN.md#crest-shading-tweak--2026-10-24) for commands and limitations.

Historical higher cloud / irregular waves and rain, 2026-10-24, macOS, Debug: build passed without
project compiler warnings; all 117 active CTests passed, including 22 active GPU checks,
CPU/GPU storm recipe parity and ENet race. CPU/GPU captures inspected and an 80-second
scripted GPU preview completed. Bots finish in 65–76 s with 10–14 jumps and no recovery.
Combined storm presentation user-accepted 2026-10-24; next: multiplayer handling and pacing;
[current status and limits](RACE_PLAN.md#r3-skill-gate-layout--2026-10-24).

Historical first moving-wave recipe / stronger R3 course, 2026-10-24, macOS, Debug: build passed without project
compiler warnings; all 117 active CTests passed, including 22 active GPU tests and ENet race.
CPU/GPU captures inspected and an 85-second scripted GPU preview completed. Physical waves,
cloud, rain and lightning share the 8 m/s movement. Four bots finish in 66–81 s with 15–20
jumps (one recovery across the fleet). Human play of this revision remains next;
[current evidence and limitations](RACE_PLAN.md#r3-skill-gate-layout--2026-10-24).

Historical first R3 layout, 2026-10-24, macOS, Debug: build passed without project compiler warnings;
all 117 active CTests passed, including 22 active GPU tests and the ENet race. CPU/GPU captures
inspected and a 3,300-frame scripted GPU preview completed. Four bots finish in 48.5–49.8 s
with 8–9 jumps and no recoveries. Human gate-layout play remains next; detailed evidence and
limitations belong to the [R3 status](RACE_PLAN.md#r3-skill-gate-layout--2026-10-24).

Historical R2 verification: Race R2 network race, 2026-10-24, Apple Silicon/macOS 26.6.2: warning-free Debug build; all
95 non-GPU CTests pass, including the new `boat_net_race`; 22 of 24 GPU tests could not open a
window in the agent's environment and are unverified for R2. Bots raced over ENet; no human
race play. [R2 status](RACE_PLAN.md#r2-network-race--2026-10-24).
Historical R1 verification: Race R1 core, 2026-10-24, Apple Silicon/macOS 26.6.2: warning-free Debug build and all 116
active CTests pass, including 24 GPU checks and the new `boat_race` validator (four bots finish
the 927.5 m course in 45.8–51.7 s). No race room, HUD or human play exists yet.
[R1 status](RACE_PLAN.md#r1-race-core--2026-10-24).

Race R0 source-visibility correction, 2026-10-24, Apple Silicon/macOS 26.6.2: warning-free
Debug build and all 115 active CTests pass, including 24 GPU checks. The ferry now renders
in the mapped main/reflection/shadow passes; passing scenery boats in the trial have their
own bounded physical wakes. Revised CPU/GPU frames and a scripted window run were checked;
corrected-version human feel review remains open. [Feedback fix and verification](RACE_PLAN.md#r0-feedback-correction--2026-10-24).

Initial race R0 baseline (before the source-visibility feedback fix), 2026-10-24, Apple Silicon/macOS 26.6.2: Debug build and all 115 active CTests
pass with 24 GPU checks enabled. The shortened 957 m ferry-wake trial measures about 49 s
at full throttle, with 8–9 jumps across tested lines and 0/3/6 s arrival delays. CPU/GPU
captures were inspected and a scripted GPU window trial completed. R1–R4, human feel review
and the full downtown start remain open. [R0 evidence and limits](RACE_PLAN.md#r0-prototype--2026-10-24).

River hosting and open-river spawns, 2026-10-24, Apple Silicon/macOS 26.6.2: Debug build and
all 113 active CTests pass with GPU checks enabled. River/Eriksberg rooms negotiate their map
with protocol-v3 clients; four initial starts are about 60 m apart in open river water.
A windowed river deathmatch beside three hunting bots joined/exited cleanly; connected capture
inspected. Rebuilt clients are required on every machine. Other-platform qualification,
human play and full M7 map/resource coverage remain open.
[River hosting evidence](MULTIPLAYER_PLAN.md#river-hosting-handoff--2026-10-24).

Multiplayer M5 step 1, 2026-10-24, Apple Silicon/macOS 26.6.2: Debug build and all 111 active
CTests pass with GPU checks enabled. Four-player seeded profile-F lifecycle coverage exposed
and now guards a cross-channel bootstrap-before-Accept join race. Real-ENet lifecycle runs
passed one and two churn cycles; a windowed client joined beside three resetting bots and
exited cleanly. Long soaks, resource budgets, broader impairment/platform coverage and human
acceptance remain open; the user deferred the remaining M4 acceptance checks.
[M5 evidence and next task](MULTIPLAYER_PLAN.md#m5-step-1-four-player-lifecycle-handoff--2026-10-24).

Arcade stability integration, 2026-10-24, Apple Silicon/macOS: Debug build and all 110 active
CTests pass, including twenty active GPU checks and the current LAN-discovery checks.
The merged CPU basin capture matches the reviewed worktree candidate byte for byte.
[Integration evidence and remaining play checks](ARCADE_STABILITY.md#main-integration-2026-10-24).

LAN discovery, 2026-10-24, M1 Max/macOS 26.6.2: Debug build warning-free and all 110 active
CTests pass; `boat_bot --discover` found loopback and `--bind 0.0.0.0` rooms (two at once) on
one Mac, and the title's LAN row was inspected in temporary captures. Not tried between two
machines; Windows code uncompiled. [LAN discovery evidence](MULTIPLAYER_PLAN.md#lan-discovery-handoff--2026-10-24).

Deathmatch D2, 2026-10-24, M1 Max/macOS 26.6.2: Debug build warning-free and all 109 active
CTests pass, including the new `boat_net_deathmatch` loopback; in-game HUD inspected in screen
captures of an agent-run window beside two hunting bots. Not played by a human.
[D2 evidence](DEATHMATCH_PLAN.md#d2-protocol-v2-and-hud-handoff--2026-10-24).

Deathmatch D1, 2026-10-24, M1 Max/macOS 26.6.2: Debug build warning-free and all 108 active
CTests pass; free-mode scripted fleet output identical before and after; scripted and ENet
loopback deathmatch rounds credit capsizes in the server log. Not played by a human.
[D1 evidence](DEATHMATCH_PLAN.md#d1-match-core-handoff--2026-10-24).

Marine HUD and controls merged, 2026-10-24, M1 Max/macOS 26.6.2: Debug build warning-free and all
105 active CTests pass after cherry-picking `boat-marine-hud` onto `29d1afb`. Not hand-played;
not yet through Windows, macOS, Linux or web CI. [HUD status](BOAT_HUD.md).

Packaged LAN server, 2026-10-24, M1 Max/macOS 26.6.2: Debug build warning-free and all 104
active CTests pass; the packaged Mac app's relocated `boat_server` passed `--scripted` and the
ENet loopback smoke. Windows CI at `29d1afb` passed every step. macOS CI failed `boat_skin`,
`boat_replay` and `boat_net_loopback`; the causes (test/tool defects) are fixed and recorded in the
[handoff](MULTIPLAYER_PLAN.md#packaged-lan-server-handoff--2026-10-24); CI not yet rerun.
[Packaged server evidence](MULTIPLAYER_PLAN.md#packaged-lan-server-handoff--2026-10-24).

Multiplayer M4 step 4b, 2026-10-24, M1 Max/macOS 26.6.2: Debug build warning-free and all 104
active CTests pass; resets report progress and a dropped request expires. Agent-run windowed
check only; R not yet played. [Reset UI evidence](MULTIPLAYER_PLAN.md#m4-step-4b-reset-ui-handoff--2026-10-24).

Multiplayer M4 step 4a, 2026-10-24, M1 Max/macOS 26.6.2: after cherry-picking the Nordic skins,
Debug build warning-free and all 104 active CTests (22 GPU) pass; every remote boat is drawn as
an ordinary hull in its slot's country on CPU and GPU, and single-player captures match
`38d76c6` byte for byte (8/8). The user played several boats plus bots on one machine
("It works!"). [Fleet rendering evidence](MULTIPLAYER_PLAN.md#m4-step-4a-fleet-rendering-handoff--2026-10-24).

Multiplayer M4 step 3, 2026-10-24, M1 Max/macOS 26.6.2: Debug build warning-free and all 101
active CTests (21 GPU) pass; on profile F the other boat is drawn 106 ms behind the authority,
interpolated in over 99.8% of frames, within 5 mm of the authority's position at that tick.
Agent-run windowed check only. [Remote view evidence](MULTIPLAYER_PLAN.md#m4-step-3-remote-view-handoff--2026-10-24).

Multiplayer M4 step 2, 2026-10-24, M1 Max/macOS 26.6.2: Debug build warning-free and all 101
active CTests (21 GPU) pass; on profile F, predicted wakes dropped and re-added at corrections
fell from about 50 to 0–1 per client per minute, none visible. Agent-run windowed checks only;
one of three runs reported unexplained prediction holds. [Wake evidence](MULTIPLAYER_PLAN.md#m4-step-2-wake-reconciliation-handoff--2026-10-24).

Multiplayer M4 step 1, 2026-10-23, M1 Max/macOS 26.6.2: Debug build warning-free and all 101
active CTests (21 GPU) pass; `boat_net` adds bit-identical prediction replays (including
airborne, landing and capsized restores) and predicting rooms with own corrections of at most
2 mm on profile F. An agent-run windowed `wake_velocity --connect` played beside a bot. On
2026-10-24 the user drove it beside `boat_bot` and its wakes ("Works really well!"); no
two-human or two-machine session is claimed. [M4 evidence](MULTIPLAYER_PLAN.md#m4-step-1-client-prediction-handoff--2026-10-23).

Multiplayer M3, 2026-10-23, M1 Max/macOS 26.6.2: Debug build warning-free and all 101 active
CTests (21 GPU) pass, including the protocol/session validator, a real ENet loopback process
test and the net boundary check; fifteen retired field cases stay disabled. Single-player CPU
captures match `803a901` byte for byte. An agent-run windowed `wake_velocity --connect` joined a
room beside a bot. No keyboard play, two-machine session, user play or Windows/Linux/browser
build is claimed. [M3 evidence](MULTIPLAYER_PLAN.md#m3-protocol-and-transport-handoff--2026-10-23).

Multiplayer planning, 2026-10-23, Apple Silicon/macOS: documentation only; Debug
configure/build and all 83 active CTests (20 GPU) pass in 113.65 s with desktop access;
fifteen retired field cases stay disabled. The first sandboxed GPU attempt timed out and
was stopped. [Planning evidence](MULTIPLAYER_PLAN.md#verification-of-this-planning-change)
records the dependency review and limits. No network prototype, manual multiplayer play,
Windows/Linux/browser qualification or new audiovisual review is claimed.

Arcade review implementation, 2026-10-22, M1 Max/macOS: Debug build and all 83 active
CTests (20 GPU) pass in 121.79 s; fifteen retired field cases stay disabled. The Emscripten
build, inspected captures and visible scripted basin-rival/river runs pass. A subsequent
[local browser smoke](WEB_BUILD.md#current-arcade-build-local-smoke--2026-10-22) verifies
Chrome/SwiftShader river/basin startup, rendering and help/reset without runtime errors. The
[implementation record](WAKE_ARCADE_IMPLEMENTATION.md) owns outcome sweeps, full-pool GPU
cost, the tight-turn overlap investigation and remaining human/platform checks.

Routine test selection and timings are owned by [the test workflow](TESTING.md).
On 2026-10-26 (M1 Max/macOS, Debug, GPU tests off), `ctest --preset quick` ran 133 quick tests
in about 1 s. `ctest --preset full` ran all 220 enabled CPU tests in 18.7 s, down from 71.6 s
for 98 tests earlier that day; with GPU tests on, all 246 passed in 60.7 s. Later that day the
network tests moved to a virtual clock and tests may no longer wait on wall time: 227 CPU tests
in 11.2 s, 140 quick ones in 0.75 s. Heavy validators are split into `<prefix>.<case>` tests. GPU
tests run one at a time under a shared resource lock beside the CPU tests. Fifteen retired
physical-field experiments stay disabled by default.


Wake continuity revision, 2026-10-22, macOS / M1 Max: Debug/optimized builds, all 97
CTest cases (23 GPU checks), final focused object checks, portable ASan/UBSan and the
Emscripten build pass. Before/after basin/river GPU and current basin CPU captures were
inspected; visible scripted jumping/landing completes. [Continuity results](WAKE_OBJECT_SMOOTHING.md)
own the denser/smoother source, 128-piece bound, .37–.40 ms observed cost increase,
verification and limits. The user subsequently enjoyed this revision and requested it as
the default; [default promotion](WAKE_OBJECT_SMOOTHING.md#default-promotion) owns the switch
and the remaining stuck-wave report.

Initial arcade wake objects, 2026-10-22, macOS / M1 Max: Debug/optimized builds and all 97 CTest
cases pass after correcting/rerunning the object GPU comparison, including 23 GPU checks.
Portable ASan/UBSan, bounded four-emitter stress, actual jumping/recovery, CPU/GPU recipe
parity, inspected captures, a visible scripted run and the Emscripten build pass. [Object results](WAKE_OBJECT_RESULTS.md)
own exact evidence and scope. The user subsequently reported enjoying the mode;
[continuity follow-up](WAKE_OBJECT_SMOOTHING.md) owns the subsequent revision and default promotion.

Earlier compact packet experiment, 2026-10-22, macOS / M1 Max: Debug build and all 92 CTest
cases pass across split runs, including 22 GPU checks after resuming with desktop access.
Portable packet ASan/UBSan contracts, four quadrature sweeps, doubled-reference control,
inspected diagnostic plots and documentation checks pass. [Packet results](WAKE_PACKET_RESULTS.md)
record the fixed-quadrature rejection, strong returning encounter and remaining limits.
At that checkpoint large-world runtime acceptance and the 2 ms target remained open.
The subsequent arcade direction above supersedes its proposed next task; D3 remained default
at this historical checkpoint.

Wake performance follow-up, 2026-10-21, macOS / M1 Max: Debug configure/build and all
91 CTests passed, including 22 GPU checks (349.92 s). Native and portable sanitizer checks,
identical CPU/GPU captures, scripted desktop animation and the Emscripten baseline build
pass. [Scaling results](WAKE_SCALING_RESULTS.md) record the 29.2% measured physics reduction,
frame percentiles, size sweep and remaining limits. At that checkpoint the 2 ms target
failed and large-world simulation remained open. The subsequent [packet experiment](WAKE_PACKET_RESULTS.md)
then narrowed the physical-track proposal; arcade object play review is now the active task.

Wake candidate, 2026-10-21, macOS / M1 Max: Debug configure/build and all 91 CTests passed,
including 22 GPU checks. Matched 80 s boundary/island and 160 s basin-domain proofs,
source/query convergence, physical crossing/recovery, CPU/GPU captures, Instruments profiling,
scripted desktop animation, portable sanitizers and browser-baseline smoke checks are recorded
in [candidate results](WAKE_CANDIDATE_RESULTS.md). The user reports revision 3 looks and feels
much better; the subsequent [performance/large-water follow-up](WAKE_SCALING_RESULTS.md)
takes priority over water appearance. At that checkpoint the 2 ms wake target failed and D3 remained
default, and broader scenario/platform review is not inferred from these checks.

Swedish flag, 2026-10-20, macOS: Debug build and all 77 CTests passed, including 20 GPU
checks; seven affected checks passed again after shortening the staff. CPU/GPU captures
and scripted desktop animation were checked. The user accepted the final flag;
[the boating record](BOATING_PLAN.md#swedish-ensign--2026-10-20) owns details and limits.

Handling/speed pass, 2026-10-19, macOS: Debug configure/build completed; all 69 CTests
passed across the full run and targeted handling rerun, including 18 GPU checks. CPU/GPU
frames were inspected and an Eriksberg GPU+AA desktop demo smoke run completed. Countersteer
recovery improved in deterministic trials. The user accepted the handling/speed-view pass on
2026-10-19: "Feels great!" Old personal-best files are preserved separately from handling
revision 2. Exhaustive scenario and platform coverage remain unverified. Detailed measurements and limits are in the
[physics feel-pass record](BOAT_PHYSICS_PLAN.md#handling-feel-pass--2026-10-19).

All-location/default-wake update, 2026-10-18, macOS: Debug build and all 69 CTests passed,
including 18 GPU checks. Default and explicit player-wake captures match byte for byte;
the opt-out matches the earlier calm frame. Mapped profile/source-clearance checks,
CPU/GPU captures, and a desktop river smoke run passed. Courses/replays retain their
original settings. Details and continuing manual/platform limits belong to the
[physics handoff log](BOAT_PHYSICS_PLAN.md#handoff-log).


User acceptance, 2026-10-18: Phase D handling is provisionally accepted as feeling good,
with continued evaluation over time. Revised player-wake motion was also accepted.
Preserve this baseline; optional Phase E has not started. This supersedes pending
handling status in the historical verification entries below. See the
[physics handoff log](BOAT_PHYSICS_PLAN.md#handoff-log) for scope and limitations.


Player-wake motion refinement, 2026-10-18, macOS: Debug build and all 66 CTests passed,
including 18 GPU checks. A new propagation test tracks crest travel independently of the
packet; CPU/GPU parity and loop/recovery checks passed. Desktop scripted playback and
sampled native captures passed. The user accepted the revised player-wake motion on
2026-10-18; broader Phase D handling review remains pending. See the
[physics handoff log](BOAT_PHYSICS_PLAN.md#handoff-log) for current speeds and evidence.


Player-path wake follow-up, 2026-10-18, macOS: Debug build and all 66 CTests passed,
including 18 GPU checks. The expanded full-history shader parity probe and environment
checks also passed afterward. Scripted loop/recovery and desktop playback passed; sampled
CPU/GPU basin and mapped captures were inspected. Human handling/artistic acceptance is
pending. Details and limitations belong to the
[physics handoff log](BOAT_PHYSICS_PLAN.md#handoff-log).


Phase D, 2026-10-18, macOS: Debug build and all 62 CTests passed, including 16 GPU
checks. The authored basin current/wake profiles passed scripted recovery and desktop
playback checks; 160 calm-profile CPU frames matched the pre-D baseline byte for byte.
Human handling/artistic acceptance remains pending. See the
[physics handoff log](BOAT_PHYSICS_PLAN.md#handoff-log) for detailed evidence and limits.


Boating music, 2026-10-18: Debug build and all 54 CTests passed across CPU and
unsandboxed desktop graphics runs (14 GPU tests); the final XM was rechecked after
regeneration. Desktop boating audio smoke passed. The user accepted the joyful music
on 2026-10-18; exhaustive focus/replay listening remains unverified.
See [the boating music record](BOATING_PLAN.md#joyful-background-music--2026-10-18).

Boating-audio change, 2026-10-18: a peaceful water-led mix supersedes the earlier engine
revisions following user feedback and a second Astra consultation. Debug build and offline
engine/water validation, all 53 CTests (including 14 graphics tests), and the desktop CPU
wave-demo smoke run passed. Detailed evidence is tracked in
[the audio record](BOATING_PLAN.md#peaceful-water-mix-verification--2026-10-18).
The user accepted the peaceful mix on 2026-10-18; it is the audio baseline.
Exhaustive manual control/replay coverage remains unverified.

On the development Mac, 2026-10-18, Debug configure/build and all 52 CTests passed,
including 14 graphics-context tests with `PROTEUS_GPU_TESTS=ON`; rerun after the
boating-gull addition (see the living-city log). This verifies the
automated suite at this revision; it does not establish Windows behavior, human motion/audio
assessment, or musical acceptance. See the owning plans for existing manual evidence and limits.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_GPU_TESTS=ON
cmake --build build -j
ctest --test-dir build -j 4 --output-on-failure
```

GPU tests require a desktop graphics context. In display-free environments configure a separate
build with `-DPROTEUS_GPU_TESTS=OFF` and report that GPU checks were omitted.

## Keeping this accurate

Follow [AGENTS.md](../AGENTS.md#documentation-maintenance) at every step: update the owning
plan and affected guides together, reconcile superseded claims, and distinguish implemented,
verified, and user-accepted work. Refresh the overview when milestones or next tasks change.
Keep test results dated and historical entries explicitly scoped to their original revision.
Ignored editor autosaves are not additional plans.
