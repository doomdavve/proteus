# Server security

How `boat_server` (the Wake Velocity room) limits what a compromised process can do, and what is
done to find bugs first. This describes the system as it stands; the work that produced it, with its
dated evidence and findings, is in [the hardening plan](SERVER_HARDENING_PLAN.md) (history).
Report vulnerabilities through [SECURITY.md](../SECURITY.md); do not record specific
vulnerabilities, crashing inputs or exploit notes in this repository.

## Threat model

`boat_server` is C, public, and reachable from the internet over UDP (ENet) and WebSocket
signalling (WebRTC through libdatachannel and libjuice). A memory-safety bug in anything that
parses network bytes is a potential remote code execution, and none can be ruled out. Two
directions follow: make exploitation unprofitable, and find bugs first.

Untrusted: ENet packets (`boat_net_codec`), WebRTC signalling and data channels, SDP offers,
join tokens and names. Operator-controlled: room packages, Lua plugins, assets and the command
line. No client-supplied bytes reach package, Lua or scenery parsing.

Not defended: denial of service beyond the limits below, the integrity of game state by a
compromised network parser (the server is one process, see [Decisions](#decisions)), the game
client and browser build, the hosted web server (nginx), and release signing.

## Layers

Winterfell runs Debian 13 (Linux 6.12). A hit in the network code lands in a process that passes
through each layer below; the operator-side units live in the private `davve-srv` repository.

| Layer | What it does | Where |
| --- | --- | --- |
| Build flags | Stack protector, `_FORTIFY_SOURCE=3`, PIE, full RELRO and `-z now`, stack-clash protection, CET, zero-initialised autos, for the server targets and bundled dependencies | `cmake/Hardening.cmake`; `tests/lint/check_binary_hardening.py` (`boat_server_hardening` on Linux) |
| Input limits | 8 peers, 6 per source address, a join token bucket (burst 8, 2 per second), oversize and empty message drops, per-peer rate limits, a 10 s handshake timeout, bounded WebRTC signalling | [Connection limits](MULTIPLAYER_PROTOCOL.md#connection-limits) |
| SDP filter | Only ICE candidates with a numeric IP address reach libdatachannel, so a client cannot make the server resolve names | `src/net/boat_net_sdp.c` |
| Lockdown | `no_new_privs`, Landlock (all filesystem rights, TCP bind and connect, abstract-socket and signal scopes) and a seccomp allowlist, applied after assets, package, plugins, sockets and the recording file are open | `src/room/boat_lockdown.[ch]`; [In-process lockdown](MULTIPLAYER_HOSTING.md#in-process-lockdown) |
| systemd sandbox | No capabilities, syscall filter, protected kernel and proc, private devices and users, W^X, memory and task caps; `systemd-analyze security` 1.1 for both units | `davve-srv`, `docs/wake-velocity.md#service-sandbox` |
| Credentials | The TURN password is read from a credential file (`--ice-server-file`, `LoadCredential=`), never the command line | `davve-srv` |
| Firewall | Per-source UDP limits in their own nftables table on 24680, 24682 and the relay range 49160-49200 (see below) | `davve-srv`, `docs/wake-velocity.md#per-source-udp-limits-nftables` |
| Exit alert | Mail to root on any abnormal room or relay exit (signal, exit status, seccomp lines), one per unit per 10 minutes | `davve-srv`, `service-exit-alert.sh` |

### Lockdown behaviour

`--lockdown MODE` is Linux only and off by default; Winterfell runs `kill`. It is fail closed: if
Landlock is below `BOAT_LOCKDOWN_MIN_LANDLOCK_ABI` (6), or `no_new_privs` or the filter cannot be
installed, the server prints the reason and exits 1 before serving. `log` is a tuning mode, `errno`
is for tests, and `--package-reload` is refused. Landlock does not cover UDP; the ENet socket is
bounded by the seccomp list and the firewall. Before locking, `warm_libc` performs glibc's lazy
file reads and address probing (timezone, `gai.conf`, the `nscd` probe) and sets
`mallopt(M_ARENA_MAX, 8)` so glibc never reads the CPU count afterwards. To tune the allowlist,
run `--lockdown log`, read the `SECCOMP` audit records (`journalctl -k`) and extend
`build_allowlist`. The code never lives in `proteus_boat_core` or `proteus_boat_net`.

### Firewall

The `wake_velocity` role loads `ip wake_velocity` through `wake-velocity-firewall.service`. It only
limits (no default drop, because the host serves other services), leaves the `iptables-nft`
tables alone and skips the LAN and VPN subnets. Per source address: 5 new flows per second
(burst 20), 24 concurrent flows, 800 packets per second (burst 1600). The values are guesses;
tune them from the drop counters. The port allowlist is the router's forwards, which are not
managed or inspected from this repository.

## Finding bugs first

- **Sanitizers.** `-DPROTEUS_SANITIZE=address,undefined`; the `linux-sanitize` workflow runs the
  `fuzz_*` and `boat_net*` tests on changes to the network, room, core and fuzz code.
- **Fuzzing.** `fuzz_net_decode`, `fuzz_net_server`, `fuzz_net_sdp` and `fuzz_session_record` in
  `tests/fuzz/` run as deterministic seeded mutation tests in routine CI and as libFuzzer
  harnesses with `-DPROTEUS_FUZZ=ON` (see [TESTING](TESTING.md#sanitizers-and-fuzzing)).
  Not fuzzed: signalling inside libdatachannel, and the operator-supplied package and Lua parsers.
- **Boundaries.** `boat_net_boundary` and the core boundary lint keep sockets and files out of
  the core and net libraries; only `boat_net_enet.c` uses ENet and sockets.

## Decisions

- **No privilege separation (H6).** `boat_server` is not split into a network front and an
  authority process. After the layers above, an RCE lands in a process with no filesystem,
  no `execve`, no new privileges and a short syscall list; a split would mostly add game-state
  integrity and DoS containment for a second binary, an IPC protocol and a harder release.
  Revisit if the server holds persistent data, accounts or credentials worth stealing; if a fuzz
  campaign or advisory shows real memory bugs in the parsers; or if rooms and Lua plugins become
  third-party supplied. The cheaper first step is one binary run twice over a socketpair.
- **Limits are guesses.** Every numeric limit above was chosen conservatively, not measured.
  Revisit with real join traffic.
- **Short evidence, accepted.** The fuzz runs (a ten-minute libFuzzer smoke run per harness) and
  the `kill` soak (about 25 minutes of mostly scripted joins plus operator play) are short. The
  risk is accepted: a rare syscall under real play ends the room, and systemd restarts it and
  mails the operator. Longer runs are welcome, not a gate.

## Known limits

- libjuice does not send a lifetime-0 TURN refresh when a peer closes, so each join holds a relay
  allocation for up to ten minutes. The relay quota is 24 against 41 ports, which makes
  exhaustion hard but does not remove it; the fix is a patch to or upgrade of the vendored
  libdatachannel. Restarting `wake-velocity-relay` releases the slots.
- The nftables limits have been deployed and loaded but not shown to drop anything: no test
  source outside the LAN and VPN has flooded the room.
- Relay joins from outside the LAN, WebRTC under a long lockdown soak, aarch64 lockdown, and the
  `_FORTIFY_SOURCE` check on a Release server binary are unverified.
- Per-source limits for WebRTC signalling live only in the firewall; the process cannot see the
  address.

## Operating

```sh
ssh winterfell 'systemd-analyze security wake-velocity wake-velocity-relay --no-pager'
ssh winterfell 'sudo nft list table ip wake_velocity'          # drop counters
ssh winterfell 'sudo journalctl -k | grep -i seccomp'          # lockdown violations
```

A `SIGSYS` exit means the allowlist is missing a syscall: reproduce under `--lockdown log` on a
scratch copy, then extend `build_allowlist`. Do not widen it blindly.
