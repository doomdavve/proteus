# Browser boating build

## Current status

Implemented in the isolated `web-parity` worktree, 2026-10-25: the browser enters the shared
Wake Velocity title after the page's PLAY gesture. SINGLE PLAYER opens PLACE (River, Basin,
Basin trial), WAVES, SMOOTHING and START. SETTINGS remembers name, single-player boat livery,
steering and soundtrack. Escape opens the shared in-game menu; BACK TO TITLE returns to setup
without reloading. BACK TO LAUNCHER flushes saves and reloads the page. The small HTML launcher
retains fullscreen selection, remembered separately in browser storage. Gameplay/title settings
use the native `title.txt` format through the existing IndexedDB mount; older saves get the new
river/waves/smoothing defaults. IndexedDB denial still permits play without durable settings.

Browser multiplayer now uses a JavaScript WebRTC transport and the existing C protocol,
client prediction, wake reconciliation and remote rendering. The optional native endpoint accepts
browser players alongside ENet clients in the same four-player authority. MULTIPLAYER offers a
room signaling URL, CONNECT and BACK; browser hosting and UDP LAN discovery are unavailable.
See [WebRTC setup and limits](#webrtc-rooms). Normal native/release builds remain ENet-only.
The GitHub Pages build includes the browser client; Pages does not provide a game server.
No deployment or public service was created by this change.

The existing Emscripten/WebGL 2 build, click-to-start audio, IndexedDB bests, content-hashed
packaging and `v*` Pages deployment workflow remain in use. Both music tracks and the shared
title backdrop are bundled. One browser audio device starts during PLAY and remains alive
across title/session transitions; Off disables the soundtrack, retaining engine/water sound.

Next: qualify WebRTC on Firefox/Safari and physical devices, test real loss/latency and tab
suspension, measure memory/runtime budgets, and establish secure public signaling/ICE/TURN
hosting before internet-play acceptance. User play/listening acceptance remains outstanding.
The earlier launcher and browser qualification records below are dated history.

## Build and deployment

The [README](../README.md#browser-build-and-github-pages) owns the SDK installation,
build/serve commands, Pages setup, and browser controls. The toolchain is pinned in
[web/emsdk-version](../web/emsdk-version); the workflow also pins the official SDK checkout.
Keep SDK updates coordinated with browser smoke checks.

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
- **Window:** the browser keeps a fixed 960x600 drawing buffer, containing the 320x200 scene,
  and scales it to whole scene-pixel multiples. This avoids raylib's web resize handler,
  which otherwise takes over canvas sizing. The launcher hides before the game starts;
  the canvas then fits the full viewport. Fullscreen is selected before launch. Escape opens
  the in-game menu (and may leave fullscreen); title and sessions share the window.
- **Assets:** preload both game XM tracks, the title backdrop, textures and used shaders into `/assets`.
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

## WebRTC rooms

The transport feasibility check first echoed C-codec PING bytes unchanged over all five
negotiated browser/native DataChannels. Gameplay then reused that adapter. Channel IDs 0 and 4
(control/bootstrap) are reliable and ordered; 1, 2 and 3 are unordered with no retransmissions.
Existing application sequence numbers discard stale state/births. Protocol v5 bytes are unchanged.
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
using standard `RTCIceServer` objects. There is no bundled public server, TURN credential
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

Current control implementation in `boat-marine-hud`: captured relative mouse steering, held
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
WebKit build is not the shipping Safari application. Touch remains unsupported.

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
acceptance are unverified. No touch controls or recording import/export UI is provided.

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
Limitations: keyboard required; reload is the way back to setup; browser-specific fullscreen
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
