# Multiplayer protocol v7

Written 2026-10-23 with multiplayer M3 (v1); v2, 2026-10-24, adds the
[deathmatch](DEATHMATCH_PLAN.md) room mode in Accept and the MatchState and MatchEvent messages. This document owns the byte layout, delivery classes,
validation rules and version policy of the boating network protocol. Task status, decisions
and acceptance stay in the [multiplayer plan](MULTIPLAYER_PLAN.md); the codec is
[`src/boat_net_protocol.[ch]`](../src/boat_net_protocol.h) and its fixtures are in
[`tools/validate_boat_net.c`](../tools/validate_boat_net.c).

v3 (2026-10-24) adds the authority-selected basin/Eriksberg/river location to Accept, widens
position validation to cover the mapped world, and fingerprints the complete compiled map
bundle for compatibility. A room hosts one fixed location and up to four players. M4 client
prediction uses only the fields listed here. The application protocol provides no encryption
or player authentication: a room token is a shared admission word. Native ENet sends these
bytes without encryption; the optional [WebRTC adapter](WEB_BUILD.md#webrtc-rooms) encrypts its
transport while retaining the same bytes and delivery classes. No protocol revision is needed.

v4 (2026-10-24) adds the [river race](RACE_PLAN.md) room mode (race R2): room mode 2 in Accept,
the grid hold as slot flag bit 2, the latest round's GO tick in every snapshot and checkpoint
(it sets the course ferry's clock and the tick holds release in prediction), the RaceState and
RaceEvent messages, and the race course in the content digest. Rebuild both server and clients;
v1–v3 are incompatible.

v5 (2026-10-25) removes the Eriksberg location: Accept's location byte is now 0 basin or
1 river (v3/v4 sent river as 2), and the content digest covers only those two locations and
the single river crop. Message layouts are otherwise unchanged; v1–v4 are incompatible.

A3.2 (2026-10-26) adds location 2, `outer` (the baked first outer-river slice), without a
version bump: Accept's location byte is valid up to 2 and the content digest now also covers
the outer definition and its mesh, so a build without it fails the digest check at Hello.
Race rooms still require the river.

A3.5e (2026-10-27) adds the harbour obstacles to the content digest, without a version bump:
the docked ferry joined the harbour contact, and moored hulls already block boats and
projectiles, so builds that differ there must not share a room.

Handling revision 9 (2026-10-26, isolated controls branch) increases commanded-thrust travel
to 4 units/s and removes steering-commanded aerial roll. Protocol v5 bytes are unchanged;
old handling revisions are rejected. Pedals/Shift become ordinary target-derived rate inputs,
and the menu/held shortcut use the existing reset request. Inactive authority input uses the
same shared command rate, preventing neutral overshoot. [Integration status](CONTROLS_PEDALS_HANDOFF.md).

v6 (2026-10-26) adds deathmatch combat. Commands gain held gun fire and a numbered grenade
request, slot blocks gain that request, checkpoints optionally append the complete enabled
combat state, and CombatPage (type 21) carries bounded periodic correction pages. MatchEvent
uses a formerly reserved byte for the defeat reason. Rules are fingerprinted in content
compatibility. Rebuild native and browser peers together; v1–v5 are incompatible.

v7 (2026-10-27) replaces gun gameplay with a water cannon, reverses the grenade, and adds
torpedoes, recoverable pressure and per-attacker damage bursts. The command/slot reserved byte
becomes a u8 torpedo serial. Combat checkpoint layout changes; MatchEvent adds cannon (5) and
torpedo (6) causes. Message/slot sizes stay unchanged. Rebuild all peers; v1–v6 are incompatible.
The cannon-arc follow-up preserves v7 byte layouts but fingerprints launch lift, gravity and
cube radius, and muzzle height/forward/side offsets as well as encoded rules; builds with different
cannon trajectories or launch attachments reject each other by
content digest. Projectile height is the committed current height, advanced ballistically for
cannon cubes; birth ticks supply vertical velocity, so no extra checkpoint fields are needed.

## Conventions

- Every multi-byte integer is **big-endian** (network order). `i8` is two's complement.
- `f32` is an IEEE-754 binary32 bit pattern sent as a `u32`. Builds whose `float` is not binary32
  fail to compile. Decoders reject NaN and infinities in every float field.
- Structs, padding, `bool`, native `long` and compiler enum representations are never sent.
  Booleans are `u8` 0 or 1 unless the combat section specifies `u32`; enums have documented widths.
- Reserved bytes are written as zero and **must** be zero on receipt.
- One message is one transport packet of at most **1100 bytes**, header included. The ENet MTU is
  1200 bytes of UDP payload, so a message never fragments.
- A decoder validates everything before it writes its output. It rejects a message for the
  wrong size, an unknown type, an invalid field, a non-finite float or a value outside the
  safety envelope below. The encoder runs the same decoder on its own output and refuses to
  emit anything that fails it.
- Strings are printable ASCII (0x20–0x7e). Hello strings are a `u8` length followed by that many
  bytes. Roster names are a `u8` length followed by 16 bytes, zero-padded.

## Header (32 bytes)

| Offset | Type | Field | Rule |
| ---: | --- | --- | --- |
| 0 | u32 | magic | `0x50424e54` ("PBNT") |
| 4 | u16 | version | 6. Any other value is a version mismatch (a Hello gets Reject `VERSION`) |
| 6 | u8 | type | 1–21, below |
| 7 | u8 | flags | 0 |
| 8 | u32 | epoch | Room session epoch from Accept; 0 in Hello |
| 12 | u32 | sequence | Per sender and channel, starting anywhere, wrapping modulo 2³² |
| 16 | u64 | tick | Authoritative tick the message refers to (see each type) |
| 24 | u16 | payload | Payload bytes, at most 1068; the packet must be exactly 32 + payload bytes |
| 26 | u16 | reserved | 0 |
| 28 | u32 | reserved | 0 |

Sequence comparison is modular half-range: `a` is newer than `b` when `(a - b) mod 2³²` is
between 1 and 2³¹ − 1. Receivers discard state-channel and birth-channel messages that are not
newer than the last one accepted. Messages whose epoch is not the room's are discarded, so
traffic from an earlier session never reaches a room.

## Channels

| Channel | ENet delivery | Carries |
| ---: | --- | --- |
| 0 control | reliable, ordered | Hello, Accept, Reject, Roster, Ready, SpawnRequest, ResyncRequest, Disconnect, MatchState, MatchEvent, RaceState, RaceEvent |
| 1 state | unreliable, sequenced | InputBatch (up), Snapshot (down), Ping, Pong |
| 2 births | unreliable, sequenced | WakeBirths |
| 3 registry | unreliable, unsequenced | CombatPage, tied to a fleet snapshot tick |
| 4 bootstrap | reliable, ordered | WorldBegin, WorldPage, WorldEnd |

A message on the wrong channel is discarded. Reliable bootstrap traffic cannot hold up new
snapshots, because they travel on a different channel.

Reliable ordering is per channel: WorldBegin/WorldPage/WorldEnd can arrive before Accept.
The client holds at most one maximum-sized checkpoint's wire messages (16 pages plus begin
and end) while awaiting Accept, then processes them using the accepted epoch. Other epochs
are discarded. This receiver buffering does not change the wire layout or channels.

## Messages

`C→S` is client to server. Every payload size is exact unless a count or string decides it.

| Type | Name | Dir | Header tick | Payload bytes |
| ---: | --- | --- | --- | ---: |
| 1 | Hello | C→S | 0 | 15 + three string lengths |
| 2 | Accept | S→C | room tick | 20 |
| 3 | Reject | S→C | room tick | 16 |
| 4 | Roster | S→C | room tick | 72 |
| 5 | WorldBegin | S→C | checkpoint tick | 28 |
| 6 | WorldPage | S→C | checkpoint tick | 8 + 1..1024 |
| 7 | WorldEnd | S→C | checkpoint tick | 8 |
| 8 | Ready | C→S | checkpoint tick | 8 |
| 9 | InputBatch | C→S | target tick of the newest command | 16 + 4 × count |
| 10 | Snapshot | S→C | post-tick n | 736 |
| 11 | WakeBirths | S→C | room tick | 4 + 40 × count |
| 12 | SpawnRequest | C→S | client's mirror tick | 8 |
| 13 | Ping | C→S | client's mirror tick | 8 |
| 14 | Pong | S→C | room tick | 8 |
| 15 | ResyncRequest | C→S | client's mirror tick | 12 |
| 16 | Disconnect | both | sender's tick | 4 |
| 17 | MatchState | S→C | room tick | 104 |
| 18 | MatchEvent | S→C | event tick | 16 |
| 19 | RaceState | S→C | room tick | 104 |
| 20 | RaceEvent | S→C | event tick | 16 |
| 21 | CombatPage | S→C | matching snapshot tick | 8 + 1024 (pages 0–2), 8 + 980 (page 3) |

### Hello, Accept, Reject

Hello: `u32` handling revision (`BOAT_PHYSICS_REVISION`, currently 9), `u64` content digest,
then the name (≤16), room token (≤32) and build label (≤32) strings.

The content digest is FNV-1a 64 over every compiled location in ID order: identity, mapped
flag, basin half-extents, single-player spawn, multiplayer start centre, current centre and
crossing offset; for each mapped location,
the crop's triangle count, then every triangle's three vertex indices and referenced XYZ
coordinates. It then hashes the fleet/wake contract: tick rate, slots, solver passes, spawn
retry, emitter slots, emission cadence, maximum lifetime, mass and restitution. Finally (v4) it
hashes the river race course: gate count; the start and every gate's centre, direction and
half-width; the four grid points; and the ferry source's origin, direction, start offset,
speed, height, duration, lifetime, wavelength, spread and half-width. The local-storm extension
also hashes storm centre X/Z at GO, radius, height, start offset, velocity X/Z (floats), then
the storm recipe revision (u64, currently 3: irregular directional crests), then the ferry
recipe revision (u64, currently 2: centred crest with a tapered three-wave packet). Last
(A3.5e) come the harbour obstacles: each moored berth's kind (u64), X, Z and heading; the docked
ferry's X, Z, heading, length, beam, draught and height; then the harbour contact recipe
revision (u64, currently 1). This changes
compatibility without changing message bytes;
rebuild server and clients together. Storm settings are compiled course data; GO anchors their
clock in both the authority and prediction. Cloud, rain, lightning and rough water move
from that shared origin at the authored velocity; no per-player weather position is sent.
`boat_net_content_digest()` defines the exact order. Fingerprinting the whole bundle lets a
client send Hello before it knows which location the server will select.

The server checks, in order: version (from the header), handling revision, content digest,
token, free slot. The first failure is answered with Reject and a disconnect, before any slot
or boat exists.

Accept: `u8` slot (0–3), `u8` waves (0/1), `u8` snapshot interval (4), `u8` birth interval (12),
`u8` input interval (2), `u8` commands per batch (12), `u8` future window (24), `u8` room mode
(0 free, 1 deathmatch, 2 race; v1 reserved; a race room's location must be the river),
`u16` tick rate (must be 120), `u8` location (0 basin, 1 river, 2 outer; v5), `u8` reserved,
`u32` wake lifetime ticks (240–1920),
`f32` crest height (0.1–3). The header epoch is the room's epoch.

Reject: `u8` reason, `u8` reserved, `u16` server protocol version, `u32` server handling
revision, `u64` server content digest. Reasons: 1 version, 2 handling revision, 3 content,
4 token, 5 room full, 6 malformed handshake.

### Roster

Four entries of 18 bytes, in slot order: `u8` state (0 empty, 1 joining, 2 playing), then the
fixed-width name. It is sent whenever a slot changes.

### World checkpoint

The bootstrap and resync checkpoint is a byte string of 5352 bytes when combat is disabled, or 9068 in deathmatch,
at most 16384, cut into
pages of 1024 bytes:

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | u64 | fleet tick (equals the wake pool tick) |
| 8 | u32 | next spawn-request number |
| 12 | u32 | wake lifetime ticks |
| 16 | f32 | crest height |
| 20 | u16 | live records (must equal the records with non-zero amplitude) |
| 22 | u16 | peak live records |
| 24 | u64 | births emitted |
| 32 | u64 | records expired |
| 40 | u64 | race GO tick (v4): the room's latest race round, 0 for none and in other modes |
| 48 | 4 × 174 | slot blocks (below) |
| 744 | 128 × 36 | wake records (below), in pool order |

WorldBegin: `u32` checkpoint ID, `u32` byte count, `u16` page count (must equal
`ceil(bytes / 1024)`), `u16` reserved, `u64` FNV-1a 64 of the checkpoint bytes, `u64`
`boat_fleet_hash()` of the encoded state. WorldPage: `u32` checkpoint ID, `u16` index, `u16`
count, then the page bytes. WorldEnd and Ready: `u32` checkpoint ID, `u32` reserved.

The client installs a checkpoint only when every page arrived, the byte digest matches, it
decodes and its fleet hash equals the sender's. It then answers Ready. A failed install asks for
a resync.

### Slot block (174 bytes)

Snapshots and checkpoints use the same block for each slot:

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | u8 | bit 0 active, bit 1 spawn pending, bit 2 held (v4: race grid or parked; only with bit 0); other bits 0 |
| 1 | u8 | motion state: 0 waterborne, 1 airborne, 2 landing, 3 capsized |
| 2 | u8 | input source on the latest tick: 0 none, 1 received, 2 repeated, 3 zero, 4 neutral |
| 3 | u8 | reserved |
| 4 | u32 | life (spawn generation) |
| 8 | u32 | spawn request number |
| 12 | u64 | tick of the pending spawn request (≤ header tick) |
| 20 | u64 | last tick whose received command was used (≤ header tick) |
| 28 | i8, i8 | steering and throttle rates used on the latest tick, × 127 (−128 invalid) |
| 30 | u8 | flags: bit 0 inactive, bit 1 held water cannon; others zero |
| 31 | u16 | grenade request serial; zero means none |
| 33 | u8 | torpedo request serial; zero means none |
| 34 | u32 | landings |
| 38 | 25 × f32 | position x, z; velocity x, z; heading; throttle; engine response; planing; height; vertical velocity; pitch; pitch velocity; roll; roll velocity; dry time; landing time; inverted-contact time; turn-roll load; wave spray; wheel; engine angle; yaw velocity; legacy trail time; legacy trail start x, z |
| 138 | 2 × f32 | emitter span start x, z |
| 146 | u64 | emitter span start tick (≤ header tick) |
| 154 | u64 | emitter last tracked tick (≤ header tick) |
| 162 | u8 | emitter cursor (0–31) |
| 163 | u8 | emitter initialized (0/1) |
| 164 | u16 | reserved |
| 166 | f32 | emitter strength |
| 170 | f32 | emitter strength sum |

### Wake record (36 bytes)

`f32` position x, z; `f32` direction x, z; `f32` length; `f32` amplitude; `f32` inverse speed;
`u64` birth tick (≤ header or checkpoint tick). Positions are simulation X/Z; the renderer's
reflected X never travels.

### Snapshot

Post-tick authoritative state, sent every fourth tick (30 Hz) to every accepted client: `u32`
next spawn-request number, `u16` live records, `u16` peak live, `u64` births emitted, `u64`
records expired, `u64` wake digest, `u64` race GO tick (v4, as in the checkpoint), then four
slot blocks: 768 bytes with the header (target ≤800). The wake digest is FNV-1a 64 over each live record's pool index and fields, in pool
order (`boat_net_wake_digest()`). The client compares it with the records it rebuilt. Wake
records themselves travel only in checkpoints and births.

### CombatPage and combat checkpoint

Enabled deathmatch combat appends **3716 bytes** to the checkpoint after its 128 wake records
(offset 5352; complete deathmatch checkpoint 9068 bytes). Disabled free/race checkpoints are
5352 bytes and omit this section. All words below are big-endian; flags use `u32` 0/1 in this
section, floats use binary32, and timestamps use `u64` ticks. The codec in
`boat_combat.c` is shared by checkpoints and fleet hashing and validates before committing.

| Section | Count × bytes | Fields in order |
| --- | --- | --- |
| Flags | 2 × 4 | enabled, launches allowed |
| Rules | 92 | eight f32: health, damage, bullet speed/range, lob distance, crest height/speed/range; seven u32: fire, heat, cool, recharge, splash, fuse, grace ticks; then five f32: cannon pressure, recovery/s, torpedo damage/speed/range; three u32: torpedo reload ticks, arming ticks, starting ammo |
| Players | 4 × 184 | f32 health/heat; u64 life start, next fire, next grenade, defeated tick, confirmed hit tick; u32 connection generation, consumed throw request, hit serial, lethal owner; u32 overheated; f32 pressure; u64 next torpedo, pressure tick; u32 torpedo request, ammo, pressure owner/generation; four target records, each f32 damage, u64 tick, u32 life/generation |
| Projectiles | 52 × 52 | 40 water packets, four grenades, then eight torpedoes: f32 position x/z, direction x/z, height; u64 birth/end ticks; u32 connection generation, life, owner, active |
| Waves | 4 × 44 | f32 centre x/z; u64 birth tick; u32 generation, life, owner; f32 height, speed, range; u32 active |

A projectile's active identity is owner/generation/life/birth. Grenades store the landing point;
presentation derives the flight from direction, lob distance, launch height and splash tick.
Each grenade index reserves the same wave index until both have expired. Cannon commands are
held. A nonzero grenade or torpedo request is consumed once per life, including rejected requests, so
redundancy, repetition and prediction replay cannot defer or repeat it. Pool exhaustion does
not consume the weapon's cooldown.

CombatPage (type 21, server to client, channel 3) uses the WorldPage payload: `u32` low bits of
snapshot tick, `u16` page index, `u16` count, then data. Count is exactly four. Pages 0–2 have
1024 data bytes, page 3 has 644; the largest whole packet is **1064 bytes**. Header tick is the
full matching fleet snapshot tick; page ID must equal its low 32 bits. Wrong counts, indices,
IDs or lengths are rejected. No transport fragmentation is needed.

Deathmatch clients retain eight bounded in-flight sets indexed by snapshot sequence. A fleet
snapshot and all four matching pages must arrive before correction. Older complete sets may
apply while a newer set is partial; applying a newer complete set discards older incomplete
sets. A lost page never blocks subsequent complete sets. Bootstrap/resync still uses the
reliable complete checkpoint and its byte/fleet digests. Prediction restores and replays the
whole combat state and physical waves; match scores/events remain authority-only.

At 30 Hz, the four combat pages add 116280 application bytes/s/client to the 23040-byte/s fleet
snapshots, before wakes/control/transport headers. This initial fixed-page design favors bounded
correction correctness over bandwidth optimization. Native and browser transports use the same
C codec and existing channel 3 delivery class.

### WakeBirths

Sent every twelfth tick (10 Hz): `u8` count (≤16), three reserved bytes, then per birth `u8`
pool slot (< 128; slot ÷ 32 is the emitter), three reserved bytes and a 36-byte record. Each
batch repeats every birth of the last 180 ticks (1.5 s), newest first when more than 16 would
fit. A birth replaces a pool slot only when it is newer than the record already there.
Records born after the client's newest snapshot wait until that tick's snapshot arrives. When
the digest mismatch outlasts one second, the client asks for a resync.

### InputBatch

`u32` life the commands steer, `u8` count (1–12, ≤ header tick), three reserved bytes, `u64`
newest snapshot tick the client has applied, then `count` commands for consecutive ticks ending
at the header tick: `i8` steering rate × 127, `i8` throttle rate × 127 (−128 invalid), `u8`
flags (bit 0 inactive, bit 1 held cannon), `u16` grenade request, `u8` torpedo request. Rates −1, 0 and 1 survive exactly.

The authority accepts a command for tick t only when the slot is active on that life,
fleet tick < t ≤ fleet tick + 24, and no command for t is stored yet. Stored commands are
immutable, so repeated copies in later batches cannot change them.

### Other control messages

- SpawnRequest: `u32` request ID, `u32` life to reset. It is honoured only while the slot is on
  that life (or not yet placed), so a repeated request cannot reset twice.
- Ping/Pong: `u64` client clock in microseconds, echoed unchanged.
- ResyncRequest: `u8` reason (1 wake digest mismatch, 2 other), three reserved, `u64` mirror tick.
- Disconnect: `u8` reason (1 quit, 2 timeout, 3 shutdown, 4 misbehaving), three reserved.

### MatchState and MatchEvent (deathmatch rooms)

Only a room whose Accept says deathmatch sends these; free rooms never do. The authority alone
decides credit and scores ([rules](DEATHMATCH_PLAN.md#rules)); clients display them and never
predict them.

MatchState is the whole scoreboard and round clock. The authority sends it to a joining peer
right after Roster (before the checkpoint, so a mid-round joiner sees the round at once) and to
every peer whenever any field changes: a phase change, a score, a join or a leave.

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | u8 | phase: 0 warmup, 1 playing, 2 intermission |
| 1 | u8 | reserved |
| 2 | u16 | automatic respawn delay, ticks |
| 4 | u16 | earliest R after a capsize, ticks (≤ respawn delay) |
| 6 | u16 | reserved |
| 8 | u32 | rounds started (> 0 unless warmup) |
| 12 | u32 | round length, ticks (> 0) |
| 16 | u64 | tick the phase ends; 0 exactly in warmup |
| 24 | 4 × 20 | rows in slot order: `u8` present (0/1), three reserved, `i32` score, `u32` kills, `u32` deaths, `u32` self-capsizes (≤ times capsized); an absent row is all zero |

During the intermission the rows are the finished round's result; the next round start resets
them.

MatchEvent: `u8` kind (0 capsize, 1 round start, 2 round end), `u8` victim slot, `u8` attacker
slot, `u8` cause (0 none, 1 contact, 2 wake, 3 legacy gun, 4 grenade, 5 cannon, 6 torpedo), `u8` flags (bit 0 bailout: R while upright after a
touch), `u8` defeat reason (0 capsize/bailout, 1 hull destruction), two reserved, `u32` victim life, `u32` round (> 0). A capsize names a victim < 4 and
either an attacker < 4 other than the victim with cause contact, wake, legacy gun, grenade, cannon or torpedo, or attacker 255 (a
self-capsize, −1) with cause none and no bailout flag. Round events carry 255 in both slots,
cause none, no flags and life 0. Within a tick, events precede the MatchState that counts them.

### RaceState and RaceEvent (race rooms)

Only a room whose Accept says race sends these. The authority alone decides gates, places,
recoveries and deadlines ([rules](RACE_PLAN.md#r1-race-core--2026-10-24)); clients display
them and never predict them. Clients do predict the grid hold and the course ferry, from the
slot flags and the race GO tick of snapshots and checkpoints: a replay from an authoritative
state before the GO tick releases held slots when it reaches that tick, as the authority does.

RaceState is the whole race phase, clock and every slot's row. The authority sends it to a
joining peer right after Roster (before the checkpoint) and to every peer whenever any field
changes: a phase, a gate, a place, a recovery, a parking, a join or a leave.

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | u8 | phase: 0 waiting, 1 grid, 2 countdown, 3 racing, 4 results |
| 1 | u8 | course: 0 river sprint (the only one) |
| 2 | u8 | places awarded (≤ 4; equals the finished rows) |
| 3 | u8 | reserved |
| 4 | u16 | automatic recovery delay after a capsize, ticks |
| 6 | u16 | earliest R after a capsize, ticks (≤ recovery delay) |
| 8 | u16 | finisher parking delay, ticks |
| 10 | u16 | reserved |
| 12 | u32 | rounds gridded (> 0 unless waiting before the first) |
| 16 | u64 | tick the grid, countdown or results phase ends; 0 exactly in waiting and racing |
| 24 | u64 | GO tick of the latest round; 0 exactly before the first round |
| 32 | u64 | racing deadline (the cap, or 15 s after the first finish); 0 exactly outside racing |
| 40 | 4 × 16 | rows in slot order (below) |

Row: `u8` flags (bit 0 a player occupies the slot; bit 1 the row belongs to the latest round's
entrant; bit 2 the present player is that entrant, which needs bits 0 and 1; bit 3 parked past
the finish, which needs bit 2 and a finish), `u8` status (0 running, 1 finished, 2 did not
finish), `u8` place (1–4 exactly when finished, ≤ places awarded; ties share a place), `u8`
gates cleared (≤ 8, the finish included), `u32` recoveries, `u64` finish time in 1/1024-tick
units after GO (0 unless finished). A row without bit 1 has no other fields. A leaver's row
keeps bit 1 and its result with bits 0 and 2 clear.

RaceEvent: `u8` kind (0 phase, 1 gate, 2 finish, 3 did not finish, 4 recovery, 5 abandoned,
6 parked), `u8` slot (255 for phase and abandon events, else < 4), `u8` value (the new phase
≤ 4, the gate index < 8, or the place 1–4; otherwise 0), `u8` reserved, `u32` round (> 0),
`u64` finish time in 1/1024-tick units (finish events only; 0 otherwise). Within a tick, events
precede the RaceState that counts them.

## LAN discovery

Added 2026-10-24 so the title can list rooms on the local network. Discovery is plain UDP on
its own port, **24681** (`BOAT_NET_DISCOVERY_PORT`, `boat_server --lan-port`), outside ENet and
the header above. It has its own format number (1), so the game protocol version can change
without breaking it, and every answer carries the room's protocol version, handling revision
and content digest. A searcher can therefore tell a room it cannot join before it tries.

A searcher sends a **query** to 127.0.0.1, 255.255.255.255 and the subnet broadcast address of
every interface that is up and supports broadcast, about once a second. A room answers each
valid query with one **answer**, sent directly to the query's source address and port. Both are
exactly 52 bytes, so an answer is never larger than the query that caused it. Rooms answer at
most four queries per service. Rooms bound to a loopback address listen for queries on that
address only. Other rooms listen on every interface and share the port (`SO_REUSEADDR`, plus
`SO_REUSEPORT` outside Linux), so several rooms on one machine all answer broadcasts.

Query (52 bytes):

| Offset | Type | Field | Rule |
| ---: | --- | --- | --- |
| 0 | u32 | magic | `0x50424c51` ("PBLQ") |
| 4 | u16 | format | 1 |
| 6 | u16 | version | The searcher's protocol version; informative, any value |
| 8 | 44 bytes | reserved | 0 |

Answer (52 bytes):

| Offset | Type | Field | Rule |
| ---: | --- | --- | --- |
| 0 | u32 | magic | `0x50424c52` ("PBLR") |
| 4 | u16 | format | 1 |
| 6 | u16 | version | The room's protocol version, any value |
| 8 | u32 | physics revision | The room's `BOAT_PHYSICS_REVISION` |
| 12 | u64 | content digest | As in Hello |
| 20 | u32 | epoch | Room session epoch; with port and name, identifies one room |
| 24 | u32 | address | IPv4 `a.b.c.d` as `a << 24 \| b << 16 \| c << 8 \| d`; 0 means "the sender's address" (a room bound to 0.0.0.0) |
| 28 | u16 | port | Game port, 1–65535 |
| 30 | u8 | mode | Room mode as in Accept (2 race since v4); not validated, since other versions may add modes |
| 31 | u8 | players | Connected players, at most max players |
| 32 | u8 | max players | `--players`, at least 1 |
| 33 | u8 | flags | Bit 0: the room needs a token; other bits 0 |
| 34 | u8 + 16 bytes | name | The host's name (the first label of its host name), as a Roster name |
| 51 | u8 | reserved | 0 |

Receivers drop datagrams of any other size, magic or format, and answers that fail a rule.
A searcher keeps the first address it heard for a room: on the host machine a room answers both
the loopback query and the broadcast. It forgets rooms that have been silent for 3.5 s. There
is no authentication. Any machine on the network can answer, and a room's name and counts are
only as trustworthy as that machine.

## Authority rules carried by the protocol

- **No client state.** Clients never send positions, impacts, launches, landings, capsizes or
  wake records. A client that sends a server-only type is counted, and after 16 malformed or
  forbidden messages it is disconnected.
- **Missing commands.** The authority repeats the last received command for up to 6 ticks, then
  uses zero rates (zero throttle rate holds the throttle). From 30 ticks (250 ms) without a
  command, or at once for a command flagged inactive, it eases toward neutral. Neutral means
  zero steering and a throttle rate toward idle at the ordinary adjustment speed. The boat
  stays physical.
- **Rates and bounds.** Per peer: 120 command packets/s with a burst of 30, 2 spawn requests/s
  and 1 resync/s. At most 128 messages are handled per service call. Each slot buffers 256
  commands by tick. The room allows four players and four handshakes, a handshake must finish
  within 10 s, and silence of 5 s disconnects a peer. ENet's maximum packet is 1100 bytes and
  its waiting data 64 KiB per peer.
- **Scheduling.** 120 Hz from a monotonic clock, with at most eight catch-up ticks per service
  call and never a larger step. A room more than 500 ms behind for a second closes.

## Safety envelope

These are generous decode limits, not gameplay limits. Positions (boats, emitter starts, wake
records) must be within ±8192 m on both horizontal axes. Shoreline and hull queries enforce
the selected map during simulation. Speeds are limited to ±1000 m/s, angular
rates to ±10000 rad/s, pitch, roll and heading to ±10⁶ rad, height to ±1000 m, and timers to
0–10⁹ s. Throttle, engine response and wheel must be within −1..1, planing and wave spray
within 0..1, and engine angle within ±π. Wake amplitude and emitter strength must be within
0–3.5, direction components within ±1.001, and length and inverse speed within 0–10⁴. Counts
must not exceed their capacities, and expired ≤ emitted.

## Version rules

- Any change to a byte layout, a message type, a channel or a rule above increments the
  protocol version. v7 rejects every other version, v1–v6 included; there is no negotiation or
  migration.
- A handling change increments `BOAT_PHYSICS_REVISION`; a change to any compiled location/shore geometry or the
  fleet/wake contract changes the content digest. Either is rejected at Hello.
- CombatPage uses channel 3. Additional registry formats, prediction acknowledgements or remote-input history require a later version.

## Fixtures

`validate_boat_net` pins:

- golden bytes of a Ping, an InputBatch, a Hello, a MatchState, a MatchEvent, a RaceState and
  a RaceEvent payload;
- the snapshot size (768), the race GO tick and held flag, slot field offsets and an FNV-1a 64
  digest of a hand-built snapshot's bytes (`7aa25c376fac8ec5`; v5 was `4de7f51f1c0586cc`, v4's was `9f816f6333e7646f`, v3's
  `690c711d99578e10`);
- round trips of all 20 types, rejection of every truncation, and a declared extra byte;
- header, field, non-finite and range faults, including the room mode, a race room away from
  the river and the MatchState, MatchEvent, RaceState and RaceEvent consistency rules;
- a deathmatch room on the in-memory transport whose clients, including a mid-round joiner,
  hold the authority's MatchState through capsizes, a round end and a leaver;
- a race room on the in-memory transport: predicting entrants held through the countdown and
  released at GO without a correction, every mirror and prediction running the ferry on the
  authority's clock, a mid-race joiner gridded next round, results on every client; and a
  prediction replay through GO from held states that matches the authority bit for bit;
- LAN discovery: the query's first 8 bytes, the answer's magic and bytes 20–51, every
  truncation, a query offered as an answer, invalid player counts and port, an answer from
  another version that decodes but is incompatible, and the search list's merge and expiry;
- checkpoints of busy four-boat fleets (calm and waves) that decode to the same fleet hash and
  race GO tick, re-encode byte for byte and resume 600 ticks bit-identically.

Any intentional layout change must update these fixtures, this document and the version.
