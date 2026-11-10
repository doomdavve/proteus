# Reliable Lua source availability

Status, 2026-11-10: vendored archive implemented and locally verified; updated CI pending.
Open items: updated native/browser CI, especially Windows cold configuration.
Done when: fresh builds extract the checksum-verified archive without lua.org or a cache.
Next: updated #182 CI with preview-test deadline adjustment; then merge before #180.

## Scope and revised decision

PR #182's cache-only Windows run failed during its first Lua download, while Linux, macOS
and browser checks passed. Caching cannot bootstrap when the upstream endpoint is
unreachable. The user requested a way out of the repeated failures. Ship the unmodified
373,429-byte Lua 5.4.9 archive, whose hash already matches the published pin, in
`third_party/lua`. Extract it locally with the same SHA-256 check. Remove the now-unnecessary
Lua cache actions; retain the shared declaration and broaden the browser dependency filter.
The current contract is in [testing](TESTING.md#lua-source-availability); the archive's
provenance, license and upgrade procedure are in [the vendor guide](../third_party/lua/README.md).

Checked the Open loops board in [the index](README.md#open-loops). Existing plugin P5,
lighting/user acceptance, platform/human play and gameplay/graphics loops remain deferred.
No dependency version, VM behaviour, compiled-output cache or network mirror is added.

## Steps

1. Cache-only implementation and local verification: completed, superseded after hosted failure.
2. Vendor the original verified archive and remove cache-only scaffolding: implemented locally.
3. Verify a fresh uncached extraction with upstream blocked, build/full suite and workflows;
   close into the testing/plugin specs: done locally; publication to #182 authorized.

## Superseded cache-only verification

2026-11-10, Apple Silicon/macOS: seeded a fresh build's download directory with the original
Lua archive from an existing build, then configured with HTTP/HTTPS/ALL proxy variables set
to the closed loopback port 9. Raylib/ENet used existing local source overrides; Lua had no
source override or prior extraction/build stamps. Configuration passed, extracting the
verified cached Lua archive without reaching upstream (`build/cache-configure.log`).
The original archive matched the pinned SHA-256. Native Debug build passed with only the
existing upstream stb_vorbis pointer-comparison warning. `ctest --preset full` passed all
544 enabled tests (15 retired checks disabled) in 153.13 seconds, without retries
(`build/cache-build.log`, `build/cache-full.log`), with GPU/socket access. Actionlint 1.7.12
validated all five changed workflows (shellcheck unavailable/disabled). Changed documentation
links/anchors and `git diff --check` passed. No audiovisual behavior changed; no new manual
playback review was needed. Windows/browser execution and hosted cache save/hit behavior
remain unverified locally.

The user authorized opening the PR on 2026-11-10. Main remains at `75375b3`, the
verified base; this publication update changes documentation only.

## Vendored archive verification

2026-11-10, Apple Silicon/macOS, Debug: a fresh minimal FetchContent project extracted
Lua with HTTP/HTTPS/ALL proxies pointed at the closed loopback port 9, with no cache,
source override or prior Lua extraction (`build/vendor-offline.log`). A second fresh
project using an intentionally wrong SHA-256 failed before extraction
(`build/vendor-bad-hash.log`). The vendored archive matches the original pinned hash.
The full project configured with the same blocked proxies using only existing raylib/ENet
source overrides (`build/vendor-configure.log`); Lua used the local original archive.

Native build passed warning-clean (`build/vendor-build.log`); all 544 enabled full-preset
tests passed (15 retired checks disabled) in 103.09 seconds without retries
(`build/vendor-full.log`), with GPU/socket access. Actionlint 1.7.12 passed for the changed
browser workflow (shellcheck disabled/unavailable); the other four workflows were restored
to main because they no longer need cache steps. Changed documentation targets and
`git diff --check` passed. No audiovisual behavior changed, so no new playback review.
Updated Windows/browser CI remains pending; no updated hosted success is claimed yet.

## Repeated Linux preview-test timeout, 2026-11-10

The vendored-archive revision passed Windows, macOS and browser CI. Linux configured/built
successfully but failed `boat_plugin_author`: `Worker.test_preview` exceeded its 10-second
child deadline on both the original run and a retry; 507 other enabled tests passed. This
is separate from the resolved Lua download failure. Local preview measurements on the same
Debug build were approximately 0.04/0.88/0.05/0.03 seconds for langedrag/harbor-life/shelter-tour/
river-sprint. These measurements do not establish Linux runner timings or prove absence of
another slowdown. The worker output is a temporary file, so a stdout pipe-fill deadlock is
not the cause.

Allow 30 seconds per finite preview and 180 seconds for the integration CTest, retaining
all 360 ticks, eight previews and exact evidence comparisons. Add package subtest labels
to make any repeated failure actionable. Production virtual deadlines remain unchanged.
Local verification (Apple Silicon/macOS, Debug): rebuild passed with only the known upstream
stb_vorbis pointer-comparison warning. The full preset passed all 544 enabled tests (15 retired
checks disabled), including `boat_plugin_author` and the virtual-time audit; see
`build/preview-deadline-full.log`. `git diff --check` passed. The hosted Linux result remains
pending; the larger deadline is not proof of the runner's underlying performance cause.
