# Cache the pinned Lua download in CI

Status, 2026-11-10: local implementation and verification complete; hosted cache checks pending.
Open items: hosted cache miss/save/hit evidence after publication.
Done when: restored archives configure without upstream access, pins invalidate the cache,
and the shared CMake declaration retains SHA-256 verification.
Next: PR authorization, then inspect hosted cache miss/save/hit on CI.

## Scope

PR #180's Windows run failed before compilation because lua.org was unreachable during
Lua download. Cache the original pinned archive across the five native/browser workflows.
Keep the pin and explicit download directory in `cmake/LuaSource.cmake`; each cache key
hashes that file. The browser authoring path filter also includes the new declaration.

Checked the Open loops board in [the index](README.md#open-loops). This infrastructure
track defers the existing plugin P5, lighting/user acceptance, platform/human play and
other gameplay/graphics loops. It does not change dependency versions, add mirrors,
vendor third-party sources or cache compiled outputs. The contract is in
[testing](TESTING.md#ci-dependency-download-cache).

## Steps

1. Implement the shared download location and native/browser workflow caches: done locally.
2. Verify archive reuse offline, workflow syntax, native build and full suite: done locally.
3. Close the local implementation: done; contract in the testing spec and evidence below.
   Hosted Actions cache behavior remains a follow-up after a separately authorized PR.

## Verification

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
