# Windows hardware results — 2026-11-03

Saved evidence for 60 benchmark runs: two six-run AMD collections, a 24-run AMD full matrix
and a 24-run GeForce full matrix. Each linked file preserves complete JSON summaries;
[deadline samples](deadline-misses.md) preserve the relevant source CSV fields for all 13
measured frame deadline misses. Raw ZIPs and full CSV traces remain in the supplied Downloads
archives, not Git; the hashes below identify those exact inputs. Collection logs include local
user paths and are not copied. No executable content is retained.

## Source archives

| Archive / saved summaries | Bytes | SHA-256 |
| --- | ---: | --- |
| [collect-30675-25827.zip](collect-30675-25827.md) | 1344121 | `0ea9d8efce0c040a1e718cdcbde68f9736b6e8a1f954c33638f2a83962888a73` |
| [collect-28144-18884.zip](collect-28144-18884.md) | 1349052 | `e4fbc2e13af4a5c8ba47321e7ea19b39e9a198efe9427294e7fd4f951b0a1007` |
| [collect-167-25388.zip](collect-167-25388.md) | 5417112 | `8459aa79f4550864ba0b06730b82a1b3973a0c0c2aac6ca3ce2a6b50243941a2` |
| [collect-4498-243.zip](collect-4498-243.md) | 5018969 | `ccf75e4be527da11fdac15057cbc84aecd4c55b1f8e9345d60547d8a5b040a6c` |

The outer `collect-28144-18884.zip.zip` (SHA-256 `ed32e27c6374b94b98effbbee2b1ebec1aa28c45bc2d8ff42e02aa90a0757217`)
contains both initial ZIPs. Its `collect-30675-25827.zip` exactly matches the standalone input;
it is counted once.

## Full-matrix comparison

Collection logs pin source commit `7e3e4b9743f4b258c0d888e40c5ca868922c62a4`.
Both full matrices use build `7e3e4b9//MSVC-19.44.35229.0`, Windows/AMD64, hidden/unpaced,
120 Hz simulation and a 120-frame warmup. AA off renders internally at 320×200; 4x4 at
1280×800. Values below are medians of three per-run percentiles, not pooled percentiles.
AMD renderer: `AMD Radeon(TM) Graphics`, OpenGL `3.3.0 Core Profile Context 2026.04.17.260319`.
NVIDIA renderer: `NVIDIA GeForce RTX 4070 SUPER/PCIe/SSE2`, OpenGL `3.3.0 NVIDIA 610.88`.

| GPU | Scenario | Water | AA | Frame p50/p95/p99 ms | GPU p50/p95/p99 ms | Submit p95 ms | Frame misses |
| --- | --- | --- | --- | --- | --- | --- | --- |
| AMD | river-review | flat | 4x4 | 6.182 / 7.977 / 8.535 | 5.889 / 7.656 / 8.204 | 0.824 | 0 |
| AMD | river-review | flat | off | 1.452 / 2.566 / 3.076 | 1.067 / 2.262 / 2.777 | 0.772 | 0 |
| AMD | river-review | waves | 4x4 | 8.837 / 10.540 / 11.008 | 8.532 / 10.185 / 10.690 | 0.905 | 0 |
| AMD | river-review | waves | off | 1.962 / 3.038 / 3.449 | 1.479 / 2.714 / 3.129 | 0.929 | 0 |
| AMD | stress-basin | flat | 4x4 | 10.172 / 11.727 / 12.046 | 9.842 / 11.382 / 11.689 | 1.183 | 0 |
| AMD | stress-basin | flat | off | 5.837 / 6.432 / 6.741 | 5.053 / 5.776 / 6.048 | 1.130 | 0 |
| AMD | stress-basin | waves | 4x4 | 10.254 / 11.736 / 11.993 | 9.891 / 11.413 / 11.672 | 1.255 | 0 |
| AMD | stress-basin | waves | off | 5.958 / 6.531 / 6.852 | 5.150 / 5.749 / 6.102 | 1.248 | 0 |
| RTX 4070 SUPER | river-review | flat | 4x4 | 1.047 / 1.348 / 1.478 | 0.193 / 0.269 / 0.991 | 1.229 | 3 |
| RTX 4070 SUPER | river-review | flat | off | 1.050 / 1.342 / 1.459 | 0.111 / 0.181 / 0.927 | 1.218 | 3 |
| RTX 4070 SUPER | river-review | waves | 4x4 | 1.115 / 1.452 / 1.908 | 0.250 / 0.317 / 1.102 | 1.305 | 3 |
| RTX 4070 SUPER | river-review | waves | off | 1.105 / 1.451 / 1.924 | 0.128 / 0.193 / 0.962 | 1.298 | 3 |
| RTX 4070 SUPER | stress-basin | flat | 4x4 | 2.021 / 2.359 / 2.766 | 0.444 / 0.921 / 1.141 | 1.414 | 0 |
| RTX 4070 SUPER | stress-basin | flat | off | 2.019 / 2.411 / 2.803 | 0.322 / 0.788 / 0.973 | 1.432 | 0 |
| RTX 4070 SUPER | stress-basin | waves | 4x4 | 2.192 / 2.624 / 2.977 | 0.480 / 1.000 / 1.212 | 1.499 | 0 |
| RTX 4070 SUPER | stress-basin | waves | off | 2.190 / 2.658 / 3.022 | 0.346 / 0.853 / 1.010 | 1.507 | 0 |

All 60 runs completed without skipped GPU samples. Each full matrix has 81,810 measured frames;
corresponding AMD/NVIDIA runs have matching final state strings and the listed settings/revisions.
The initial AMD pair has one 30.774 ms presentation hitch; the AMD full matrix has no frame
misses. The NVIDIA matrix has 12 frame misses, exactly one in each river repetition and none
in stress. NVIDIA hitches span 46.687–67.567 ms, dominated by CPU render submission
(46.522–67.428 ms); GPU time at those samples spans 0.100–0.241 ms. They occur at frames
3725–4299, destination 4, at varying positions. This is a recurring observation, not proof of
one fixed world trigger or a diagnosis of transfer, shader, driver or loading behavior.

## Deferred river hitch

Status: open, investigation explicitly deferred by the user on 2026-11-03. No fix or additional
profiling was attempted. Do not describe the NVIDIA baseline as hitch-free or infer visible
smoothness from p95/p99 alone. On resumption, use the linked source identifiers and deadline
samples to reproduce the river stall on the same build/GPU/settings, then profile the CPU
render-submission interval. Reproduction and profiling are future work, not checks performed.

## Verification and limits

2026-11-03, Mac analysis of user-collected Windows data: ZIP CRC checks; duplicate ZIP byte
comparison; JSON parse; CSV total/measured row counts and frame deadline counts; complete-run
and GPU sample checks; corresponding full-matrix settings and final state comparisons. These
checks do not constitute new game playback or human acceptance. CPU/GPU intervals overlap;
do not add their percentiles. Steady NVIDIA frame times suggest CPU/submission work dominates
these workloads, but neither transfer cost nor the hitch's cause is isolated. Visible/paced
play, live F4 export, capture overhead, other maps and newer build revisions remain unqualified.
