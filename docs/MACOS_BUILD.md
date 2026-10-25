# macOS Wake Velocity builds

Multiplayer M3 (2026-10-23): native builds fetch the pinned ENet library, and `wake_velocity`
links it for the diagnostic `--connect` mode, so the app bundle now carries
`enet-LICENSE.txt`. Since 2026-10-24 the bundle also carries the room authority at
`Contents/MacOS/boat_server`, ad-hoc signed before the app, and CI runs it relocated with
`--scripted`. Since 2026-10-25 that copy is gone: the game serves rooms itself
(`Contents/MacOS/wake_velocity --serve`, and the title's HOST A ROOM), so the bundle holds one
executable and CI runs `wake_velocity --serve --scripted --ticks 240`; see
[hosting from the game](../README.md#hosting-from-the-game). The 2026-10-24 notes follow. There is no
`boat_bot` and no Host control. Since 2026-10-24 the title's MULTIPLAYER entry joins a room by address (not yet
re-run through the packaged app or CI). The plan's M6 owns server packaging, firewall guidance and a child-process Host
option. The package script change was run locally on a Debug build; the CI workflow has not
run it yet: [run 40083439307](https://github.com/doomdavve/proteus/actions/runs/40083439307)
(2026-10-24, `29d1afb`) stopped at three CI-only test failures before packaging. Their causes
and fixes are in the [multiplayer handoff](MULTIPLAYER_PLAN.md#packaged-lan-server-handoff--2026-10-24).

The **macOS Wake Velocity** GitHub Actions workflow builds a Release Apple Silicon
app on `macos-15`, targeting macOS 13 or newer. It runs on pushes and can be
started manually after the workflow is on the default branch. Intel Macs are
not included. No Apple Developer membership or signing secrets are required.

## Download and open

For versioned downloads, get `wake_velocity-<version>-macos-arm64.zip` from **Releases**
once a version is published, extract it, and continue with step 4 below. See
[automated releases](RELEASES.md). Here `<version>` is the release tag (for example
`v0.1.0`); branch artifacts use
`dev-<full-commit-sha>`. For an ordinary branch build:

1. Push the changes, then open **Actions → macOS Wake Velocity** in GitHub.
2. Open a successful run and download `wake_velocity-<version>-macos-arm64` under **Artifacts**.
3. Extract the artifact, then extract the enclosed `wake_velocity-<version>-macos-arm64.zip`.
4. Move **Wake Velocity.app** into Applications, or another folder of your choice.
5. Open the app. If macOS blocks it, open **System Settings → Privacy & Security**,
   select **Open Anyway** for this app, and confirm the launch.

See [Apple's instructions](https://support.apple.com/en-ie/102445) for the security
override. Managed Macs may restrict this option. The app is ad-hoc signed: its
bundle integrity is checked, but there is no verified developer identity or Apple
notarization. The workflow does not disable Gatekeeper. A SHA-256 file accompanies
the ZIP; it checks archive integrity, not publisher identity. Artifacts expire after
30 days. `Contents/Resources/build.txt` records the source commit and whether the
checkout had uncommitted changes when packaged.

Double-clicking the app opens the Wake Velocity title (2026-10-24), as does `wake_velocity`
without options. Its SINGLE PLAYER starts the preferred river setup: current, crossing
wake, gentle waves, GPU antialiasing, camera distance 12, and camera height 6, in the
chosen BOAT livery. MULTIPLAYER hosts a room (HOST A ROOM), lists rooms found on the local
network and joins one, or a typed HOST:PORT, with the chosen NAME. The single-player setup is equivalent to:

```sh
./build/wake_velocity --current --crossing-wake --location river --waves --gpu-aa --camera-distance 12 --camera-height 6
```

See the [controls and replay guide](../README.md#boating-sketch). Supplying any
explicit arguments to the bundle executable uses the usual CLI defaults (basin,
without current/crossing wake/waves/antialiasing), then applies those arguments.
For example:

```sh
"/Applications/Wake Velocity.app/Contents/MacOS/wake_velocity" --location river --gpu-aa
```

Music, shaders, textures, the title river backdrop, map attribution, and dependency licenses
are inside the bundle. Personal bests remain in `~/Library/Application Support/Proteus/`.

## Build locally

From the repository root on an Apple Silicon Mac:

```sh
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DBUILD_SHARED_LIBS=OFF -DPROTEUS_GPU_TESTS=OFF
cmake --build build-mac --parallel 3
ctest --test-dir build-mac --output-on-failure
python3 tools/macos/package.py --build-dir build-mac --output-dir build-mac/package
```

Use a fresh output directory when packaging again; the script refuses to overwrite
an existing app. It checks architecture and system-only dynamic dependencies,
creates the bundle, ad-hoc signs it, verifies the signature, and creates the ZIP
with `ditto`. The plist records the executable's minimum macOS version.
`--version v0.1.0` sets the ZIP name, build record and bundle version; the default
local version is `dev`. An optional `--raylib-source PATH` supports a custom CMake
FetchContent source location.

The workflow runs the full display-free CTest suite and checks the extracted app's
signature and a CPU capture from another working directory. GPU rendering and
audio playback require a desktop check; CI does not establish those or Gatekeeper
behavior after a browser download.

## Implementation status and verification

Release naming, 2026-10-24: The renamed/versioned Mac package was built locally from Debug, signed, extracted
and checked outside the checkout. CPU/title captures, a short GPU demo, no-argument
title startup and the bundled server passed; captures were inspected.
[Releases](RELEASES.md#status-and-verification) owns the complete evidence and limits.
Next: verify the updated workflow downloads and perform user playback.


2026-10-18, Apple Silicon/macOS 26: workflow, relocatable boating assets, preferred
river launch defaults, bundle packager, ad-hoc signature, ZIP, and checksum implemented.
Debug configure/build passed; all 69 CTests passed with desktop access, including
18 GPU checks. The first sandboxed suite was stopped at its first GPU test after
50 passing checks; desktop access resolved the stall. Release arm64 build targeting
macOS 13 passed (raylib emitted an existing third-party `stb_vorbis` warning).
Local Release configuration reused the fetched raylib source via
`FETCHCONTENT_SOURCE_DIR_RAYLIB`; packaging used `--raylib-source` accordingly.

ZIP extraction, strict code-signature verification, checksum verification, relocated
CPU capture, GPU/antialiasing desktop smoke, and Launch Services startup passed.
The final no-argument bundled river launch ran for five seconds before deliberate
termination, and a GPU capture of the preferred invocation was inspected.
Workflow YAML parsing, Python compilation, changed local documentation links, and
`git diff --check` passed. No subjective audio listening or interactive control
review was performed for this change; macOS 13 execution remains untested.

The [first GitHub run](https://github.com/doomdavve/proteus/actions/runs/39163710234)
at commit `73bf1ba` passed all build, test, packaging, signature, relocation, and
upload steps; the Apple Silicon artifact was available when checked on 2026-10-18.

Next: download through a browser and verify the Open Anyway path and interactive
playback. Browser-download Gatekeeper behavior, user acceptance, macOS 13 execution,
Intel support, and notarization remain unverified or out of scope.

Startup asset fix, 2026-10-24: the packaged app omitted `assets/title/`, so its no-argument
launch exited when `wake_velocity` could not load `title/river-backdrop.png`. The macOS packager
now copies the title assets and its workflow checks that the backdrop is present in the
relocated bundle. This fix has not yet been run through macOS packaging CI or an interactive
launch. Next: verify the workflow artifact and launch it from Finder.
