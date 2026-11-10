# Bounded routine CI

Status, 2026-11-10: implementation in progress; current-main/full verification pending.
Budget: three minutes per routine workflow, including setup/build/tests; test step one minute.
Open items: hosted cold/warm Windows timings and manual/release path validation.
Done when: routine jobs enforce the budget and run a curated, size-limited lane; full
qualification remains manual/release, and obsolete PR revisions are cancelled.
Next: rebase onto merged main, verify final selection/build/full suite, document limits.

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
