# Server hardening: limiting what `boat_server` can do and find

Status, 2026-11-14: **H0 (systemd sandbox) is deployed on Winterfell; H1–H7 are planned.**
Completed task: H0, both units sandboxed; `systemd-analyze security` 7.9 → 1.1 (evidence below).
Open: the relay-from-outside-the-LAN join and a soak under the sandbox were not exercised.
Done when: every step's gate passes, the closing step has moved the contract into a spec, and
the remaining risk is written down as a decision, not an omission.
Next task: **H1**, the TURN password out of the process command line.

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
signalling and data channels (libjuice, usrsctp/libdatachannel, `boat_net_*`), join
tokens and names. Operator-controlled: room packages, Lua plugins, assets, command line. H3
must confirm that no client-supplied bytes reach package, Lua or scenery parsing; if any do,
that path becomes a fuzz target and a P1 for hardening.

## Steps

| Step | Work | Gate |
| --- | --- | --- |
| H0 | Systemd sandbox for the room and relay units (`davve-srv`). **Done 2026-11-14.** | `systemd-analyze security` ≤ 2.0; native and browser join |
| H1 | Remove the TURN password from `ExecStart` (it shows in `ps` and `/proc/<pid>/cmdline`): the server reads it from a credential file or environment variable; `davve-srv` supplies it with `LoadCredential=`. Add `SECURITY.md` with the disclosure route. | Password absent from `ps`; room and browser join still work |
| H2 | Exploit-mitigation build flags for the server targets and their bundled dependencies: `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=3`, PIE, full RELRO and `-z now`, `-fstack-clash-protection`, `-fcf-protection`, `-ftrivial-auto-var-init=zero`. Check the built binary with `checksec`. | CI check on `boat_server`; no warning regressions; tests pass |
| H3 | Sanitizer and fuzz lane (absorbs the M5 "parser fuzzing" open loop). ASan/UBSan runs of the net and room tests and the `boat_bot` soak; libFuzzer/AFL++ harnesses for the packet codec, the ENet receive path, signalling messages, join/name/token handling and the session-record codec. Seed corpus from the recorded protocol tests. Tests keep the virtual clock. | Sanitizer lane green in CI budget; harnesses run N minutes locally without findings, or findings fixed privately |
| H4 | Server-side limits and network hygiene: peer, packet-size and join-rate caps; per-source rate limit and port allowlist in nftables on Winterfell; alert on `SIGSEGV`/`SIGSYS`/`EPERM` service exits. | Bot flood test stays within caps; firewall rules in `davve-srv`; alert fires on a deliberate crash |
| H5 | In-process lockdown, Linux only (`pledge` substitute), in `src/room/` behind `__linux__`; never in `proteus_boat_core` or `proteus_boat_net`. After assets, package, plugin load and socket binding: Landlock (no filesystem, only the allowed bind ports) then a seccomp allowlist of the steady-state syscalls. | Server runs a full soak; a test proves a denied `open`/`execve` fails; core boundary lint passes |
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

H0, 2026-11-14, Winterfell (Debian 13): both units 7.9 EXPOSED before, 1.1 OK after
`systemd-analyze security`; playbook ok=44, failed=0; native `boat_bot --hunt --predict` 8 s
join (0 decode errors, 0 dropped, consistent); browser join confirmed by the user. Not
exercised: relay join from outside the LAN, a long soak. Deployment record and the
filter-tuning procedure: `davve-srv/docs/wake-velocity.md#service-sandbox`
(private repository).
