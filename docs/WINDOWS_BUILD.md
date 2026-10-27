# Windows Wake Velocity builds

Multiplayer M3 (2026-10-23): `wake_velocity` now links the pinned ENet library (winsock,
`ws2_32`/`winmm`) for the diagnostic `--connect` mode, and the package step copies
`enet-LICENSE.txt`. MSVC first compiled ENet and `src/boat_net_enet.c` on 2026-10-24 (below).
[M6](MULTIPLAYER_PLAN.md) still owns MSVC
qualification and firewall/Host documentation. The existing Windows verification limits below
remain unresolved.

Packaged LAN server (2026-10-24): the workflow also builds `boat_server`, `boat_bot` and
`validate_boat_net`, runs `boat_server_scripted`, `boat_net` and the ENet `boat_net_loopback`
test (since 2026-10-26: the `boat_net.*` and `boat_net_enet.*` cases and the
`boat_net_process` smoke, which replace `boat_net` and `boat_net_loopback`), and packages
`boat_server.exe` with `host.bat` (a room on `--bind 0.0.0.0`). The relocated check runs
`boat_server.exe --scripted --ticks 240`.

Host from the game (2026-10-25): the ZIP no longer carries `boat_server.exe`. `wake_velocity.exe
--serve` is the room (`host.bat` runs it with `--bind 0.0.0.0`), and the title's HOST A ROOM
starts it as a hidden child process (`CreateProcess`, `CREATE_NO_WINDOW`) that closes when the
game's pipe ends. The relocated check runs `wake_velocity.exe --serve --scripted --ticks 240`, and
CTest adds `wake_velocity_serve_scripted` and `boat_host`. The Windows process and pipe code has not
been compiled yet; the next CI run is its first check. See
[host from the game](MULTIPLAYER_PLAN.md#host-from-the-game--2026-10-25).

2026-10-24: the first MSVC build of the M3+ code,
[run 40082682785](https://github.com/doomdavve/proteus/actions/runs/40082682785/job/120839290770),
failed compiling `src/boat_net_enet.c`: ENet's headers include `windows.h` after raylib, so
`Rectangle`, `CloseWindow` and `ShowCursor` were redeclared (C2365/C2371), the same clash
`profiler.c` hit on 2026-10-20. `boat_net_enet.c` now defines `WIN32_LEAN_AND_MEAN`, `NOGDI` and
`NOUSER` before `enet/enet.h`; its clock and sleep use only kernel32. MSVC is unavailable here,
so the fix was first compiled by CI: [run 40083439355](https://github.com/doomdavve/proteus/actions/runs/40083439355)
at `29d1afb` passed every step: the build of `wake_velocity`, `boat_server`, `boat_bot` and the
validators, the selected CPU and multiplayer tests (step result only; per-test log lines were not
read), packaging and the relocated `wake_velocity`/`boat_server` checks. The HUD/control merge below
has not yet been through Windows CI. Human Windows play and `host.bat` remain unverified.

The Windows Wake Velocity GitHub Actions workflow builds a Release x64 executable
on windows-2022 with Visual Studio 2022. It runs on pushes and can also be
started manually from the Actions tab after the workflow exists on the default branch.
No Windows development tools are needed on your Mac or the test machine.

## HUD/control qualification, 2026-10-24

Merged into `main` on 2026-10-24 (after `29d1afb`), the `boat-marine-hud` work adds relative captured mouse steering (M/Esc releases),
scroll-wheel thrust and shared pure control helpers. Native input uses the same pinned raylib
GLFW backend on macOS, Windows and Linux; browser-only code is excluded from MSVC builds.
The existing Windows `boat_handling` check exercises all detent/wheel transitions, held targets,
relative movement, scroll accumulation and lock/overscroll reversal. This revision has **not**
yet been compiled or played on Windows; the next push/workflow run must qualify it before
Windows support is called verified. Manual pointer capture, help/focus/Esc, mouse/trackpad feel
and connected play remain required. See [the HUD owner](BOAT_HUD.md).

## Download and test

For versioned downloads, use the ZIP under **Releases** once a version is published;
see [automated releases](RELEASES.md). Extract `wake_velocity-<version>-windows-x64.zip` completely
on Windows and double-click `launch.bat`. Here `<version>` is the release tag
(for example `v0.1.0`); branch artifacts use
`dev-<full-commit-sha>`. For an ordinary branch build:

1. Commit and push the source and .github/workflows/windows-boat.yml to GitHub.
2. Open the repository's **Actions** tab and select **Windows Wake Velocity**.
3. Open the successful run and download `wake_velocity-<version>-windows-x64` under **Artifacts**.
4. Transfer the ZIP to Windows and extract it completely before running it.
5. Double-click launch.bat to open the Wake Velocity title (`wake_velocity.exe` without options).

Artifacts expire after 30 days. `build.txt` records the version and source commit. The package
includes shader/texture/title assets, bitmap menu fonts and their attribution, background
music, and map attribution; geometry is
compiled into the executable. Asset loading uses the executable directory, so the folder can be moved.
raylib and the MSVC runtime are statically linked for this workflow.

The workflow runs the boat handling validator and CPU screenshot checks, including
a relocated executable launched from another working directory. These do not exercise
GPU shader loading. On the Windows test machine, check startup, shadows, reflections,
steering, thrust, camera controls, reset, and exit. Try --gpu without --gpu-aa if
performance is poor. OpenGL 3.3 support is required for GPU mode.

The executable also accepts `--location river` for the bridge expansion, `--waves`
for gentle waves, and `--course` for the basin time trial. Renderer defaults are the
same as on macOS: use `--renderer cpu` explicitly for the software reference.
See the [main controls and replay guide](../README.md#boating-sketch) for the full interface.
Workflow build/CPU checks do not establish a successful human Windows playthrough.

## Connect an existing checkout to GitHub

Create an empty GitHub repository without a generated README, license, or gitignore.
From the local repository root, substituting your account and repository name:

```sh
git remote add origin https://github.com/YOUR-USERNAME/proteus.git
git remote -v
```

After committing the files you want uploaded, push the current branch:

```sh
git push -u origin HEAD
```

Use your configured GitHub credentials for HTTPS, or an SSH remote if you have
already set up a GitHub SSH key. Pushes upload committed files only.

See [GitHub's artifact download instructions](https://docs.github.com/en/actions/managing-workflow-runs/downloading-workflow-artifacts).

## Current status and verification

Release naming, 2026-10-24: The release naming now uses `wake_velocity.exe`, matching `launch.bat`, and
versioned package/artifact names. A fixture ZIP layout/checksum and workflow lint
passed locally; real PowerShell packaging and Windows execution still require CI.
[Releases](RELEASES.md#status-and-verification) owns the complete evidence and limits.
Next: verify the updated workflow downloads and perform user playback.


Static package review, 2026-10-24: the Windows workflow copied shaders, textures, music, and
map attribution but omitted `assets/title/river-backdrop.png`. Since `launch.bat` starts the
no-argument title screen, this matched the packaged macOS startup failure. The workflow now
copies `assets/title/` and checks that the backdrop is in the relocated package. This change
has not yet run on Windows; CI and interactive Windows startup remain to be verified.

2026-10-20: the [Windows boat study run](https://github.com/doomdavve/proteus/actions/runs/39309664424/job/118255664525)
failed while compiling the newly added Windows monotonic-clock code in `profiler.c`. Including
`windows.h` after raylib exposed the Windows SDK's `Rectangle`, `CloseWindow`, `ShowCursor`, and
`DrawText` declarations. Both timing sources now define `WIN32_LEAN_AND_MEAN`, `NOGDI`, and
`NOUSER` before including `windows.h`, following the existing `boat_replay.c` pattern. This
preserves `QueryPerformanceCounter` while excluding the conflicting UI/GDI declarations.
MSVC is not installed in this macOS environment, so the fix is not locally compiled; rerun the
Windows workflow after pushing and confirm the build, CPU checks, package, and relocation step.

2026-10-19: [run 39273971121](https://github.com/doomdavve/proteus/actions/runs/39273971121/job/118133350281)
failed the basin stress test's `abs(yaw_velocity) < 4` assertion; both CPU capture tests
passed. Local heading perturbations reproduced wall-impact peaks of 5.07 and 5.66 rad/s.
The old limit treated impact-induced rotation as a free-steering turn. The validator now
bounds combined linear/rotational kinetic energy by the old combined speed/yaw budget,
retains finite-state, speed, and basin bounds, and runs three nearby starting headings.
A separate unpowered impact check verifies energy decreases even when yaw exceeds 4 rad/s.
Production physics and the accepted handling baseline are unchanged.

Local macOS checks passed: all 69 Debug tests across split runs and all three
Windows-selected Release CPU tests. Evidence and remaining checks are recorded in the
[physics CI follow-up](BOAT_PHYSICS_PLAN.md#windows-ci-impact-bound-follow-up--2026-10-19).
Next: verify the Windows workflow after this correction is pushed, then perform the outstanding
human Windows GPU/audio/control review. Local checks do not establish an MSVC pass.

2026-10-18: the Windows Release run at commit `73bf1ba` failed the mapped reversing
assertion while both CPU capture tests passed. A local Release probe reproduced the
assertion with a starting-heading perturbation of -0.00001 radians: the boat travelled
10.17 units backward, but ended only 2.27 units from where reverse thrust was selected.
The validator now measures actual backward displacement along the hull each tick,
retains the three-unit minimum and shoreline checks, and covers both quay directions
at three nearby starting headings. This changes validation, not boat handling.

The package now includes `a-little-boat.xm`; its relocation check explicitly selects
the CPU renderer. On Apple Silicon/macOS 26, Debug configure/build and all 69 CTests
passed with desktop access (18 GPU checks); the Release handling test also passed.
Workflow YAML parsing, changed documentation links, and `git diff --check` passed.
The corrected [Windows run at `3bffdb7`](https://github.com/doomdavve/proteus/actions/runs/39164837209)
passed the build, all three selected tests, packaging, relocation, and artifact upload.
Next: download and check GPU startup, music, and controls on Windows. Human Windows
playback remains unverified. Version-tag builds are also reused by the
[automated release workflow](RELEASES.md).
