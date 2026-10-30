# Browser boating build

## Current status

Implemented in the isolated `web-parity` worktree, 2026-10-25: the browser enters the shared
Wake Velocity title after the page's PLAY gesture. SINGLE PLAYER opens PLACE (River, Basin,
Basin trial), WAVES, SMOOTHING and START. SETTINGS remembers name, single-player boat livery,
steering, soundtrack and smoothing. The saved smoothing setting applies to single-player and
multiplayer. P opens/closes the shared in-game menu while staying fullscreen; Escape also opens it,
but the browser may leave fullscreen. BACK TO TITLE returns to setup
without reloading. BACK TO LAUNCHER flushes saves and reloads the page. The small HTML launcher
retains fullscreen selection, remembered separately in browser storage. Gameplay/title settings
use the native `title.txt` format through the existing IndexedDB mount; older saves get the new
river/waves/smoothing defaults. IndexedDB denial still permits play without durable settings.

Browser multiplayer now uses a JavaScript WebRTC transport and the existing C protocol,
client prediction, wake reconciliation and remote rendering. The optional native endpoint accepts
browser players alongside ENet clients in the same four-player authority. MULTIPLAYER offers a
named SERVER choice, manual URL, CONNECT and BACK; browser hosting and UDP LAN discovery are unavailable.
See [WebRTC setup and limits](#webrtc-rooms). Normal native/release builds remain ENet-only.
The GitHub Pages build includes the browser client; Pages does not provide a game server.
The [Winterfell hosting guide](MULTIPLAYER_HOSTING.md) owns the separately deployed released
room, HTTPS signaling and private TURN relay, including current verification limits.
For internet play on that room, launch the pinned client at
`https://wake.davve.net/` or [GitHub Pages](https://doomdavve.github.io/proteus/) and choose
MULTIPLAYER → WINTERFELL. Both use the public `wake.davve.net` directory and WebSocket
endpoint, outside the VPN's private Winterfell DNS override. Tag pushes deploy Pages; release completion also requires the matching Winterfell cutover.
The manual Pages workflow can republish a selected
published release with a changed server-directory URL without changing the game version.

The existing Emscripten/WebGL 2 build, click-to-start audio, IndexedDB bests, content-hashed
packaging and `v*` Pages deployment workflow remain in use. Both music tracks and the shared
title backdrop are bundled. One browser audio device starts during PLAY and remains alive
across title/session transitions; Off disables the soundtrack, retaining engine/water sound.

Basic mobile controls and iPhone setup are described [below](#mobile-controls-and-iphone-presentation-2026-10-28).
Next: qualify WebRTC on Firefox/Safari and physical devices, test real loss/latency and tab
suspension, measure memory/runtime budgets, and complete internet-play acceptance of the
[Winterfell deployment](MULTIPLAYER_HOSTING.md). User play/listening acceptance remains outstanding.
The earlier launcher and browser qualification records below are dated history.

## Shared app icon, 2026-10-28

The launcher SVG, iOS 180 px Home Screen PNG and Android 192/512 px manifest PNGs now use
a compact bridge-and-river scene drawn from the daylight menu backdrop. They share one
editable SVG with 1024 px PNG, Windows ICO and macOS ICNS exports. See the
[asset guide](../assets/icons/README.md) for regeneration and the
[title plan](TITLE_SCREEN_PLAN.md#app-icon-2026-10-28) for design and verification status.
Existing installed Home Screen icons may need removal and re-adding to refresh their cached
artwork. Physical device review remains pending.

## On-demand scenery, 2026-10-28

Since A3.5i the maps' land is in scenery packs rather than the wasm
([result](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a35i-result-2026-10-28)). The river pack is
preloaded in `.data`. The outer pack is copied to `build-web/web-lazy/geometry/outer/` and
packaged as `scenery-outer-<hash>.bin`. `web/scenery_loader.js` fetches it the first time an
outer session (solo or a room) starts and writes it into the in-memory file system, while the
game shows LOADING OUTER RIVER with its percentage. A second outer session reuses it.
`tools/package_web.py` prints the initial and on-demand bytes. Before A3.5i the page downloaded
about 6.3 MB gzipped before play; now about 2.9 MB, plus 3.3 MB for the outer river (5.3 MB
since A3.5f extended it upstream past Hisingsbron; the pack is already DEFLATE-compressed).

Check it against a served build (Playwright and Chrome; the script is optional and not in CTest):

```sh
python3 -m http.server 8000 --directory build-web/site &
python3 tools/check_web_scenery.py --output build/web-scenery
python3 tools/check_web_scenery.py --output build/web-scenery-slow --throttle-kbps 200
```

Verified 2026-10-28 on macOS arm64, headless Chrome with SwiftShader: one outer fetch, only at
the outer start; the outer map drawn; no re-fetch. A failed fetch stops the game with a console
error (reloading retries); there is no offline cache beyond the browser's own HTTP cache.

## Mobile controls and iPhone presentation, 2026-10-28

Basic mobile boating is implemented on `mobile-web-controls`. Touch devices with a coarse
primary pointer show left/right steering, hold-to-go Forward/Reverse, and Menu during a
session. Releasing the pedals selects neutral; pressing both also selects neutral. Menu
provides reset/recovery and return to title. Title choices and gameplay menus accept completed
touch taps, including taps shorter than a rendered frame. Keyboard and mouse remain available
on desktop. Mobile weapon, camera, replay and text-editing controls are deferred; the existing
player name is usable for joining a listed room.

Tap **Enable tilt**, allow orientation access, and hold the phone comfortably to calibrate.
Tilt left/right to steer; **Recenter** resets the holding position and **Use touch** disables
sensors. Both landscape directions and portrait are supported. Rotation neutralizes steering
until Recenter. Touch steering overrides tilt, and keyboard steering overrides mobile steering.
The initial tuning uses a 3-degree dead zone, full lock at 25 degrees and 100 ms smoothing.
Flat-on-a-table positions do not provide a useful gravity-based screen roll; raise the phone
slightly. Touch steering remains available when permission is denied, readings stop or sensors
are unavailable. Tilt requires HTTPS (localhost is suitable for desktop automation); permission
is requested from its own button, separately from PLAY and audio initialization.

On mobile the game fills the visible viewport, with translucent controls over the scene and
inside safe areas. The GPU scene adapts to the viewport aspect ratio, keeping geometry, instruments
and centred menus proportional in portrait and landscape. The browser uses a low-resolution
viewport of at least 320×200, bounded to a 3:1 aspect range, and retains nearest-neighbour
filtering. Browser-bar changes settle for 150 ms before target reallocation, retaining shaders
and scenery caches. Desktop retains its original 320×200 scene and integer scaling. Forward
is stacked above Reverse on the right; portrait instruments sit above the bottom steering buttons. Fullscreen is feature-detected; supported
mobile browsers have a Fullscreen/Exit fullscreen button. Fullscreen requests prefer hidden
navigation UI and clear held touch input on entry/exit. iPhone Safari does not reliably offer element fullscreen:
use **Share → Add to Home Screen → Open as Web App**, then launch from its icon for more screen
space. Relative manifest/icon URLs support repository subpaths. This adds no service worker or
offline-play promise. See [Apple's instructions](https://support.apple.com/guide/iphone/open-as-web-app-iphea86e5236/ios)
and the [WebKit fullscreen issue](https://bugs.webkit.org/show_bug.cgi?id=206854).

Automated checks:

```sh
node tools/test_web_mobile_controls.cjs
python3 tools/check_web_mobile.py --url http://127.0.0.1:8000/ --output build-web/mobile-review
python3 tools/check_web_mobile.py --url http://127.0.0.1:8000/ --android --output build-web/android-review
```

Android review: Chrome-style orientation access is enabled by the same explicit Tilt button;
when `requestPermission` is absent, the browser listens directly rather than treating that as
an error. HTTPS and device sensor/site settings still govern access. Touch controls work if
sensors are blocked. Pointer capture, cancellation, blur, tab backgrounding and fullscreen
transitions release held controls. Orientation is not locked. The manifest permits either
orientation and includes 192px/512px raster icons for Android installation, alongside the SVG
and iPhone icon. Long-press context menus and page overscroll are suppressed during play.
See [orientation access](https://developer.mozilla.org/en-US/docs/Web/API/Device_orientation_events/Detecting_device_orientation),
[fullscreen behavior](https://fullscreen.spec.whatwg.org/) and
[manifest guidance](https://web.dev/articles/add-manifest).

The Node contract test is also registered in native CTest when Node is available. The optional
Chrome/SwiftShader check drives touch-only setup, simultaneous steering/throttle, simulated
tilt, menu/reset, portrait resizing and return to title against the actual Wasm build. Add
`--room-url ws://127.0.0.1:24443` for a matching local WebRTC room; this also checks emitted
steering/throttle commands and leaving the room.
Physical iPhone tilt permission, steering feel, audio playback, Safari bars and Home Screen behavior
still require manual review. Automated emulation is not physical-device or user acceptance.
Verification on 2026-10-28, Apple Silicon/macOS: the initial native Debug build and full CTest
preset passed against current main (233 enabled tests; 13 retired wake-field checks disabled),
as did the Emscripten MinSizeRel package. Chrome/SwiftShader mobile automation passed solo
setup, simultaneous touches, simulated tilt, reset and return to title. The matching optional
native WebRTC endpoint built; local-room automation verified outgoing tilt/throttle commands
and touch-menu leave. Existing desktop mouse and title/persistence/repeated-session checks
passed. The title regression check now accounts for the existing Credits row.

The user rejected the original small centred mobile presentation, then accepted landscape
with controls over the game on iPhone via LAN HTTP. Portrait was reported stretched, and
Forward above Reverse was requested. The current revision renders to the viewport aspect
ratio, removes the landscape recommendation and stacks the pedals. Both phone-emulated
portrait/landscape/menu captures were inspected. The Android-emulated check also passes
implicit orientation access, default fullscreen launch with a running audio context,
fullscreen entry/exit, cancellation of a held pedal on exit,
proportional portrait layout and touch-menu navigation. Android install icons are packaged.
No physical Android device was available.

The revised native full suite with `PROTEUS_GPU_TESTS=ON` passed (259 enabled tests, 15 retired
wake-field checks disabled); GPU resize round-trip coverage additionally checks exact frame
parity after portrait/landscape/default resizing with smoothing off and on. The native GPU
river demo (`--demo --frames 120`) is an agent-run startup/playback smoke check. Physical-device
acceptance of the new portrait layout and vertical pedals, real Android/iPhone tilt permissions,
Home Screen installation, performance and human audio/steering review remain pending.
Next: collect that physical-device feedback. Automated emulation does not establish hardware
or user acceptance. The first sandboxed full suite could not open local sockets; rerunning
with socket access passed.

## Multiplayer anti-aliasing, 2026-10-27

Implemented on `web-antialiasing`: title-launched multiplayer now passes the saved
`smooth_edges` setting to the shared GPU renderer, just as single-player does. SMOOTHING
is also available in SETTINGS before joining a room; the SINGLE PLAYER control edits the
same saved setting. New profiles default to ON, and existing OFF preferences are preserved.
The existing 1280×800 scene and 4×4 box downsample still produce a 320×200 image with integer,
nearest-neighbour presentation. Native JOIN/HOST uses the same launch path.

The optional browser check exercises SETTINGS and joins a compatible test room twice with
fresh browser profiles, once with smoothing ON and once OFF. It checks actual WebGL render
viewports, captures settings/gameplay and rejects runtime or shader errors:

```sh
python3 tools/check_web_aa.py --url http://127.0.0.1:8000/ \
  --room-url ws://127.0.0.1:24443 --output build-web/aa-review
```

Verification on 2026-10-27, Apple Silicon/macOS: Debug configure/build and all 256 enabled
CTests pass with `PROTEUS_GPU_TESTS=ON` (74.28 s; 15 retired field checks disabled).
Emscripten MinSizeRel packaging and the matching optional native WebRTC endpoint build.
Chrome/SwiftShader joins a local basin room with smoothing ON and OFF; actual scene viewports
include 1280×800 only when ON. Settings and gameplay captures in `build-web/aa-review/`
were inspected. The existing `check_web_title.py` title/persistence/repeated-session check
also passes. A windowed native river `--gpu-aa --demo --frames 120` run exits cleanly.
The software-rendered AA capture shows a transient STALE network indicator; this check
qualifies AA selection/rendering, not multiplayer performance or input responsiveness.
Next: qualify physical-device performance and Firefox/Safari; human play acceptance remains
outstanding. The initial sandboxed GPU suite was stopped and rerun with desktop access.

## Deathmatch weapons verification, 2026-10-26

Historical 2026-10-26 verification: protocol v6 shipped the original two weapons. Current
v7 adds the arsenal described in [deathmatch](DEATHMATCH_PLAN.md#arsenal-milestone--2026-10-27); browser requalification is pending. Emscripten MinSizeRel builds; the optional
native WebRTC endpoint builds; mixed Chrome/SwiftShader and native-hunter automation passes
weapon commands, bounded combat page reception, rendered grenade/HUD, two resets and rejoin.
Both production radial shader recipes pass independent WebGL2 float probes (2873 each,
maximum absolute error 0.00001442). [Detailed evidence and captures](DEATHMATCH_PLAN.md#weapon-milestone--2026-10-26-superseded-rules-historical-evidence).
No native desktop graphics/audio, Firefox/Safari, physical-device or human balance pass is inferred.

## Known browser servers, 2026-10-26

The source now fetches Winterfell’s public `servers.json` at startup and when MULTIPLAYER opens.
Winterfell is the first/default named SERVER choice; Enter joins without typing. Manual
URL / CONNECT remain available if the list cannot load. The list has bounded schema and
protocol/physics metadata, and refresh failures preserve a previously loaded list. Its
[operator guide](MULTIPLAYER_HOSTING.md#known-server-directory) owns the fields and update
commands. Pages uses the same authoritative directory through its public CORS endpoint,
so deployment can update compatibility metadata without republishing Pages. The default
launcher includes public STUN on every host, including Pages.

Both features were introduced as `v0.0.17-server-directory`; Winterfell now serves the exact
v0.0.21 client, built on the server by the `davve-srv` baseline role. The public directory
and launcher at `wake.davve.net` report protocol 7 / physics 9 and use
`wss://wake.davve.net/room`. On 2026-10-26, a fresh Chrome/SwiftShader run of the published
Pages build fetched Winterfell, joined that room and drove without local-network permission.
First the test overrode only the launcher URL; after PR #14 merged and Pages run
[40391231764](https://github.com/doomdavve/proteus/actions/runs/40391231764) completed,
a fresh Chrome run against the actual published URL also fetched the list, joined and drove.
The live launcher and hosted room were upgraded together to v0.0.21 on 2026-10-28;
[current evidence](MULTIPLAYER_HOSTING.md#status-and-verification--2026-10-28) owns the
verification limits. Every release includes a server cutover through the
[davve-srv workflow](MULTIPLAYER_HOSTING.md#routine-deployments-davve-srv). Native clients
also fetch the directory for native addresses and retain LAN/manual joining. Human gameplay
and wider browser/platform checks remain.

Run the directory contract check with Node, or test a served released-compatible client:

```sh
node tools/test_web_server_directory.cjs
python3 tools/check_web_server_directory.py --url https://wake.davve.net/ --output build/directory-review
python3 tools/check_web_server_directory.py --url https://doomdavve.github.io/proteus/ \
  --directory-url https://wake.davve.net/servers.json --output build/pages-preview
python3 tools/check_web_server_directory.py --url https://doomdavve.github.io/proteus/ \
  --output build/pages-live
python3 tools/check_web_server_directory.py --url https://wake.davve.net/ --unavailable --output build/directory-fallback
```

The optional Playwright check joins the named room without text input; `--directory-url`
tests a launcher-only endpoint change before publishing, and `--unavailable` simulates a
directory HTTP failure and uses the manual fallback instead. Both leave the server running
and close only their own browser client.

## Pedal controls update, 2026-10-26

The isolated `controls-pedals-stability` branch replaces stepped/scroll thrust with held W/S
(or Up/Down) forward/reverse pedals, neutral on release and Shift precision. R is inactive;
Space operates the water cannon in deathmatch; E drops a stern wave grenade; Q launches a torpedo; V toggles the rearview mirror (hidden by default). Space stays unused in free/race play. Backspace is prevented from browser navigation while
the canvas is focused and resets only after a one-second active hold. Escape also offers an
explicit reset/restart/request. Captured mouse wheel hold remains, with automatic aerial
stabilization instead of steering rolls. [Current controls and integration evidence](CONTROLS_PEDALS_HANDOFF.md).
The dated mouse/scroll qualification below remains historical: its adapter still drains and
normalizes scroll for its event contract, but scroll no longer affects propulsion.

```sh
python3 tools/check_web_pedals.py --url http://127.0.0.1:8000/ --output build-web/pedals
```

This optional Playwright check samples the actual rendered thrust needle for power/neutral,
precision, opposing pedals and ignored scroll; compares held steering around R, Space and a
cancelled reset; and captures reset progress and the menu action. Multiplayer integration now
checks both shortcut and menu reset messages explicitly before asserting two completions.

## Build and deployment

Install CMake 3.24+, Python 3 and the official Emscripten SDK. The toolchain is pinned in
[web/emsdk-version](../web/emsdk-version); the workflow also pins the official SDK checkout.
Keep SDK updates coordinated with browser smoke checks. With an SDK checkout at `../emsdk`,
run from the repository root:

```sh
../emsdk/emsdk install "$(cat web/emsdk-version)"
../emsdk/emsdk activate "$(cat web/emsdk-version)"
source ../emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-web --target web --parallel
cmake --build build-web --target web-serve
```

Open **http://127.0.0.1:8000/** and press PLAY to enable audio and enter the title.
Stop the server with Ctrl+C. Serve `build-web/site/` over HTTP; opening its HTML as a
local file does not work. Keep native builds in `build/`. Browser play needs WebAssembly
and WebGL 2; see [playing instructions](PLAYING.md) for controls and saved-run limits.

### GitHub Pages setup

The **Web boating / GitHub Pages** workflow deploys on `v*` tag pushes. Ordinary branch
pushes do not update the site. Set up GitHub once:

1. Under **Settings → Pages → Build and deployment**, select **GitHub Actions**.
2. If the **github-pages** environment restricts deployment refs, allow **Tag** `v*`
   and **Branch** `main` for manual deployments. A branch rule named `v*` does not allow tags.

Use the [release command](RELEASES.md#release-command) for a complete release, including
matching native packages and hosted room cutover. Manual workflow runs deploy from `main`
or a `v*` tag; other refs only build. Only `build-web/site/` is published. Content-hashed
JavaScript, Wasm and assets use no service worker; reload to load a new version. GitHub's
CDN may briefly cache old HTML.


The Pages and native release workflows run independently on the same pushed version tag;
a failure in one does not roll back the other. Any matching tag, including prereleases,
publishes to the same site. Deployments are serialized without cancelling an active run;
the last successful deployment wins, with no semantic version ordering. Publishing a
GitHub Release for an already-existing tag alone does not trigger this Pages workflow;
push a new tag containing it or dispatch the workflow manually. The `github-pages`
environment must permit **Tag** refs matching `v*` (and **Branch** `main` for manual main
deployments), as described in GitHub's
[environment setup guide](https://docs.github.com/en/actions/how-tos/deploy/configure-and-manage-deployments/manage-environments).

[CMakeLists.txt](../CMakeLists.txt) defines the same boat executable and static boating
libraries for both platforms. Emscripten selects raylib 6.0's `PLATFORM=Web`, OpenGL ES 3.0,
and a static library, then [cmake/Web.cmake](../cmake/Web.cmake) configures the web-only
output and applies its size optimization to the executable and each boating library. Native
validators and studies are excluded from web configuration.
No CTest executables are run as web programs.

The first configure needs network access to fetch raylib; SDK installation also needs network.
A local raylib checkout can be reused with CMake's `FETCHCONTENT_SOURCE_DIR_RAYLIB` option.
The web build requires GNU extensions only in raylib for its `EM_ASM` macros; application
sources remain C17. Its POSIX clock calls are enabled through `_POSIX_C_SOURCE=200809L`.

The native `--wake-field` review candidate is explicitly unavailable in Web builds.
[Web.cmake](../cmake/Web.cmake) removes its wide field sampler from generated WebGL shaders;
The native field sampler stays unavailable; the current arcade object default and D3
selector share the existing browser launcher. The portable cosine backend is
numerically tested on the development Mac, but no browser candidate performance or query
latency is accepted. See [candidate limits](WAKE_CANDIDATE_RESULTS.md).

## Runtime decisions

- **Loop:** raylib's documented Asyncify approach preserves the existing synchronous loop,
  fixed physics steps, and cleanup. Its `WindowShouldClose()` yields to the browser.
  This costs some binary size and CPU time versus extracting a callback and moving the
  loop's many local variables into long-lived state. That refactor is not needed for this
  port; consider it only if measured browser performance warrants it. Optimized builds,
  a 1 MiB C stack, 64 KiB Asyncify stack, and growable memory are configured.
- **Graphics:** existing GLSL 330 sources are converted at build time to GLSL ES 300 with
  precision declarations. The sky/downsample fragments match raylib's default vertex
  shader precision. Shader bodies are shared except the disabled native wake sampler; source assets are never overwritten.
  WebGL 2 supports the renderer's depth textures, derivatives, integer operations, and
  texel fetches. Antialiasing is optional because it shades sixteen times as many pixels.
- **Window:** desktop retains a 960×600 drawing buffer containing a 320×200 scene and
  whole scene-pixel presentation scales. Mobile gameplay adapts the logical scene and drawing
  buffer to the visible viewport's aspect ratio; CSS scales it uniformly with nearest-neighbour
  filtering. The title retains its centred 320×200 layout. Page code owns CSS sizing rather
  than raylib's web resize handler. Fullscreen can be selected before launch or toggled during
  mobile play. P/Escape or the mobile Menu button opens the shared in-game menu
  (P preserves fullscreen); title and sessions share the window.
- **Assets:** preload both game XM tracks, the title backdrop, bitmap menu fonts and
  their attribution, textures and used shaders into `/assets`.
  `boat_asset_path()` already resolves that path under Wasm. Geometry is compiled from C
  headers; geometry manifests, editor samples, dance files, and unrelated music are not
  runtime dependencies. The engine/water sounds are synthesized by the existing C audio code.
- **Audio:** preload before enabling Play; invoke C `main` synchronously from its click
  handler so raylib/miniaudio starts audio during a user gesture, before the title yields.
  Session cleanup unloads streams while retaining that device until the title exits. The same XM
  decoder and streaming music code are used as on desktop.
- **Storage:** mount IDBFS at `/saves` with `autoPersist`, set `XDG_DATA_HOME=/saves`, and
  restore IndexedDB before enabling Play. The existing POSIX path, temporary-file/rename
  saves, format validation, bests, ghosts, and replay code operate unchanged. Browser writes
  are asynchronous; closing immediately after finishing can lose a pending write. Storage
  denial during restoration is reported on the page and gameplay can continue. Browser
  quota/eviction/private-mode policy can still prevent durable saves; these are not backups.
- **Delivery:** [tools/package_web.py](../tools/package_web.py) hashes the actual JS/Wasm/data
  bytes and writes the URL mapping into `index.html`. Relative URLs work under a repository
  Pages subpath. There is no service worker or preload cache. Only the generated `site/`
  directory is published; raylib's license is included. SDK/build/cache files stay local.

## Plugin source bundles (P4.3)

Development browser builds now include the opt-in **PLUGIN VOYAGE** panel. It imports/exports
version-1 JSON source bundles, loads the Långedrag example, validates in a disposable Web Worker
and launches a solo `outer` voyage. Live edits use Stage/reload, explicit Apply and Undo;
opening the panel pauses solo input. Normal PLAY still opens the shared title.

Only `voyage.define` descriptors are supported. Objectives/timers and race imports are
explicitly unavailable; no imported Lua executes on the game thread. A completed receipt is
bound to exact bytes and the current revision/commit epoch before native schema/map checks.
Cancellation/deadline/late results cannot install a revision. Each UI and game reload lane
owns at most one job; the browser transport caps total entries at two. See the
[API contract](PLUGIN_API.md#browser-source-bundles-p43) for bundle bytes, transactions and limits.

Source persistence uses localStorage, separately from existing IndexedDB game settings/bests.
Denied/corrupt storage leaves import, validation, play and export usable and reports the loss
of persistence. Saved source always requires revalidation after reload. No world progress,
callback state or Undo history is persisted. Example/worker JS/Wasm URLs are content-addressed
and lazy; the packaged site now includes `lua-LICENSE.txt`. Deploy the complete `site/` output.

```sh
python3 tools/plugin_author.py export plugins/langedrag > build/langedrag.wake-plugin.json
node tools/test_web_plugin_author.cjs
# With a built site served on port 8000, Playwright installed and its engines downloaded
# (`playwright install chromium firefox webkit`); --executable selects system Chrome instead:
for engine in chromium firefox webkit; do
  python3 tools/check_web_plugin_author.py --browser $engine --url http://127.0.0.1:8000/ \
    --output build-web/plugin-browser-check/$engine
done
```

Verification, 2026-10-29: Emscripten 6.0.12 MinSizeRel game/worker packaging passes on Apple
Silicon/macOS. The Node contract and local Chrome/SwiftShader check pass export/import,
objective/race rejection, real worker launch, live rename, syntax rejection, held water
Apply/Undo, stale results across Undo, cancellation/late completion, restored source/new
sessions and denied source storage. Gameplay and authoring captures were inspected. The
[plugin plan](PLUGIN_PLAN.md#p43-result-2026-10-29) owns detailed evidence and next work.
Verification, 2026-10-30 (P4.8): the same check, extended with 20 repeated worker validations,
Chromium tab freeze/resume (including a deadline across a freeze), navigation mid-validation
and three restored sessions, passes on Playwright Chromium, Firefox and WebKit and on system
Chrome, locally on Apple Silicon. The PR browser workflow now runs it on all three engines. See
the [P4.8 result](PLUGIN_PLAN.md#p48-result-2026-10-30) for timings and limits. Safari, mobile
browsers, physical devices, real background-tab policies and worker/browser-process memory
remain unqualified; there is no human play acceptance or deployment claim. PR browser CI builds these artifacts
without publishing them; the release/Pages deployment workflow retains its existing policy.

## WebRTC rooms

The transport feasibility check first echoed C-codec PING bytes unchanged over all five
negotiated browser/native DataChannels. Gameplay then reused that adapter. Channel IDs 0 and 4
(control/bootstrap) are reliable and ordered; 1, 2 and 3 are unordered with no retransmissions.
Existing application sequence numbers discard stale state/births. Current protocol v7 carries the arsenal in combat pages on the existing unreliable unsequenced channel; rebuild browser and native peers together.
`boat_net_mux` assigns distinct authority peer IDs to colliding ENet/WebRTC local peer IDs.

Build an optional native endpoint (C++ compiler and OpenSSL development files required;
C application sources remain C17). The first configure fetches pinned libdatachannel v0.24.1
and its pinned submodules. No new dependency enters default native or Wasm builds:

```sh
cmake -S . -B build-rtc -DCMAKE_BUILD_TYPE=Debug -DPROTEUS_WEBRTC=ON
cmake --build build-rtc --target boat_server boat_bot boat_rtc_echo --parallel
./build-rtc/boat_server --bind 127.0.0.1 --port 24680 --rtc-port 24682 --location river
```

Serve the browser build over local HTTP with the normal `web-serve` command. In its title,
choose MULTIPLAYER and enter `ws://127.0.0.1:24682`. A native client joins the same authority
at `127.0.0.1:24680`. Free boating, deathmatch and race use the normal server `--mode` flag.
`wake_velocity --serve` also accepts `--rtc-port` in an enabled native build; the ordinary
HOST A ROOM button still starts its usual ENet room without a browser listener.

For a LAN experiment, use the server machine's LAN address with `--bind`, enter that address
in the browser URL, and permit the signaling TCP port and WebRTC ICE UDP traffic. Only same-machine
connectivity has been verified here. The listener provides plain WebSocket signaling, exchanging
one complete SDP offer/answer after ICE gathering. Secure-page deployment needs a separately
operated WSS/TLS endpoint (for example, a reverse proxy to this signaling listener), a reachable
authority and a tested ICE route. `--ice-server URL` configures the native endpoint; the
`boatIceServers` array in [index.html](../web/index.html) configures browser STUN/TURN servers
using standard `RTCIceServer` objects. Winterfell's separate operator deployment configures
browser STUN and a private native TURN client; its [guide](MULTIPLAYER_HOSTING.md) owns the
internet-path evidence. There is no bundled public server, TURN credential
service, automatic port forwarding or internet-connectivity guarantee.

WebRTC encrypts its DataChannel transport, but this application has no player identity or
account authentication. The existing room token remains a shared room admission word. The
optional dependency is MPL-2.0, with its own dependency licenses; default release packaging
has not been extended to distribute this optional endpoint. Do not treat a local optional
build as a qualified relocatable release package.

The JS receive queue holds at most 256 events; disposable overflow drops updates and reliable
overflow disconnects. Both adapters bound application sends at 64 KiB per channel and refuse
messages over the protocol's 1100-byte limit. Native polling rejects excess pending accepts,
oversize signaling/messages and excessive receive backlog. Library/kernel allocations and
on-wire bandwidth still require measurement against the multiplayer plan's budgets.

Opt-in Playwright checks need Playwright plus Chrome (or `--executable` with a compatible
Chromium binary). The title check covers saves/reload, audio startup and repeated solo sessions;
the multiplayer check starts and cleans up its own native room and bot:

```sh
python3 tools/check_web_rtc_echo.py --url http://127.0.0.1:8000/ --build-dir build-rtc --output build-web/rtc-echo
python3 tools/check_web_title.py --url http://127.0.0.1:8000/ --output build-web/title-review
python3 tools/check_web_multiplayer.py --url http://127.0.0.1:8000/ --build-dir build-rtc --output build-web/net-free
python3 tools/check_web_multiplayer.py --url http://127.0.0.1:8000/ --build-dir build-rtc --four-players --output build-web/net-four
python3 tools/check_web_multiplayer.py --url http://127.0.0.1:8000/ --build-dir build-rtc --mode deathmatch --output build-web/net-deathmatch
python3 tools/check_web_multiplayer.py --url http://127.0.0.1:8000/ --build-dir build-rtc --mode race --output build-web/net-race
```

`boat_rtc_echo [PORT]` is the standalone 120-second protocol-byte echo gate;
`boat_rtc_echo --fixture FILE` writes its C-codec PING fixture. The Node contract test
`node tools/test_web_rtc_transport.cjs` checks reliability options, queue/backpressure bounds,
oversize messages and stale callbacks after reconnect; it also runs in native CTest when Node
is present and before the Wasm build in Pages CI. `boat_net_mux` tests cross-transport peer-ID collisions, routing across all delivery
classes, disconnect isolation and aggregate statistics without sockets.

### Verification, 2026-10-25

Apple Silicon macOS, Emscripten 6.0.12 MinSizeRel, Chrome headless/SwiftShader: shared title,
settings/solo panels, two solo sessions, Escape/return, persisted title settings after reload,
remembered fullscreen preference, launcher return and running audio context passed automation.
Title, setup, settings, game and Escape-menu captures were inspected. Audio-context state is
an automated startup check, not human listening acceptance.

The browser/native WebRTC echo passed all five channels with byte-identical C protocol fixtures.
Mixed ENet bot + browser gameplay passed in free, deathmatch and river race rooms, including
reset, leave and rejoin; the free-room run reported zero malformed, output or emission faults.
Browser gameplay/menu captures were inspected. This is local automated play, not human acceptance,
physical GPU performance, WAN loss/jitter, TURN, sustained four-player browser load or suspension qualification.
Native regression and final browser input results are recorded at handoff below.

## Mouse and scroll qualification, 2026-10-24

Historical control implementation in `boat-marine-hud` (propulsion superseded by pedals): captured relative mouse steering, held
wheel state and scroll thrust share the native C controllers. The browser adapter lives in
[mouse_controls.js](../web/mouse_controls.js), installed by `runtime.js` and linked before it
through `Web.cmake`. It requests capture directly from M/canvas-click gestures, waits for actual
`pointerlockchange`, and falls back to keyboard mode with a visible message if capture fails.
Browser Escape can bypass game key events, so external unlock is handled independently and
never automatically re-locks. Closing help shows a click-to-capture prompt; focus loss returns
to keyboard mode. The adapter follows the [Pointer Lock API](https://developer.mozilla.org/en-US/docs/Web/API/Pointer_Lock_API).

Movement events accumulate until the C frame consumes them; capture transitions discard
stale movement. Scroll events also accumulate, preserving trackpad fractions. Pixel/line/page
scroll modes normalize to 100 pixels / 3 lines / 1 page per thrust step, with up positive.
Only vertical scrolling changes thrust; scroll events over the focused game prevent page
scrolling. Pinch zoom does not change thrust.
This bypasses the pinned browser GLFW layer's minimum-one-step wheel quantization and raylib's
last-event-only locked movement callback. Native builds continue using raylib/GLFW directly.

Completed verification on Apple M1 Max / macOS: Emscripten **6.0.12** MinSizeRel build passed
without application warnings. The Node event-contract check passes for synchronous/Promise
capture APIs, missing/denied capture, delayed capture, short M presses, multiple movement
and scroll events, Escape/help/focus release, and scroll units. Browser integration results
and remaining platform limitations are also recorded in [the HUD owner](BOAT_HUD.md). Chrome
(headless/SwiftShader) and Firefox (headed) passed captured input, help/unlock/reacquisition and
fallback integration. Headed WebKit refused capture and passed the keyboard/scroll fallback;
shipping Safari and physical device behavior remain unverified. The new event contract also
runs in Pages CI before the Wasm build.

Run the display-free event contract with Node (registered as CTest `web_mouse_controls` when
Node is found), and optionally drive an actual served Wasm build using Playwright:

```sh
node tools/test_web_mouse_controls.cjs
python3 tools/check_web_controls.py --url http://127.0.0.1:8000/ --browser chromium --output build-web/controls-chromium
python3 tools/check_web_controls.py --url http://127.0.0.1:8000/ --browser firefox --headed --output build-web/controls-firefox
python3 tools/check_web_controls.py --url http://127.0.0.1:8000/ --browser webkit --headed --output build-web/controls-webkit
```

The optional script needs Playwright and its browser binaries. `--executable` selects an
installed browser. It checks real pointer-lock acquisition, C-bridge consumption, normalized
scrolling, help/reacquisition, browser-initiated unlock and keyboard fallback, saving captures
and `results.json`. If the engine refuses capture it records `web_controls_fallback=ok` and
checks keyboard/scroll fallback instead of claiming mouse support. Movement and scroll totals
use deterministic DOM events; this does not certify physical mouse/trackpad feel. A tested
WebKit build is not the shipping Safari application. Touch was unsupported at this historical checkpoint; basic mobile controls were added on 2026-10-28.

Next: physical-device and shipping Safari checks; native Windows/Linux qualification after
CI runs, plus connected multiplayer HUD/control review. No deployment was performed.

## Verification evidence

2026-10-19, Apple Silicon macOS:

- Emscripten **6.0.12**, CMake `MinSizeRel`, raylib 6.0, WebGL 2: configure and
  `cmake --build build-web --target web --parallel 4` passed. The existing pinned raylib
  checkout was reused through `FETCHCONTENT_SOURCE_DIR_RAYLIB`. Application compilation
  is warning-clean; raylib/miniaudio emits upstream deprecation/third-party warnings with
  this SDK. The generated JS/Wasm/data total **3,949,313 bytes** (about 1.65 MB using local
  gzip as an estimate, not a measured Pages transfer). The data archive is 445,132 bytes.
- Started the actual `web-serve` target and exercised the output in installed Chrome
  headless with SwiftShader, using temporary Playwright tooling outside the repository.
  River, Eriksberg, Basin, and time-trial scenes rendered; their captures were inspected.
  Optional 4x supersampling, keyboard throttle, help, best-replay playback, fullscreen,
  and resize to a 640x400 displayed canvas worked. No JavaScript, asset-load, or shader
  errors occurred. Chrome reported the upstream ScriptProcessorNode deprecation and
  software-renderer readback performance warnings.
- The Play gesture produced a running AudioContext; audio instrumentation observed
  nonzero output (68 of 89 sampled callbacks in the final targeted run). This confirms
  generated audio, not subjective sound quality or a full musical listening review.
- Before Play, checked all eight shaders, three textures, and the XM in the virtual
  filesystem. Tested IDBFS write/close/rename persistence across reload with a real native
  recording fixture; the C game then displayed its best time/ghost and played its replay.
  This verifies storage integration, not a human-completed browser race. Denying IndexedDB
  still enabled Play and displayed the storage warning.
- Native Debug configure/build passed; **70/70 CTests passed**, including **18 GPU tests**
  with `PROTEUS_GPU_TESTS=ON`. The initial sandboxed run stalled at its first GPU case;
  it was stopped and the full suite rerun with desktop graphics access and a 120-second
  per-test timeout. A native GPU river/waves scripted demo (`--demo --frames 120`) also
  completed without logged errors. This is a startup/playback smoke check, not human
  audio or handling acceptance.
- Initially parsed workflow YAML and checked the then-current main-branch trigger (superseded
  by the tag trigger below), job dependency, deployment
  permissions, and official Pages action usage. Verified packaged content hashes and URL
  mappings, local documentation links, C formatting, JS syntax, and `git diff --check`.
  Existing native/Release workflow files have no changes.

Local evidence is in ignored `build-web/browser-*.png`, `build-web/browser-validation.json`,
`build-web/build.log`, and `build/web-native-ctest-desktop.log`. The browser automation and
SDK installation were temporary verification tools, not new project dependencies.

Remaining limits: the GitHub workflow/deployment itself has not run; Linux-hosted Emscripten
CI, Safari, Firefox, physical GPU browser performance, long sessions, browser audio latency,
private-mode/quota behavior beyond a denied-storage check, and human gameplay/listening
acceptance were unverified at this checkpoint. Touch controls were absent then; basic mobile
controls were added on 2026-10-28. Recording import/export UI remains unavailable.

### Tag deployment follow-up — 2026-10-19

Changed the push trigger to `v*` tags and allowed those refs in the deployment job;
retained explicit manual deployment from `main`. Updated setup instructions to cover tag
environment rules and the requirement to commit all web build inputs before tagging.
User feedback: local browser startup works. Actionlint 1.7.12 passed for the updated workflow
(ShellCheck unavailable); embedded build commands passed `bash -n`. Parsed the YAML and
checked tag/manual deployment conditions for stable tags, prerelease tags, manual `main`,
manual tags, ordinary main pushes, and other branches. Deployment permissions, job dependency,
changed local documentation links, and `git diff --check` passed. Native Debug configure/build
and all 70 CTests passed on Apple Silicon macOS, including 18 GPU cases with desktop access
and a 120-second per-test timeout (`build/web-tags-ctest.log`). No new audiovisual review was
needed for this workflow/documentation change. The first remote tag build and deployment
remain unverified; no tag was created or pushed during this change.

## References

- [raylib web build conventions](https://github.com/raysan5/raylib/wiki/Working-for-Web-%28HTML5%29)
- [Emscripten Asyncify](https://emscripten.org/docs/porting/asyncify.html)
- [Emscripten filesystem / IDBFS](https://emscripten.org/docs/api_reference/Filesystem-API.html)
- [GitHub Pages custom workflows](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)

## Launch-page refresh — 2026-10-20 (superseded design)

Implemented: responsive editorial layout, bundled river preview, simplified location/settings,
optional fullscreen at launch, and game-only presentation after the audio start gesture.
Removed the duplicate page controls guide and visible raylib link. Retained the distributed
raylib license and OpenStreetMap attribution. Updated in-game browser help to say reload
rather than reference the removed Restart button.

Verification on 2026-10-20, Apple Silicon macOS: native Debug configure/build and
Emscripten `web` packaging passed. Chrome/SwiftShader automation passed desktop and 390px
layout, launch/focus, integer resizing, in-game help, course fullscreen/exit, reload, and
abort recovery, with no JavaScript errors. Desktop Chrome smoke launched the river,
applied throttle, and opened/dismissed in-game help. Desktop/narrow launch, game-only, and
help captures were inspected (`build-web/launcher-*.png`). Local documentation links and
`git diff --check` passed. Listening and Safari/Firefox were not checked.
All 70 native CTests passed, including 18 GPU checks, with desktop graphics access and a
120-second per-test timeout (`ctest --test-dir build --output-on-failure --timeout 120 -j 4`).
The initial sandboxed run was stopped at its first GPU check; the full desktop rerun passed.
No user acceptance of this redesign yet.
Next: deploy with the normal version-tag/manual workflow and review the new launch page.
Historical limitations (superseded by shared-title and mobile work): keyboard required;
reload was the way back to setup; browser-specific fullscreen
restrictions may fall back to ordinary full-window play.

## Compact technical launcher — 2026-10-20

Implemented after user feedback: retained the screenshot and colors, removed promotional
copy/captions, and replaced the editorial layout with a monospace launcher, prominent PLAY
button, and collapsed Settings toggle. Smooth edges and fullscreen now default to on.
Location, waves, smooth edges, and fullscreen remain configurable before launch. The
OpenStreetMap notice remains visible; game-only presentation is unchanged.

Verification on 2026-10-20, Apple Silicon macOS: native Debug configure/build and
Emscripten web packaging passed. Desktop Chrome/SwiftShader checks passed desktop and
390px layouts, collapsed/open settings, checked AA/fullscreen defaults, fullscreen
launch/focus, throttle and in-game help, and a configured windowed course launch; no
JavaScript errors. Desktop and narrow-layout captures were inspected
(`build-web/compact-*.png`). Documentation links and `git diff --check` passed.
All 70 native CTests passed, including 18 GPU checks, with desktop graphics access
and a 120-second timeout (`ctest --test-dir build --output-on-failure --timeout 120 -j 4`).
Listening and Safari/Firefox were not checked.
Next: review the compact launcher in the deployed build. No user acceptance of this revision yet. Keyboard input remains
required; browsers may refuse fullscreen, in which case play continues in the window.

## Canvas focus outline — 2026-10-20

Removed the game canvas focus outline after the user reported a yellow border during play.
Launcher controls retain visible keyboard focus indicators. The user accepted the compact
launcher before this correction. Next: review the border correction in the browser.
Verification on 2026-10-20, Apple Silicon macOS: native Debug configure/build and web
packaging passed. Chrome keyboard-focus check confirmed no canvas outline and retained
launcher focus indicators; `build-web/canvas-no-outline.png` was inspected. All 70 native
CTests passed, including 18 GPU checks, with desktop access and a 120-second per-test
timeout. Safari/Firefox and listening remain unchecked.

## Current arcade build local smoke — 2026-10-22

Revision `8e69b5b`, development M1 Max/macOS: the Emscripten build/package passes.
Served `build-web/site` at `http://127.0.0.1:8001/` and launched the actual JS/Wasm/data
in installed Chrome headless with SwiftShader using the existing temporary Playwright
tooling. River and basin startup/rendering pass; inspected river, help and reset captures.
Sent throttle input, opened/dismissed help and reset the basin. No JavaScript, shader or
asset-load errors were logged. AudioContext reaches running state; listening was not tested.
Expected ScriptProcessorNode deprecation and software-renderer readback warnings remain.

Artifacts: `build-web/arcade-browser-*.png`, `build-web/arcade-browser-smoke.json`;
log: `/tmp/proteus-arcade-browser.log`; temporary script:
`/tmp/proteus-arcade-browser-smoke.py`. Next: hands-on browser driving/listening and
hardware GPU performance; Safari/Firefox, prolonged play and arcade landing/capsize
coverage in the browser remain unverified. This smoke is not a performance benchmark.

## Landing build version — 2026-10-22

Implemented a small version footer resolved by `tools/package_web.py` at packaging time,
with HTML escaping and an explicit `--version` override. Exact release checkouts show the
tag; development checkouts show Git's description/hash and tracked dirty state. No Git
checkout displays `unknown` unless overridden. The current workflow's shallow main
checkout can show a hash instead of a nearest-tag description. README links the live site,
verified to return HTTP 200. Emscripten packaging, exact-tag/dirty/fallback/escaped-override
checks and Chrome desktop/390px footer visibility/no-horizontal-overflow checks pass;
`build-web/version-{desktop,narrow}.png` were inspected. Native Debug configure/build and all 83 active CTests (20 GPU) pass in 119.79 s;
fifteen retired field cases stay disabled. Logs: `/tmp/proteus-web-version-*.log`.
Changed documentation link targets and `git diff --check` pass.
Next: confirm the version on the next deployed build; deployment is not performed here.

## Final regression evidence, 2026-10-25

In `web-parity` on Apple Silicon macOS: native Debug configure/build and all **93 active
CTests passed** in 72.67 s (13 retired cases disabled, GPU tests off). This includes the new
multiplexer and browser transport contract cases and the existing socket-based native room
checks. The first sandboxed suite could not bind localhost sockets; the complete rerun with
local socket access passed. The application sources compiled without warnings; the optional
third-party build retained an upstream stb_vorbis warning.

The final Emscripten 6.0.12 MinSizeRel `web` build, Chrome title/save/reload check and browser
input integration passed. Native title capture inspected; native GPU river demo (`--demo
--frames 120`) completed without logged errors. These are agent-run checks, not human audio
review or user acceptance. The repeatable `check_web_rtc_echo.py` also passed all channels and
32 messages in order on each reliable channel. Changed documentation links and workflow YAML
syntax were checked; no CI run or deployment was performed.

The final local capacity check passed with **two browser clients plus two native bots**,
rejected a fifth native client as full, and passed browser reset/leave/rejoin afterward. The
authority reported zero malformed, output and emission faults; an abruptly closed browser
was removed by the existing timeout path. Its four-player capture was inspected. Attempts
to run three browser canvases together under headless SwiftShader first lost rapid scripted
URL edits, then stalled a third-canvas Play click; that software-renderer configuration is
not qualified. No result is claimed for three/four browser clients or physical multi-browser
performance. This does not replace WAN impairment, suspension or long-soak qualification.
