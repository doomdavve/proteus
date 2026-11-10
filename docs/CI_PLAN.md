# Bounded routine CI

Status, 2026-11-10: implemented and locally verified on merged main; hosted timing pending.
Budget: three minutes per routine workflow, including setup/build/tests; test step one minute.
Open items: hosted cold/warm Windows timings and manual/release path validation.
Done when: routine jobs enforce the budget and run a curated, size-limited lane; full
qualification remains manual/release, and obsolete PR revisions are cancelled.
Next: PR authorization, then validate hosted three-minute jobs (especially cold Windows).

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
