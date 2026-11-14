# Server hardening: limiting what `boat_server` can do and find

Status, 2026-11-14: **H0 (systemd sandbox) is deployed on Winterfell; H1-H3 are merged; H4 code side is merged (operator work owed); H5 is implemented on `feat/h5-lockdown`; H6-H7 are next.**
Completed task: H5, `boat_server --lockdown kill|log|errno` (Landlock ABI 6, `no_new_privs`, hand-written seccomp allowlist); it fails closed. Evidence below.
Open: `davve-srv`: enable `--lockdown` in the unit and soak with `--lockdown log` to tune the allowlist; H4 operator work (nftables, crash alert); H1 credential unit; the WebRTC syscalls under lockdown are unverified;
the longer H3 fuzz campaign; relay join from outside the LAN.
Done when: every step's gate passes, the closing step has moved the contract into a spec, and
the remaining risk is written down as a decision, not an omission.
Next task: H6 decision (privilege separation), with the H5 operator work in `davve-srv`.

`boat_server` is C, public on GitHub, and reachable from the internet over UDP (ENet) and
WebSocket signalling (WebRTC). A memory-safety bug in anything that parses network bytes is a
potential remote code execution. We cannot promise there are none, so the plan works in two
directions: **make exploitation unprofitable** (a compromised process can do almost nothing)
and **find the bugs first** (fuzzing, sanitizers). [All plans](README.md).

Winterfell runs Debian 13 (Linux 6.12), so OpenBSD's `pledge`/`unveil` map to systemd
sandboxing, seccomp and Landlock. The operator-side units live in the private `davve-srv`
repository; this plan owns the intent and the code-side steps.

## Disclosure and what stays out of this repository

This plan lists generic measures only. **Do not record specific vulnerabilities, crashing
inputs or exploit notes here, in issues or in commit messages.** Report and track them through
GitHub private security advisories or the private `davve-srv` notes, fix them in a private
branch when exploitable, and write only the fixed class of bug into public history. H1 adds a
`SECURITY.md` that says so.

## Trust boundary

Untrusted: every byte from a network peer — ENet packets (`boat_net_codec`), the WebRTC
signalling and data channels (libjuice, libdatachannel, `boat_net_*`), join
tokens and names. Operator-controlled: room packages, Lua plugins, assets, command line. H3
must confirm that no client-supplied bytes reach package, Lua or scenery parsing; if any do,
that path becomes a fuzz target and a P1 for hardening.

## Steps

| Step | Work | Gate |
| --- | --- | --- |
| H0 | Systemd sandbox for the room and relay units (`davve-srv`). **Done 2026-11-14.** | `systemd-analyze security` ≤ 2.0; native and browser join |
| H1 | Remove the TURN password from `ExecStart` (it shows in `ps` and `/proc/<pid>/cmdline`): the server reads it from a credential file or environment variable; `davve-srv` supplies it with `LoadCredential=`. Add `SECURITY.md` with the disclosure route. | Password absent from `ps`; room and browser join still work |
| H2 | **Implemented 2026-11-14; passes Linux CI (Debug, no WebRTC); Release fortification and the WebRTC build are checked at the next release build.** Exploit-mitigation build flags for the server targets and their bundled dependencies: `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=3`, PIE, full RELRO and `-z now`, `-fstack-clash-protection`, `-fcf-protection`, `-ftrivial-auto-var-init=zero`. Check the built binary with `checksec`. | CI check on `boat_server`; no warning regressions; tests pass |
| H3 | **Merged 2026-11-14 (PR #193); sanitizer CI and a first libFuzzer run recorded; a longer campaign is still owed.** Sanitizer and fuzz lane (absorbs the M5 "parser fuzzing" open loop). ASan/UBSan runs of the net and room tests and the `boat_bot` soak; libFuzzer/AFL++ harnesses for the packet codec, the ENet receive path, signalling messages, join/name/token handling and the session-record codec. Seed corpus from the recorded protocol tests. Tests keep the virtual clock. | Sanitizer lane green in CI budget; harnesses run N minutes locally without findings, or findings fixed privately |
| H4 | **Code side implemented 2026-11-14 on `feat/h4-limits`; operator work remains.** Server-side limits and network hygiene: peer, packet-size and join-rate caps (in code, see [Connection limits](MULTIPLAYER_PROTOCOL.md#connection-limits)); per-source rate limit and port allowlist in nftables on Winterfell (`davve-srv`, not done); alert on `SIGSEGV`/`SIGSYS`/`EPERM` service exits (`davve-srv`, not done). | Bot flood test stays within caps; firewall rules in `davve-srv`; alert fires on a deliberate crash |
| H5 | **Implemented 2026-11-14 on `feat/h5-lockdown`; operator work remains.** In-process lockdown, Linux only (`pledge` substitute), in `src/room/boat_lockdown.[ch]` behind `__linux__`; never in `proteus_boat_core` or `proteus_boat_net`. After assets, package, plugin load, socket binding and the recording file, `boat_server --lockdown MODE` applies `no_new_privs`, Landlock (all filesystem rights; TCP bind/connect and abstract-socket/signal scopes at ABI 4/6) and a seccomp allowlist; see [Lockdown](MULTIPLAYER_HOSTING.md#in-process-lockdown). It is **opt-in** and stays off until soaked on Winterfell. **Fail closed:** if Landlock is below `BOAT_LOCKDOWN_MIN_LANDLOCK_ABI` (6, what Linux 6.12 offers), or `no_new_privs` or the filter cannot be installed, the server prints the reason and exits 1 before serving. `log` mode is an explicit tuning option; `errno` is for tests. Excludes `--package-reload`. | Server runs a full soak; a test proves a denied `open`/`execve` fails; core boundary lint passes |
| H6 | Decision only: privilege separation — a small network front that validates and forwards fixed-size messages to the authority process. Decide after H3–H5 whether the remaining risk justifies it. | Decision and reasons recorded |
| H7 | Closing step: move the contract and behaviour into `docs/SERVER_SECURITY.md` (new spec, present tense), update [hosting](MULTIPLAYER_HOSTING.md), turn leftovers into open items or drop them explicitly, then mark this plan done. | Docs match the deployed units and the code |

Order: H1 and H2 are small and independent; H3 is the main investment and can run beside them;
H4 follows H3's measured limits; H5 follows H3 so the syscall allowlist is built from a
sanitizer/soak trace; H6 and H7 close.

## What this plan defers

Hardening the game client, the browser build and the hosted web server (nginx); signing and
supply-chain verification of releases; Windows and macOS server sandboxes. Pinned third-party
dependency review is covered by the existing dependency plans, not here. The relay (coturn) is
covered only by its systemd sandbox (H0) and firewall (H4).

## Evidence

H2, 2026-11-14, macOS arm64 only: `binary_hardening_selftest` passes; the Linux branch of
`cmake/Hardening.cmake` was exercised with a faked system name (flags added or skipped as
intended, no `_FORTIFY_SOURCE` in Debug); the full quick lane passes. Linux CI (ubuntu-24.04 x86-64, Debug, no WebRTC; PR #192): all four optional flags accepted,
`boat_server_hardening` ran on the real binary and passed, along with the existing boundary
lints after allowing `_GLOBAL_OFFSET_TABLE_` (an artifact of position-independent code).
**Not verified:** `_FORTIFY_SOURCE` (the check only requires it outside Debug), the flags under
the libdatachannel/libjuice WebRTC build, and the deployed Winterfell binary; run
`python3 tests/lint/check_binary_hardening.py --fortify build-server/boat_server` on the next release build.
build, `boat_server_hardening` on a real ELF binary, and the flags under libdatachannel/libjuice.

H0, 2026-11-14, Winterfell (Debian 13): both units 7.9 EXPOSED before, 1.1 OK after
`systemd-analyze security`; playbook ok=44, failed=0; native `boat_bot --hunt --predict` 8 s
join (0 decode errors, 0 dropped, consistent); browser join confirmed by the user. Not
exercised: relay join from outside the LAN, a long soak. Deployment record and the
filter-tuning procedure: `davve-srv/docs/wake-velocity.md#service-sandbox`
(private repository).

H3, 2026-11-14, macOS arm64, Apple clang, ASan + UBSan: the 48 `boat_net*` tests (memory and ENet) pass.
Mutation runs found no crash or report: about 3,000,000 decoder inputs, 1,000,000 recording inputs and
90,000 joined-client server sessions across several seeds (the server runs reached spawn and resync
handling thousands of times). Findings fixed on the way: three unbounded `strcpy` calls in
`src/core/waterfront_route.c` (constant ids into fixed buffers, so not reachable from the network) now use
a bounded copy; ENet 1.3.18's sign-bit shift is exempted. Coverage-guided libFuzzer run on Winterfell (Debian 13, clang 19.1.7, ASan+UBSan, 2026-11-14, 10 minutes
per harness in parallel, seeded from the harnesses' own seeds): `fuzz_net_decode` 7.8M executions,
905 edges; `fuzz_net_server` 159k executions (joined client, full room), 1,057 edges; `fuzz_session_record`
27.8M executions, 29 edges (the frame checksum gates it, so it needs real recordings to go deeper). No
crash, timeout, out-of-memory or sanitizer report. Two setup bugs found only by this run and fixed: the
libFuzzer configure path registered a test property for a test that does not exist in that mode, and
the libraries under test were not instrumented (5 edges, so no guidance), so the whole build is now
instrumented. **Limits:** ten minutes is a smoke run, not a campaign; the next step is a longer run
(hours) with a recordings corpus, ideally after seeding with real session traffic. Not fuzzed: WebRTC signalling and SDP (inside
libdatachannel), room package and Lua definition parsing (operator-supplied; H3's trust-boundary
confirmation that no client bytes reach them is still owed), and the rest of the C code. `strcpy` in
`apps/dance_study/choreography.c` and `sprintf` in `tools/wake_play_review.c` are outside the server
and left alone.

## H4 evidence

Already present before H4: the 8-peer host cap, `maximumPacketSize` and per-message size and
channel validation in `boat_net_enet.c`, per-peer input/spawn/resync rate limits, the 10 s
handshake timeout, and WebRTC signalling bounds (8 pending sockets, 16 KiB messages, 15 s).
Added 2026-11-14: ENet `duplicatePeers` = 6 per source address, and a server-side token bucket on
new connections (burst 8, 2 per second, on the caller's clock, `connect_limited` statistic,
printed by `boat_server`). The values are conservative guesses, not measurements: 6 keeps a
four-player household plus two retries; revisit them with real join traffic.
Checks run on macOS, Debug, 2026-11-14: `ctest --preset full` passed (558 tests) including
`boat_net.connect_flood` (150 scripted connects in one virtual second: 10 admitted, 140
refused) and `boat_net_enet.enet_source_cap` (7 loopback clients: 6 served). Not run: a
`boat_bot` flood against a live server, per-source signalling limits (the process cannot see
the address; left to nftables), and any `davve-srv` change.
Remaining operator work (private `davve-srv`): nftables per-source connection and rate limits
and a port allowlist; an alert on `SIGSEGV`/`SIGSYS`/`EPERM` service exits, proven with a
deliberate crash.

## H5 evidence

- 2026-11-14, Winterfell scratch copy (Debian 13, Linux 6.12.116, x86-64, GCC, Debug, no WebRTC, Landlock ABI 6 reached): `validate_boat_lockdown` (denied `open`/`execve`/AF_UNIX/AF_PACKET socket fail with `EPERM`, `execve` and `open` kill with `SIGSYS`, loopback UDP exchange works, refusal installs nothing, Landlock denies files and TCP bind) and `boat_lockdown_process` (`--lockdown kill` and `log` servers with two bots and a LAN search, clean exit) passed; the scratch copy and processes were removed. The live room was not touched.
- macOS (Debug): full CTest passed with the Linux code compiled out; `--lockdown` exits 2 there.
- Not verified: WebRTC threads under lockdown (libdatachannel/libjuice may need more syscalls), a soak of the real unit on Winterfell, `log`-mode trace contents (kernel audit log was not readable from the scratch account), aarch64.
