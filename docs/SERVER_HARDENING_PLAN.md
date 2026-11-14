# Server hardening: limiting what `boat_server` can do and find

Status, 2026-11-14: **H0 (systemd sandbox) is deployed on Winterfell; H1-H3 are merged; H4 code side is merged (operator work owed); H5 is implemented on `feat/h5-lockdown`; H6 is decided (do not split); H7 is next.**
Completed task: H5, `boat_server --lockdown kill|log|errno` (Landlock ABI 6, `no_new_privs`, hand-written seccomp allowlist); it fails closed. Evidence below.
Open: `davve-srv`: enable `--lockdown` in the unit and soak with `--lockdown log` to tune the allowlist; H4 operator work (nftables, crash alert); H1 credential unit; the WebRTC syscalls under lockdown are unverified;
the longer H3 fuzz campaign; relay join from outside the LAN.
Done when: every step's gate passes, the closing step has moved the contract into a spec, and
the remaining risk is written down as a decision, not an omission.
Next task: release the lockdown warm-up, confirm a clean `log` soak, switch the unit to `kill`; then H7 and the H4 operator work in `davve-srv`.

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
| H6 | **Decided 2026-11-14: do not split.** Decision only: privilege separation — a small network front that validates and forwards fixed-size messages to the authority process. Reasons and revisit triggers in [H6 decision](#h6-decision). | Decision and reasons recorded |
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

## H6 decision

2026-11-14: **do not split `boat_server` into a network front and an authority process.** This is a
design judgement, not a measurement; no split was prototyped.

Benefit of a split: a compromised front could only send well-formed fixed-size messages, so it could not
read or alter room state, spoof players or cheat. The authority could run with no network at all, the
exposed code (ENet, libjuice, libdatachannel, signalling) would shrink to a small auditable front, and a
front crash would not lose the room. It would also add a second barrier if the H5 allowlist had a hole.

Cost: a second binary, an IPC protocol with its own spec, tests and fuzzing, a second unit, lifecycle
and version-skew failure modes, an extra hop per packet, a clean split of join, token and name
handling, and a rework of how WebRTC sessions map to peers (the riskiest path). Releases and
`tools/cut_release.py` would ship two components.

Why not now: after H0, H2 and H5 an RCE in the network code lands in a process with no filesystem, no
`execve`, no new privileges and a short syscall list. What a split would still add is mostly the
integrity of game state and denial-of-service containment, which do not justify the added code and
operating surface for a playground. The front would stay exposed to denial of service either way.

**Revisit if:** the server holds persistent data, accounts or credentials worth stealing; a fuzz
campaign or advisory shows real memory bugs in the network parsers; or rooms and Lua plugins become
third-party supplied. A cheaper first step then is one binary run twice (`--role front` and
`--role authority`) over a socketpair.

Defers: H5 not yet soaked on Winterfell, the WebRTC syscalls under lockdown, and the longer H3 fuzz
campaign stay the evidence that would change this decision.

## H5 soak finding: remote candidate hostnames

First `--lockdown log` soak on Winterfell (v0.0.28, 2026-11-14): the kernel logged seven would-be
violations, all `openat` or `socket` from WebRTC threads. The `RTC resolver` thread is libdatachannel's
asynchronous lookup of an ICE candidate whose address is a hostname, which `getaddrinfo` then resolves
(resolver files, a DNS socket). Browsers send such names (mDNS `<uuid>.local`) and a hostile client
can send any name, so an unauthenticated offer could make the server perform DNS lookups. The
server now filters the SDP offer in `src/net/boat_net_sdp.c` and passes only candidates with a numeric
IP address (`boat_net_sdp` test, `fuzz_net_sdp` harness). Same-LAN browsers that only offered an mDNS
candidate use their server-reflexive or relay candidates instead. **Not yet verified on Winterfell:**
the filter needs a release build and deployment; the `RTC poll` `openat` seen at start-up and the
main-thread `socket` are still unexplained and may remain after the filter. Rerun the soak, then decide
the allowlist, before `kill`. The change to `boat_net_rtc.c` is compiled only in WebRTC builds and was
not compiled locally; CI and the release build are its first compile.

## H5 soak result and warm-up

Winterfell, 2026-11-14. v0.0.29 under `--lockdown log` still logged a few `openat`/`socket` calls.
`strace` on a scratch run of the same binary identified them as glibc first-use work: the timezone
file (log timestamps, from the WebRTC threads), `gai.conf` (read when a lookup returns several
addresses, as libjuice's wildcard bind does) and an `nscd` socket probe. None came from the game or
from client input. `boat_lockdown_apply` now warms those up before locking (`warm_libc`) and
`validate_boat_lockdown` has a case that performs them after a `kill` lockdown (passes on Linux,
GCC). A scratch build of that change served a native join and a browser join under `--lockdown kill`
on Winterfell with no `SIGSYS` and no audit records. The allowlist was not widened. Open: release the
warm-up, run `log` mode again to confirm a clean journal across several joins and a restart, then
switch the unit to `kill`; the `nscd` probe is only stopped by the warm-up, not by policy, so an
extended soak (hundreds of lookups) should confirm glibc does not retry it.
