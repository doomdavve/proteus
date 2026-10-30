# Winterfell internet room

The Winterfell deployment currently runs released **v0.0.22**, commit
`62a6ac16f31ae71472789e3c29d485f3572f630d`, with the optional WebRTC transport enabled. The
v0.0.21 evidence in the dated status section below is historical; the room has since been
upgraded to v0.0.22 with the same protocol v7 / physics 9.
It hosts one shared four-player river deathmatch room for native and browser players.
The deployed protocol-v7 [arsenal](DEATHMATCH_PLAN.md#arsenal-milestone--2026-10-27)
provides the water cannon (hold Space), stern wave grenade (E), torpedo (Q) and optional
rearview mirror (V). A round starts when two players join. There is no room token and
no player account requirement.

Source status, 2026-10-30: the repository is protocol v8 with compact combat pages. Winterfell
still runs the matching v0.0.22 native/browser pair at protocol v7; v8 requires a new release and
the routine cutover below. Do not join the hosted room with a v8 development client.

## Join

- Native: use v0.0.21, choose MULTIPLAYER and select WINTERFELL, or enter
  `wake.davve.net:24680`.
- Browser: open [Wake Velocity on Winterfell](https://wake.davve.net/) or
  [GitHub Pages](https://doomdavve.github.io/proteus/), press PLAY, choose MULTIPLAYER,
  then the selected **SERVER: WINTERFELL** row. No address entry is needed.
  URL / CONNECT remain available for manual joining.
- On the home LAN, the native address `10.0.21.3:24680` bypasses router hairpin NAT.

The native server and hosted browser client are built from the same v0.0.21 tag by the
`wake_velocity` role in `davve-srv`. The published Pages client is also v0.0.21 and uses
the same public directory and signaling endpoint. The directory reports protocol 7 / physics 9.
Both launchers include public STUN. The public `wake.davve.net` hostname avoids the VPN's
private `*.winterfell.davve.net` DNS override. Native v0.0.20 includes the connection fix;
the v0.0.18 connection limitation described in the historical notes no longer applies.

## Routine deployments (davve-srv)

Every release must include this hosted cutover after native publication and Pages succeed.
The [release command](RELEASES.md#release-command) automates the version-pin commit, pushes
the private hosting baseline and executes/verifies this routine role. Its preflight mode
checks prerequisites before publication. The manual steps below remain the fallback.
`davve-srv` owns normal deployment. Pin `wake_velocity_version` in
`ansible/host_vars/winterfell/main.yml`, commit and push, then run from that repository:

```sh
git add ansible/host_vars/winterfell/main.yml
git commit -m "Update Wake Velocity version"
git push
scripts/ansible-run.sh --limit winterfell --tags wake_velocity
```

The ordinary unfiltered wrapper also includes the game in the baseline. The tagged command
limits changes to this role. It builds native and browser artifacts on Winterfell, reads the
SDK version from the selected release, and caches the pinned SDK per version under `/srv`.
The controller needs no Proteus checkout, local Emscripten, release artifacts or extra-vars.
Both builds finish before service/nginx configuration is switched. Upgrades briefly disconnect
players; repeat deployments reuse the builds and leave a healthy room running.

The v0.0.21 cutover passed through that exact wrapper on 2026-10-28, followed by a complete
`changed=0` rerun. `davve-srv/docs/wake-velocity.md` owns operator commands and deployment
evidence; [current verification](#status-and-verification--2026-10-28) records the client checks
and their limits. Do not alternate the baseline role with the standalone Proteus playbook:
they manage the same service names and ports. The standalone commands below are historical
reference, not the routine workflow.

## Known-server directory

The public directory is a version-1 JSON document with a `servers` array (at most eight entries).
Each entry has `name` (up to 24 printable ASCII characters), `description` (up to 64),
`websocket` (a secure `wss://` URL up to 63 characters), `protocol` and `physics` integers.
`native` records a native HOST:PORT. Native clients fetch this same directory at startup and
when MULTIPLAYER opens; entries without native addresses are omitted. LAN discovery and manual
joining remain available. The listing is curated, not a live availability/player count.
A listed room with different protocol/handling metadata shows a version warning and cannot
be selected; this avoids offering the live handling-8 room to development handling-9 clients.

The browser and native client fetch `https://wake.davve.net/servers.json` at startup and refresh
it when MULTIPLAYER opens, without
blocking the game. Left/Right cycles multiple named servers; Enter/click joins the selected
one. Requests bypass the browser cache, time out after five seconds, and bound accepted JSON
to 16 KiB. If a refresh fails, an already loaded list remains usable. An initial failure or
empty list leaves manual joining available. Browser servers must be secure WebSocket endpoints;
URLs with credentials are rejected. The client still performs the normal server handshake.

The normal launcher uses `Module.boatServerListUrl` to query Winterfell from either the
Winterfell site or Pages. nginx allows anonymous cross-origin GETs for this public JSON only
and disables its cache. Deployment generates the room's protocol/physics metadata from the
checked-out server headers. After an upgrade, all clients see the new metadata on refresh;
an older or newer client remains blocked until its room version matches. A new Pages client
must not keep using a stale version entry bundled in its own build.

To change names, descriptions or add known servers, edit
`davve-srv/ansible/roles/wake_velocity/templates/servers.json.j2`
and rerun deployment; no Wasm rebuild is needed. `web/servers.json` remains a local fixture/
example directory. Custom launchers can use it by setting `boatServerListUrl: 'servers.json'`
or omitting that setting. No accounts or dynamic registry service is required.

## Router and network

Forward these ports to **10.0.21.3**, preserving the external and internal port numbers:

| Protocol | Ports | Purpose |
| --- | --- | --- |
| UDP | 24680 | Native ENet gameplay |
| TCP | 443 | Public Wake Velocity HTTPS and WebSocket signaling (router forwards to Winterfell TCP 24443) |
| TCP | 24443 | Legacy direct listener; optional compatibility forward |
| UDP | 49160–49200 | WebRTC relay allocations |

Do not forward TCP 24682 or UDP/TCP 3478. The former is the plain WebSocket upstream
behind nginx on the LAN address; the TURN allocation listener binds only to localhost. LAN discovery is disabled.
Winterfell's inspected host firewall had no INPUT filtering; preserve the rules for other
services. If an INPUT firewall is added, it must also allow the table above.

For EdgeRouter Lite / EdgeOS, run `show configuration commands | match port-forward`
first. These additive commands assume rule numbers 20, 21 and 22 are unused and reuse the
existing WAN/LAN forwarding configuration. Substitute unused numbers if needed.
Enabling auto-firewall lets EdgeOS maintain firewall openings for configured forwards.

```text
configure
set port-forward auto-firewall enable
set port-forward rule 20 description 'Wake Velocity native'
set port-forward rule 20 original-port 24680
set port-forward rule 20 protocol udp
set port-forward rule 20 forward-to address 10.0.21.3
set port-forward rule 20 forward-to port 24680
set port-forward rule 21 description 'Wake Velocity WebRTC relay'
set port-forward rule 21 original-port 49160-49200
set port-forward rule 21 protocol udp
set port-forward rule 21 forward-to address 10.0.21.3
set port-forward rule 21 forward-to port 49160-49200
set port-forward rule 22 description 'Wake Velocity public HTTPS'
set port-forward rule 22 original-port 443
set port-forward rule 22 protocol tcp
set port-forward rule 22 forward-to address 10.0.21.3
set port-forward rule 22 forward-to port 24443
compare
commit
save
exit
```

The [EdgeOS guide](https://dl.ui.com/guides/edgemax/EdgeOS_UG.pdf) documents ranges and
auto-firewall. To undo just these forwards, delete rules 20, 21 and 22 in configuration mode,
then commit/save. Preserve existing shared auto-firewall and interface settings.

Browser data uses WebRTC rather than ENet or the signaling WebSocket. The release cannot
constrain its direct ICE socket ports. A private coturn instance gives the authority public
relay candidates in the bounded range above; browser clients need no TURN credentials. Public STUN lets the browser advertise its
external address so the authority can establish relay permissions for it.
The server can also negotiate a direct LAN route. Coturn requires port-preserving forwards
for its [public/private address mapping](https://github.com/coturn/coturn/wiki/turnserver).
Clients on networks blocking UDP may still fail; no public TCP/TLS TURN fallback is provided.

The playbook discovers the public IPv4 address at deployment time and records it in the
relay config. After the ISP changes that address, update DNS as usual and rerun the playbook.
Check from a separate internet connection: an on-LAN public-address test depends on router
hairpin NAT and does not qualify a residential WAN path.

## Standalone controller deployment (legacy)

From the Proteus repository root, activate the pinned Emscripten SDK as described in
[the browser guide](WEB_BUILD.md), then build the released-core browser UI and deploy using
the existing controller inventory and vault file:

```sh
bash tools/build_winterfell_web.sh
ansible-playbook -i ../davve-srv/ansible/inventory.yml deploy/winterfell.yml \
  --vault-password-file ../davve-srv/ansible/.vault_pass.txt
```

This installs Debian build dependencies and coturn, checks out the pinned commit under
`/srv/wake-velocity/releases/`, and builds only `boat_server` and `boat_bot`. CMake still
requires desktop development libraries because this release has no server-only configure.
No display, audio device or game assets are needed at runtime. The optional libjuice build
needs `-DCMAKE_C_FLAGS=-D_DEFAULT_SOURCE` with the release's strict C17 on Debian 13.

The `wake-velocity` account builds and runs the authority; systemd makes its filesystem
read-only at runtime. A separate `turnserver` account runs `wake-velocity-relay` with eight
allocations at most. Both services start at boot and restart on failure. The package's unused
default `coturn` service is disabled. Logs go to the system journal. The persistent private
relay credential lives under `/etc/wake-velocity/`; it is not a player admission token.

The room binds specifically to `10.0.21.3`. Binding to all interfaces on this multihomed
host made ENet replies to VPN clients originate from `10.0.90.1`, which clients discarded.
nginx therefore proxies signaling to `10.0.21.3:24682` on the same machine.
The systemd address-family allowlist includes `AF_NETLINK` for Linux interface discovery;
without it, WebRTC fails to gather host candidates. Room and relay restart independently.

The playbook adds `/etc/nginx/conf.d/wake-velocity.conf`, validates nginx, then reloads it.
Its dedicated TLS listener serves only the game assets and `/room`, using Winterfell's
existing Let's Encrypt certificate. The baseline's infrastructure HTTPS site is unchanged;
public TCP 443 was refused during the external check and is not required for this room.
The game deployment remains a separate playbook; running the baseline does not deploy or
upgrade the game. Keep the existing certificate renewal setup operating.

`tools/build_winterfell_web.sh` exports the pinned release into ignored build storage and
copies only the title/browser UI, directory and web packaging files from this checkout.
It builds `build/winterfell-web/site` with the released handling-8 core. The ordinary current
branch browser build retains its current physics and is not a substitute for that bundle.
The playbook copies its assets and publishes the launcher last, without restarting the room.
It serves the bundle under `/srv/wake-velocity/browser/<commit>/site/`.

For the earlier unmodified release client, `-e wake_browser_site=` selects the checksum-pinned
Pages downloads in `deploy/browser-v0.0.17.json` instead. Old hashed files may disappear from
Pages, so keep cached copies for that fallback. The default locally built directory UI does
not depend on those downloads. No new GitHub release or Pages deployment is performed.

```sh
ssh winterfell 'systemctl is-active wake-velocity wake-velocity-relay'
ssh winterfell 'sudo journalctl -u wake-velocity -n 50 --no-pager'
ssh winterfell 'sudo journalctl -u wake-velocity-relay -n 50 --no-pager'
ssh winterfell 'sudo systemctl restart wake-velocity'
ssh winterfell 'sudo systemctl disable --now wake-velocity wake-velocity-relay'
```

Restarting disconnects all players and resets the round. Keep old release directories for
rollback; deploy the old browser/server pair together. The pinned source directory must
remain unmodified. No automatic upgrade, old-release cleanup or persistent state is configured.

## Cut over to the next release

For routine cutovers, use the version pin and `davve-srv` wrapper above. The following records
the earlier standalone procedure; the v0.0.18 baseline cutover is already complete.

The original hosting PR was based on protocol-v6 weapons-enabled main while the server
still ran v0.0.17 (protocol 5, handling 8). Publishing a newer browser/native release does not
upgrade that server automatically. Plan a brief room interruption and deploy matching server
and browser artifacts after the new release tag has been pushed. For example, using the next
unused version tag containing this PR:

```sh
release_version=v0.0.18
git fetch origin --tags
release_commit=$(git rev-parse "${release_version}^{commit}")
bash tools/build_winterfell_web.sh "$release_version"
ansible-playbook -i ../davve-srv/ansible/inventory.yml deploy/winterfell.yml \
  --vault-password-file ../davve-srv/ansible/.vault_pass.txt \
  -e "wake_version=$release_version" -e "wake_commit=$release_commit"
```

The recipe exports the selected commit and emits `build/winterfell-web/deployment.json` with
its commit/protocol/physics. Ansible rejects a browser bundle for a different server commit
before installation, and publishes directory metadata from the server headers. Protocol-v6
clients can then select the upgraded room on Winterfell or Pages. Update the playbook's
pinned defaults to the chosen release after cutover, so later routine deploys target it too.
Do not move an existing published tag. Verify native and browser joins, both weapons and
leave/rejoin; a passing deployment is not human weapon-balance acceptance.

For an isolated rehearsal build, `WAKE_WEB_SOURCE_DIR` and `WAKE_WEB_BUILD_DIR` select ignored
source/build locations. This does not deploy or publish a release. The default v0.0.17 build
backports only the UI and removes the newer outer-location title row unavailable in that core.

For a native connection check using a client built from the matching release:

```sh
./build/boat_bot --connect wake.davve.net:24680 --seconds 15 --predict
```

Use `10.0.21.3:24680` for the LAN check. A development build with newer handling may correctly
be rejected; use the release checkout's bot instead. Browser testing must reach the room,
receive snapshots and play alongside a native client, then leave and rejoin. Inspect the
selected ICE candidate pair to distinguish direct connectivity from relay verification.

## Status and verification — 2026-10-28

The release command in [the release guide](RELEASES.md#release-command) upgraded Winterfell
to v0.0.21 through committed/pushed `davve-srv` pin `e6d0871`. Native and browser builds
completed on Winterfell before the switch. The ordinary wrapper finished with
`ok=44 changed=9 failed=0 unreachable=0`; a full repeat returned
`ok=41 changed=0 failed=0 unreachable=0`. Room and relay are active; the room reports
v0.0.21, `NRestarts=0`, active since 2026-10-28 13:17:48 CET. The service-account checkout
identifies exact tagged commit `0cb1921224149ddeb4707e818dd2069096bc9e3d`.
Both live browser sites report v0.0.21 and passed Chrome/SwiftShader named joining/driving
without page errors. The public directory reports protocol 7 / physics 9. A matching native
bot received 150 snapshots, repaired eight mismatches, finished consistent and reported zero
decode errors. Android-emulated Pages touch/tilt/fullscreen/orientation checks passed;
iPhone-style Winterfell emulation also passed, including connected touch/tilt commands and
leaving. Representative desktop/mobile captures were inspected.
These are automated checks on this controller: physical phones, human gameplay/audio,
restrictive networks, long soaks and independent external-network/TURN qualification remain
open. Logs/captures are under the release worktree's `build/release/`; deployment logs are
`/private/tmp/proteus-release-v0.0.21.log` and `/private/tmp/proteus-v21-hosting-repeat.log`.
Next: physical-device and human package play, then broader multiplayer qualification.

### Earlier v0.0.20 cutover

The required v0.0.20 release cutover ran through `davve-srv` commit `2cf0974` using
`scripts/ansible-run.sh --limit winterfell --tags wake_velocity`. Ansible completed with
`failed=0`, then `changed=0` on a full repeat. The running executable resolves to
`/srv/wake-velocity/releases/v0.0.20/build-server/boat_server`; the source commit matches
`2f9adfc`. Room and relay are active; the room has `NRestarts=0` and its active timestamp
is 2026-10-28 11:11:51 CET. Public HTTPS metadata reports protocol 7 / physics 9.

Chrome/SwiftShader verified both the hosted browser and live GitHub Pages v0.0.20 clients:
named join, driving and weapon-key input, leaving and rejoining alongside a matching native
bot. Both directory/gameplay captures were inspected, and neither browser reported a
JavaScript page error. A separate native run completed 100 snapshots, all consistent,
with zero decode errors and a consistent final state. Test clients exited. These are automated transport,
input and rendering checks. Human gameplay/audio, weapon balance, restrictive-network and
long-session qualification remain open. This controller check does not requalify a separate
external network or prove TURN relay use. Next: human play and broader qualification.

## Historical status and verification — 2026-10-26


At that check, v0.0.18 was deployed by `davve-srv` main commit `dc206f9`, with protocol 6 / handling 9.
The native server and browser were built on Winterfell; both services are active. A complete
wrapper rerun reported no changes and preserved the room's active timestamp. A 90-second
matching native bot run ended consistent with zero decode errors; Chrome/SwiftShader named
joining, gun/grenade inputs and authoritative combat-page reception passed. Captures were
inspected. The SDK is cached, and all test clients exited. Next: human weapons play and wider
platform/soak checks. The following records are earlier standalone/development evidence.

PR preparation on the rebased weapons-enabled main: native Debug build and the routine
CTest suite passed all 98 enabled checks with optional GPU tests off. The full opt-in GPU
attempt passed the non-GPU checks but 25 native graphics cases could not initialize a monitor
(`GLFW: Failed to determine Monitor to center Window`); this desktop-session limit is not
reported as a graphics pass. Both current protocol-v6 and v0.0.17 browser builds passed.
The release recipe was also run against the weapons-core commit and emitted protocol 6 /
physics 9 metadata. Chrome verified deployed named joining, cross-origin directory fetching,
and the protocol-v6 client's version warning against the still-old room; those captures are
[the released menu](evidence/internet-hosting/browser-v17-servers.png) and
[the newer client guard](evidence/internet-hosting/browser-v6-incompatible.png).
Public JSON returned CORS `*` and `Cache-Control: no-store`. The coordinated release cutover
has not been run; no tag, Pages update or weapons-server upgrade was published.

Initial UI verification before rebasing onto weapons-enabled main:
`v0.0.17-server-directory` was deployed. Both current-tree and
released-core Emscripten MinSizeRel builds passed. The current native Debug build and all
123 enabled CTests passed (GPU checks enabled; 15 retired checks disabled). The Node
directory contract covers schema/URL bounds, version matching, coalesced refresh, updates,
cached-list failure and empty lists. Chrome/SwiftShader joined through the default named
row from a local served build and the deployed site without typing; a simulated directory
HTTP 503 still allowed manual joining. Local/server-list and fallback captures were inspected.
Evidence is in ignored `build/directory-*.log` and `build/directory-*-review/` files.
Deployment passed and its complete rerun reported `changed=0`; the room's active timestamp
stayed at 16:09:12 and `NRestarts=0`, confirming this UI deploy did not restart the server.
The new named-server flow awaits human review. Native directory fetching and a new Pages
publication were not part of this slice. Next: user review, then next-release Pages testing.

The following records the original room deployment and manual-address browser qualification
before the directory UI was added:

Implemented and deployed: v0.0.17 authority, private coturn relay, pinned browser client and
dedicated game TLS listener. The user confirmed adding the EdgeRouter UDP 24680,
UDP 49160–49200 and TCP 24443 forwards. No weapons patches or new release were published.

Verification actually performed on 2026-10-26:

- Apple Silicon/macOS: Debug configure/build passed. CTest passed all 122 enabled checks
  with `PROTEUS_GPU_TESTS=ON`; 15 retired wake-field checks remained disabled. Evidence:
  ignored `build/winterfell-ctest.log`.
- Debian 13/x86-64 Winterfell: released-source Release build with WebRTC passed; both
  services were active and enabled at boot. Core-service deployment was repeated with
  `changed=0` before adding the pinned browser site. Final site deployment passed with
  checksum validation and nginx validation/reload; its cache rerun was not yet checked.
- A matching v0.0.17 macOS bot joined over the VPN after the explicit bind fix, then via
  the router's public address. With the VPN disconnected, its route used `en0` and its
  public source address differed from Winterfell's: the separate-internet-path native
  check ran 90 seconds, ended consistent and reported zero decode errors.
- Chrome headless/SwiftShader opened the deployed game URL from that separate internet
  path, joined alongside a native bot, drove, selected LEAVE ROOM and rejoined. The browser
  reported no page errors; gameplay, menu and rejoined captures were inspected. The selected
  remote ICE candidate was the public **relay** on UDP 49180, verifying the bounded relay
  path rather than a direct VPN path. A simultaneous 90-second native run saw two players,
  ended consistent and reported zero decode errors. Evidence: ignored
  `build/winterfell-browser-external-final.log`, `build/winterfell-browser/` and
  `build/winterfell-mixed-external-reconnect.log`.
- Playbook syntax, changed local documentation links and both repositories' whitespace
  checks passed. Test clients exited; the room remains running. Earlier automation failures
  exposed missing public TLS forwarding and `AF_NETLINK`, and a test menu mismatch (the
  released menu has four rows); the final external check passed after these were corrected.

Next: human native/browser play, then Firefox/Safari, Windows and longer mixed/four-player
sessions with loss/jitter. No human gameplay/audio acceptance, restrictive-network TCP TURN
fallback, full M5/M6 soak or broader cross-platform qualification is claimed. Those gates
remain owned by the [multiplayer plan](MULTIPLAYER_PLAN.md).

Follow-up, same date: the user reported Chrome joining only over VPN. With the VPN off,
agent automation reproduced the exact 10-second join timeout from the GitHub Pages launcher;
the Winterfell launcher passed join/drive/leave/rejoin over public relay UDP 49168 on the same
machine and connection. After switching to the Winterfell launcher, the user confirmed
“works!”. This confirms the corrected browser connection workflow; extended gameplay and
audio acceptance remain separate.

## Native directory client, 2026-10-26

Historical implementation evidence: the v0.0.18 workaround and live-join limitation below
are superseded by the matched v0.0.20 deployment and native/browser checks above.

Follow-up, 2026-10-26: fixed the native game client checking an unset transport flag after
creating its ENet connection, which caused an empty `Cannot connect:` message for every
native join. The macOS Debug build and full preset passed (230 active tests); the loopback
host test now exercises the game client's token rejection. A direct connection to
`wake.davve.net:24680` reached the server but was rejected for a map/content mismatch
(both sides reported protocol 6 / physics 9). The digest includes all compiled maps, including
the outer map changed since the deployed v0.0.18 release, even though the room uses the river.
Protocol and physics numbers alone do not establish content compatibility. The client reports
the server and client protocol, physics revision and content hash together on a mismatch.
For the deployed room, build a client from v0.0.18 with the connection fix applied; the released
native client also contains that bug. Alternatively deploy a server built from the same source
as the fixed client. A compatible live build and manual gameplay
remain to be verified; this fix has not been deployed or packaged.

Native builds now fetch the public directory asynchronously and offer its compatible native
rooms in the SERVER row. Refresh failure preserves the last successful list for the current
process; manual joining and LAN discovery remain available. Hosted rooms are not registered
automatically: the directory is still maintained by the operator. The next qualification step
is a packaged Windows and macOS manual join against a compatible room. On 2026-10-26,
the macOS Debug build succeeded and the full CTest preset passed 230 active tests with
local socket access. The published JSON was retrieved and its native endpoint and revision
fields checked. Menu interaction, live joining, Windows/Linux builds and packaging have not
been manually verified. PR CI initially exposed Windows header collisions and unsupported C
atomics; the Windows worker now uses its thread handle for completion. Linux built and passed
CTest but its Wayland smoke check exceeded Weston’s 10-second startup deadline; the deadline
is now 60 seconds. On 2026-10-26 the revised PR CI passed the Windows, Linux and macOS
builds and their configured automated checks, including the Linux Wayland smoke and Windows
package check. Manual menu play and a live native room join remain pending.
