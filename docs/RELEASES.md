# Automated app releases

The [multiplayer plan](MULTIPLAYER_PLAN.md) proposes native multiplayer before browser support;
release packages link ENet for `wake_velocity --connect` and the title's MULTIPLAYER entry
(2026-10-23/24) and include `enet-LICENSE.txt`. From 2026-10-24 both also carried `boat_server`
for LAN rooms; since 2026-10-25 the game serves rooms itself (`wake_velocity --serve`, the
title's HOST A ROOM, Windows `host.bat`), so packages carry one executable. M6 will add protocol/content
identity, native cross-platform checks and a separate Linux headless-server distribution to
release documentation. A future native multiplayer release need not imply browser multiplayer parity.

Every release includes upgrading the [Winterfell room](MULTIPLAYER_HOSTING.md) and its
matching hosted browser client. The `wake_velocity` role in `../davve-srv/ansible` owns
this required step: pin the published version, commit/push, then run the ordinary Ansible
wrapper. Native and browser builds run on Winterfell with the optional WebRTC transport.
There is still no downloadable Linux server distribution.

The earlier `v0.0.17-server-directory` UI backport is historical. v0.0.18 includes the directory,
STUN defaults and protocol-v6 weapons; its release/Pages publication and server cutover have
completed. Tag publication does not automatically upgrade the room. Complete each release with the
[routine deployment workflow](MULTIPLAYER_HOSTING.md#routine-deployments-davve-srv). The public
directory reports the deployed server's compatibility to Pages and Winterfell clients.

The **Release Wake Velocity** workflow builds and tests the Apple Silicon Mac app
and Windows x64 package for the same version tag, then attaches both ZIPs and their
SHA-256 files to a GitHub Release. Downloads from Releases do not require navigating
Actions artifacts and are not subject to their 30-day retention policy.

## Publish a version

### Release command

From a clean, pushed worktree at the reviewed `origin/main` commit, after the Debug build
and full CTest preset pass, run:

```sh
python3 tools/cut_release.py v0.0.21 \
  --hosting-repo /path/to/clean-pushed-davve-srv-worktree \
  --browser-python /path/to/playwright-venv/bin/python
```

The hosting worktree needs its normal controller/vault setup. For a temporary worktree,
set `ANSIBLE_VAULT_PASSWORD_FILE` to the existing controller's vault password file; do not copy
or commit the password. Install Playwright in the chosen Python environment and have Google
Chrome available at the browser check's default macOS path. Build `build/boat_bot` first.

The command creates and pushes the immutable tag, waits for native publication and Pages,
downloads both ZIPs, checks SHA-256, ZIP integrity and embedded version/commit, then commits
and pushes the hosting version pin and runs the ordinary Winterfell role. It checks service
health/version, both browser versions, named browser joins on both sites, directory protocol/
physics and a matching native join. Evidence is saved under `build/release/verification.json`
with browser logs/captures. A failed step stops the command; rerun from the same commit/tag
after resolving the failure. Existing tags pointing elsewhere are rejected. This command
does not merge PRs or replace human package/input/audio review. Update the release and hosting
guides and the hosting repository's evidence after completion.

Preparation, 2026-10-28: v0.0.21 collects the merged mobile browser controls and adaptive
viewport work since v0.0.20. Release orchestration and offline archive/pin guards implemented;
Debug build and all 233 enabled CPU CTests passed (13 retired cases disabled, GPU tests off).
The CPU river capture was inspected. Publication and hosted cutover are pending at this
preparation commit. The macOS CI build directory is now ignored so generated build output
does not falsely mark packaged source as modified.

### Tag or UI publication

After the release workflow changes are committed and pushed, create and push a
version tag on the commit you want to ship. For example, using a new version:

```sh
git tag -a v0.1.0 -m "Wake Velocity v0.1.0"
git push origin v0.1.0
```

The same tag push also starts the separate **Web boating / GitHub Pages** workflow,
which builds the browser game and updates the Pages site. Complete the one-time
[Pages setup](../README.md#browser-build-and-github-pages), including allowing `v*` tags
in the deployment environment, before the first web release. The tagged commit must
contain the web workflow and build inputs. Web deployment and native ZIP publication
run independently; either can succeed if the other fails. All `v*` tags, including
prereleases, update the same Pages site. The [web build guide](WEB_BUILD.md) owns its
deployment status and verification.

Alternatively, publish a release in GitHub's **Releases → Draft a new release** UI
using a tag beginning with `v`. Publishing it triggers the same builds and automatic
attachments. Existing tags/releases created before this workflow was present are
not processed retroactively; create the next version from a commit containing it.
Publishing a Release for an existing tag alone does not trigger Pages; use a new tag
push or a manual run of the web workflow for that deployment.

The version-tag path creates a draft after both platform jobs succeed, uploads all
four assets, then publishes it. A tag containing a hyphen, such as `v0.1.0-rc.1`,
creates a prerelease. Titles and notes are generated by GitHub for new releases;
existing release titles, notes, and prerelease status are preserved. If you publish
through the UI, that release is visible while its assets build.

If a build fails, fix it and release a new version. If an upload or publication
fails, rerun the failed jobs from the release workflow's Actions page. Reruns reuse
the existing release and replace assets with the same names. A new release remains
in draft if its uploads fail. Do not move an already published version tag.

## Required Winterfell cutover

After native publication and Pages deployment succeed:

1. In `../davve-srv`, set `wake_velocity_version` in
   `ansible/host_vars/winterfell/main.yml` to the newly published tag, then commit and push.
2. From that repository run `scripts/ansible-run.sh --limit winterfell --tags wake_velocity`.
   This builds the authority and matching browser client before switching the room and nginx;
   players briefly disconnect during the room restart.
3. Verify the running release, active room and relay, hosted browser version, public directory
   protocol/physics, and matching native/browser joins. Record results and limits in this
   document, the [hosting guide](MULTIPLAYER_HOSTING.md) and `davve-srv/docs/wake-velocity.md`.

A release remains incomplete until the hosted cutover is verified or its blocker is reported.
Use the [routine role](MULTIPLAYER_HOSTING.md#routine-deployments-davve-srv), not the legacy
standalone playbook. Keep the server and hosted browser client on the same release.

## Downloads and trust

| Asset | Contents |
| --- | --- |
| `wake_velocity-<version>-macos-arm64.zip` | Ad-hoc signed `Wake Velocity.app`; Apple Silicon, macOS 13+ |
| `wake_velocity-<version>-windows-x64.zip` | Portable folder containing `launch.bat`, `wake_velocity.exe`, and assets |
| Matching `.zip.sha256` files | SHA-256 checksums for both archives |

Here `<version>` is the full release tag, including its `v` prefix (for example,
`wake_velocity-v0.1.0-windows-x64.zip` or `wake_velocity-v0.1.0-rc.1-macos-arm64.zip`).
The Windows ZIP contains a folder with the same versioned stem. The Mac app is
`Wake Velocity.app`, with `Contents/MacOS/wake_velocity` as its executable.
Both packages record the full version in `build.txt`; the Mac bundle version uses
the numeric part of a semantic version tag. Branch/manual builds use `dev-<full-commit-sha>`
for their Actions artifact names and packages. Local Mac packaging defaults to `dev`.
Since 2026-10-25 the CMake target, source files and developer commands also use
`wake_velocity` (formerly `boat_study`), so the packaged and built executable names match.

See [Mac first-launch instructions](MACOS_BUILD.md#download-and-open) and
[Windows controls and startup](WINDOWS_BUILD.md#download-and-test). The Mac app
retains the preferred river launch settings and still may require **Open Anyway**.
Release hosting does not add Apple notarization or Windows publisher signing.

Normal branch pushes and manually dispatched platform builds continue to upload
Actions artifacts without publishing a release. The release workflow reuses those
same platform workflows at the tagged commit; it does not duplicate their build
recipes. Only its publication job receives `contents: write`, using GitHub's
provided token. No personal access token or additional secret is needed.

## Status and verification

Current status, 2026-10-28: **[v0.0.20](https://github.com/doomdavve/proteus/releases/tag/v0.0.20)**
is published at commit `2f9adfc`, after [PR #34](https://github.com/doomdavve/proteus/pull/34)
passed macOS, Windows and Linux CI. The
[native release run](https://github.com/doomdavve/proteus/actions/runs/40668366272)
and [Pages deployment](https://github.com/doomdavve/proteus/actions/runs/40668366195)
both succeeded. Both published native ZIPs were downloaded and passed SHA-256 and
ZIP-integrity checks; both `build.txt` files identify v0.0.20 and the tagged commit.
The Mac metadata also reports `Uncommitted changes: True`; the workflow's generated
`build-mac/` directory is not ignored by the current packaging status check.
Neither downloaded package was launched locally; CI performed package relocation checks.

This release collects the changes since v0.0.19: outer-map Höjdmodell terrain and river
scenery, expanded deathmatch weapons and optional rearview, multiplayer smoothing, the
docked VINGA LINE ferry, marine menus and scrolling credits (including erik), solo time
controls through 8x, and the optional nautical minimap (`N`). Multiplayer uses protocol v7;
hosts and clients should use compatible versions. The required Winterfell cutover to v0.0.20
completed on 2026-10-28 through `davve-srv` commit `2cf0974`: matching authority and hosted
browser built, room/relay active, public metadata protocol 7 / physics 9, full repeat
`changed=0`. Hosted browser and live Pages named join/drive/weapon input/leave/rejoin
passed alongside a matching native bot; [hosting evidence](MULTIPLAYER_HOSTING.md#status-and-verification--2026-10-28)
owns client verification and its limits. Next: human package/input/audio review and wider
multiplayer qualification.

Local verification, 2026-10-28, Apple Silicon/macOS: Debug configure/build passed;
`ctest --preset full` passed all 232 enabled tests, with 13 retired cases disabled
and `PROTEUS_GPU_TESTS=OFF`. CPU basin, river and outer captures and the GPU title
capture were inspected; a 120-frame windowed river GPU demo completed. The build
reported only raylib's existing bundled stb_vorbis pointer-comparison warning.
Human input/listening, downloaded-app execution and user acceptance are not claimed.

Earlier publication evidence, 2026-10-26: **v0.0.18** was published at commit `2f94335`. The
[release run](https://github.com/doomdavve/proteus/actions/runs/40371176253) passed macOS,
Windows and publication; [Pages](https://github.com/doomdavve/proteus/actions/runs/40371176205)
also built and deployed successfully. Both native ZIPs were downloaded and passed SHA-256
and ZIP-integrity checks; both `build.txt` files identify v0.0.18 and the tagged commit.
Neither downloaded package was manually launched. Winterfell subsequently cut over through
`davve-srv` main `dc206f9`, with native/browser builds from that tag, an idempotent wrapper rerun,
and automated named joining plus weapon-input/combat-page checks. Next: human package and
weapons review, then wider platform/soak qualification.

Earlier publication evidence (superseded by v0.0.18): native package naming and versioned
archives, checksums and Actions artifacts are implemented. The macOS executable,
bundle display name, Windows launcher/readme and CLI usage use the released name.
Internal source/CMake names remain unchanged. Version tags `v0.0.1`–`v0.0.17` have since
been pushed to `origin` (2026-10-17 to 2026-10-26; `v0.0.17` at `2b55994`, after the MSVC
initializer and `-ffp-contract=off` portability fixes). Checked with `gh` on 2026-10-26: the
`v0.0.17` release run (40306222878) and the web run succeeded, the macOS, Windows and Linux
runs at `2b55994` succeeded, and `v0.0.17` was the latest GitHub release at that check with macOS arm64 and
Windows x64 ZIPs plus checksums. Both ZIPs were downloaded and matched their `.sha256` files;
neither package was launched. The `v0.0.14` and `v0.0.15` release runs failed (the race
smoke-test timing defect fixed in `v0.0.16`); `v0.0.11`–`v0.0.13` and `v0.0.16` are also
published. Next: launch both packages for user review.

Verification on Apple Silicon/macOS 26, 2026-10-24:

- Debug configure/build passed. `ctest --test-dir build -j 4 --output-on-failure`
  passed all 110 enabled tests; 15 retired wake-field checks stayed disabled. The initial
  sandboxed attempt hit loopback permissions and was stopped; the complete rerun used
  desktop/socket access.
- Eight offline publisher tests passed, including prerelease filenames and rejection
  of assets for a different version. Actionlint and `bash -n` passed.
- A real `v0.1.0-rc.1` Mac ZIP from the local Debug build passed checksum, extraction,
  strict signature, bundle metadata, relocated CLI help/CPU capture, title capture,
  a 120-frame GPU demo, four-second no-argument startup and scripted server checks.
  Title and CPU gameplay captures were inspected.
- Windows versioned ZIP layout and checksum passed with a fixture executable; this
  does not verify PowerShell packaging or Windows execution. Changed local documentation
  file links and `git diff --check` passed.

No release was published during that local verification (later tags are listed in the
current status above). Updated CI, real Windows packaging/execution, browser-download
Gatekeeper behavior, human input/audio playback and user acceptance remain unverified.

### Earlier release automation evidence

2026-10-18: reusable platform workflows, version-tag and published-release triggers,
Windows release ZIP/checksum, draft/upload/publish script, and documentation implemented.
Seven offline tests passed using a fake GitHub CLI: stable/prerelease creation,
reruns, upload failure, create failure, missing assets, and invalid tag rejection.
All workflows passed actionlint 1.7.12 (ShellCheck unavailable); the publication
script passed `bash -n`. Debug configure/build and all 69 CTests passed on Apple
Silicon/macOS 26 with desktop access, including 18 GPU checks. The archive step's
folder layout and both checksums were checked locally with fixture Windows files
and the real local Mac ZIP, using `shasum -a 256` in place of Linux's `sha256sum`.
Changed documentation links and `git diff --check` passed. No release has been
created or uploaded as part of local verification.

The Windows fix at `3bffdb7` passed its real
[GitHub run](https://github.com/doomdavve/proteus/actions/runs/39164837209), including
handling/capture tests, packaging, relocation, and artifact upload. The Mac build's
existing evidence is in its [guide](MACOS_BUILD.md#implementation-status-and-verification).

Historical next step (superseded by the current status above): publish a version to verify
the full release path and download both ZIPs. Actual release publication, GitHub
token permissions, and human Windows GPU/audio/control playback remain unverified.
