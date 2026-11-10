# Bounded routine CI

Status, 2026-11-10: implemented and measured on #183; all routine workflows pass under 3 minutes.
Budget: three minutes per routine workflow, including setup/build/tests; test step one minute.
Open items: manual/release routes were reviewed/linted but not executed in this track.
Done: curated lane and size guard; old PR revisions cancelled; cold Windows verified at 2m30s.
Next: PR review/merge; keep future additions within the existing budget.

## Scope and deferrals

The user set a three-minute workflow limit after repeated long feedback cycles. Checked
[Open loops](README.md#open-loops); gameplay, plugin P5, lighting/human acceptance and wider
platform qualification stay with their existing owners. This track changes routine CI
selection/setup rather than enlarging individual failure deadlines. No new product feature.

## Evidence and choices

Observed successful hosted runs before this change: Windows used 111 seconds installing
curl, 54 configuring, 61 building and 144 testing; macOS used 317 seconds testing. Source:
Windows run 42885552228, macOS 42885552071, Linux 42883425956. Repeated package previews,
full-route simulations, captures, benchmarks and package qualification leave routine PR/main
checks; they remain available through manual/native release qualification and local full CTest.

The curated ci target builds the game and only selected validators. Case discovery labels
only quick cases from selected families. Its post-build guard requires the essential boundary
and asset checks, 50..150 checks, and no disabled/GPU/exhaustive additions. The initial local
108-check lane passed in 12.24 seconds before adding the guard's own unit test.

Windows uses a Release-only static curl/zlib triplet for routine jobs, with ABI-checked
vcpkg binary archives cached by runner image/vcpkg revision/triplet. Qualification keeps the
standard triplet. A cold Windows setup has not yet been timed; do not claim three-minute
success from the configured timeout. Manual/release paths retain their longer deadlines.

## Closing and local evidence

The budget/selection contract and commands are in [testing](TESTING.md#routine-ci-budget),
with affected platform guides updated. The Lua-availability plan is closed after its now
successful native/browser CI and merge; this track owns the remaining runtime work.

Rebased cleanly onto main `09acc4b`, which includes #180, #181 and #182. On Apple Silicon/
macOS, Debug, GPU checks enabled, pinned raylib/ENet sources reused locally and the merged
vendored Lua archive: configure and the isolated `ci` target passed. The final 109-check
routine lane passed in 10.76 seconds with two CTest workers (`build/ci-main-tests.log`);
it built only its declared targets before any full build. Its unit test checks the size
limits, missing essential checks and expensive/disabled additions.

Full build passed warning-clean on this final main-based configuration, and the required
local `ctest --preset full` passed all 555 enabled tests (15 retired checks disabled) in
126.85 seconds, without retries (`build/ci-full-build.log`, `build/ci-full-tests.log`).
Actionlint 1.7.12 passed all workflow files with shellcheck unavailable/disabled. Changed
documentation links/anchors and `git diff --check` passed. No product audiovisual behavior
changed; no new playback review was needed.

The configured limits are enforcement, not measured hosted performance. Windows binary
cache cold/warm operation, the Release-only triplet and all three-minute hosted jobs still
need CI evidence. Full manual/release routing was reviewed and linted but not executed on
GitHub locally. Queue/runner allocation time is outside the job timeout. No PR is opened
without the user's approval. These limits remain explicit rather than claiming a three-minute
end-to-end guarantee from local test timings.

The user authorized opening the PR and measuring hosted workflow time on 2026-11-10.

## First hosted measurement, 2026-11-10

PR #183's first revision: Linux passed in 1m13s (42891501294), browser passed in 1m11s
(42891501508). macOS failed in 1m29s (42891501473): `boat_room_package.match_room` ran a
13-second three-client combat simulation but was incorrectly labelled quick, exceeding the
10-second child deadline. Mark it slow so it stays in full qualification; do not raise the
routine deadline. The corrected 108-check local lane passed in 5.32 seconds.

Cold Windows completed dependency installation in 66 seconds, saved its binary cache in
one second, configured in 31 seconds and built for 46 seconds before failing (2m38s total,
42891501388). The newly selected ENet validator exposed an existing unguarded GCC warning
flag that MSVC rejected. Scope `-Wno-unused-function` to GNU/Clang-family compilers and keep
the protocol checks. Neither failed run establishes successful cold Windows qualification.
Final hosted measurements remain pending.

The corrected first warm Windows run failed in 2m42s (42892238965) on a pre-existing cargo
validator comparison of independent structs including padding. Compare all defined cargo,
crane and event fields instead; keep byte comparisons only where testing no mutation of the
same object. No production cargo behavior changes. Configuration took 66s versus 31s in
the earlier run; use Ninja with the same MSVC compiler for routine checks to reduce setup/
project overhead. Manual/release qualification retains its Visual Studio generator.
Linux passed in 1m30s, macOS 1m12s and browser 1m36s on that revision.

A deliberate cache deletion and Windows rerun (42893264938 attempt 2) proved cold compilation
and all 107 checks passed, but the job reached cancellation during final upload/cleanup at
3m03s. Installation took 84s, Ninja configuration 29s, compilation 36s and tests 11.63s.
The budget is not raised. Upload JUnit artifacts only for failing runs, and correctly mark
`race_room` (70 virtual seconds with clients) and `scene_contact_room` (track sampling plus
a 25-second networked contact run) slow, like the existing impairment variants. These remain
in full qualification. Final cold/warm measurements are repeated after these cuts.

## Final measured routine lane, 2026-11-10

Code revision `d2a3fb4`, native/platform settings from the workflows: all four workflows
passed. Durations below use each GitHub job's startedAt/completedAt, include setup/build/
tests/cleanup, and exclude queue/runner allocation before job start.

| Workflow | Full job | Native test step | Evidence |
| --- | --- | --- | --- |
| Linux, Debug | 1m45s | 6s | [42894617272](https://github.com/doomdavve/proteus/actions/runs/42894617272) |
| macOS, Release | 1m16s | 5s | [42894617392](https://github.com/doomdavve/proteus/actions/runs/42894617392) |
| Windows, Release, deliberately cold dependency cache | 2m30s | 8s | [42894617183](https://github.com/doomdavve/proteus/actions/runs/42894617183) |
| Browser PR build | 59s | separate browser contract/build lane | [42894617256](https://github.com/doomdavve/proteus/actions/runs/42894617256) |

For the Windows cold measurement, deleted only the PR-scoped 7 MB vcpkg binary archive
cache before the run. Installation rebuilt the dependencies in 68s, cache save took 1s,
MSVC setup 2s, Ninja configuration 20s and compilation 36s. An earlier warm run of the same
Ninja/MSVC setup (before the last room-case reductions) passed in 1m41s, with installation
28s, configure 19s and build 28s (42893264938 attempt 1). The final documentation-only update
will also trigger a warm run; its latest timing can be inspected in PR #183. These are
observed samples, not a promise that network or runner load never reaches the enforced cap.

The final lane has 106 checks locally (105 on Windows), retains quick decoding/catalog/
state checks and the two asset-loading captures, and keeps all room simulations in the full
suite. Local final focused lane passed in 3.22s (`build/ci-trimmed-tests.log`). The guard's
failure-path tests and all-workflow Actionlint checks passed. The local full preset passed
all 555 enabled tests before the cargo padding comparison fix. After that test-only fix,
the full local attempt passed all 520 CPU checks but 35 GPU cases could not initialize a
macOS monitor; a targeted retry hit the same display failure (`build/ci-portability-full-tests.log`,
`build/ci-portability-gpu-retry.log`). Final GPU re-verification is therefore unavailable,
not claimed passed. Hosted native routine checks, including cargo, passed on all three OSes.

The current contract is in [testing](TESTING.md#routine-ci-budget); there are no routine-lane
implementation leftovers. Manual/release jobs retain their existing broader qualification
and longer limits, but those paths were not executed on GitHub in this track. No product
visual/input/audio behavior changed, and no human playback/acceptance is inferred.
