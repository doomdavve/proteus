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

Use Python 3.11+ from a clean, separate source worktree at the reviewed `origin/main`
commit. A detached source worktree is supported. Prepare a separate `davve-srv` worktree
on a branch tracking `origin/main`; fetch the hosting baseline first. Keep its existing
controller vault file in place and pass its path rather than copying it.

For the next version (replace the tag and paths as appropriate), check prerequisites first:

```sh
python3 tools/cut_release.py v0.0.22 \
  --hosting-repo /path/to/clean-davve-srv-worktree \
  --vault-password-file /path/to/davve-srv/ansible/.vault_pass.txt \
  --browser-python /path/to/playwright-venv/bin/python \
  --check
```

Repeat the command without `--check` to release. The command checks clean repositories,
source/tag identity, hosting baseline ancestry, the vault file, required tools, Playwright,
Chrome and noninteractive SSH/sudo access before publishing anything. `--check` fetches Git
refs and checks access, but creates no tag, output directory or hosting change. Required tools:
Git, GitHub CLI authenticated for this repository, CMake, CTest, curl, SSH and Ansible.
Install Playwright in the chosen Python environment. Chrome defaults to the macOS application
path; `--chrome` selects another installed executable. `--ssh-host` selects the Winterfell
SSH alias. The vault path can also come from `ANSIBLE_VAULT_PASSWORD_FILE`, falling back to
`ansible/.vault_pass.txt` in the hosting worktree. Vault contents are never printed.

A full run:

1. Configures the source worktree's `build/` as Debug with GPU tests off, builds all targets
   (including the matching native bot), and runs `ctest --preset full`.
2. Creates/pushes the immutable tag, waits for native publication and Pages, downloads both
   ZIPs and verifies SHA-256, ZIP integrity and embedded version/commit.
3. Commits and pushes the hosting version pin, then fast-forwards its private remote baseline
   (`main` by default, configurable with `--hosting-branch`) before running the ordinary
   Winterfell role. Only pin changes may be ahead of that baseline; unrelated hosting work
   must be merged first. Concurrent pushes fail without force or automatic rebasing.
4. Verifies room/relay health, deployed source commit, both live browser versions and named
   joins, directory protocol/physics, and native joining with a consistent final state and
   zero decode errors.

Logs, browser captures, checksums and `verification.json` go under
`build/releases/<tag>/` by default (`--output` overrides this). Evidence records the current
stage and failure or success, so a failed retry does not leave an earlier success record.
`--timeout` bounds publication waiting (default one hour); build, deployment and client
checks also have failure deadlines. The default output directory is ignored by Git; custom
output should also be ignored or outside the source worktree.

After a failure, inspect the stage log, resolve the issue and rerun the same command from the
same source commit/tag. Retries accept an existing tag even after `origin/main` advances,
but refuse to move a tag or deploy an unmerged source commit. An unpushed hosting pin commit
from a failed push can be retried; unrelated unpushed commits are rejected. After a concurrent
hosting baseline update, reconcile that worktree explicitly before retrying. Publication
reruns remain governed by the immutable-tag rules below.

For an already deployed release, add `--verify-only`: it rebuilds/tests the selected source,
downloads/checks the existing release and verifies live services/clients, without pushing
tags, changing pins or running Ansible. The source worktree must still match that tag.
This command does not merge Proteus PRs, update narrative documentation, run mobile-device
emulation, or replace human package/input/audio review. After success, update this guide,
the hosting guide and `davve-srv/docs/wake-velocity.md` with evidence and remaining limits.

Reusable-command hardening, 2026-10-28: detached source worktrees and retries after main
advances are supported; local build/full CPU tests, hosting baseline promotion, controller
preflight, verification-only mode and per-tag stage logs/evidence are implemented. Debug
configure/build and all 234 enabled CPU CTests passed on Apple Silicon/macOS, with GPU off
and 13 retired cases disabled. The expanded offline release guards passed 17 tests, including
real temporary Git history checks and virtual-clock workflow polling; the final focused
CTest rerun passed. Changed documentation file links and whitespace were checked. This
hardening has not published another release or redeployed Winterfell.
[PR #40](https://github.com/doomdavve/proteus/pull/40) passed macOS, Windows and Linux CI.
The merged `bb8e7c8` command passed live `v0.0.22 --check` using the existing controller,
Playwright environment, vault file and noninteractive SSH/sudo setup. No v0.0.22 tag or
release output directory was created, and the hosting worktree remained clean.
The next release will
qualify the revised complete command against live publication/deployment; physical-device,
human package/input/audio and wider network review remain open. v0.0.21 remains the deployed
release; its live evidence below belongs to the earlier command.

Preparation evidence, 2026-10-28: v0.0.21 collects the merged mobile browser controls and adaptive
viewport work since v0.0.20. Release orchestration and offline archive/pin guards implemented;
Debug build and all 233 enabled CPU CTests passed (13 retired cases disabled, GPU tests off).
The CPU river capture was inspected. Publication and hosted cutover subsequently completed
as recorded below. The macOS CI build directory is now ignored so generated build output
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
[Pages setup](WEB_BUILD.md#github-pages-setup), including allowing `v*` tags
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

Current status, 2026-10-28: **[v0.0.21](https://github.com/doomdavve/proteus/releases/tag/v0.0.21)**
is published at `0cb1921224149ddeb4707e818dd2069096bc9e3d`, after
[PR #38](https://github.com/doomdavve/proteus/pull/38) passed macOS, Windows and Linux CI.
The [native release](https://github.com/doomdavve/proteus/actions/runs/40685734545) and
[Pages](https://github.com/doomdavve/proteus/actions/runs/40685734544) workflows succeeded.
The new release command completed publication, both downloaded ZIP checksum/integrity and
version/commit checks, then the required Winterfell authority/browser cutover through
`davve-srv` pin `e6d0871`. Both services are active on the matching tag; public metadata is
protocol 7 / physics 9. The full deployment repeat reported `changed=0 failed=0`.
Both live browser sites passed named joining/driving; a matching native bot received 150
snapshots, repaired eight mismatches and finished consistent with zero decode errors.
The Mac ZIP reports `Uncommitted changes: False`. Neither downloaded package was launched
locally; CI performed relocation checks. Android-emulated mobile touch, tilt, fullscreen and
portrait/landscape checks passed against Pages. iPhone-style emulation passed against Winterfell,
including connected touch/tilt commands and leaving the room. Representative desktop/mobile
captures were inspected. Physical device/audio/human acceptance remain
open. [Hosting evidence](MULTIPLAYER_HOSTING.md#status-and-verification--2026-10-28) owns
deployment limits. Next: physical phone and native package play, then wider multiplayer
qualification. Local evidence: `build/release/verification.json` and browser logs/captures in
the release worktree; orchestration log `/private/tmp/proteus-release-v0.0.21.log`.

The merged release commit also passed Debug configure/build and all 233 enabled CPU tests
(`ctest --preset full`, GPU off, 13 retired cases disabled). CPU river/outer captures were
inspected and a 120-frame windowed GPU river demo completed. The two offline archive/pin
tests and eight existing publisher tests passed. No new human playback is claimed.

Earlier publication evidence, 2026-10-28: **[v0.0.20](https://github.com/doomdavve/proteus/releases/tag/v0.0.20)**
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
