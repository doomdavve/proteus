# Authority session recordings

`boat_server --record PATH` records a hosted room for headless fleet reconstruction and seek.
`wake_velocity --serve` accepts the same option. Recording is opt-in, creates a new file, and
refuses to overwrite an existing path. It works with free, race and deathmatch definitions;
private working-harbour authorities and `--scripted` do not use this path. A recording is a
playback artifact, not a room save or a resumable network authority.

```sh
./build/boat_server --location outer --package proteus.outer-hunt \
  --package-reload --record build/hunt.p5r
./build/validate_boat_session_record build/hunt.p5r
./build/validate_boat_session_record build/hunt.p5r --seek 600
./build/validate_boat_session_record build/hunt.p5r --diagnostic
```

Stop the server normally (Ctrl+C or its normal lifetime options) to finish the recording.
A write/create/close error makes the server exit unsuccessfully. A write error detected during
service closes the room; a crash, kill or buffered write failure can leave an incomplete file.
The validator requires a valid end marker and rejects incomplete files; it does not silently
salvage their prefix. There is no visual viewer or in-game replay menu for these files. The
existing solo `.run` recordings keep their own format and commands.

## Capture and reconstruction

The authority observes the exact selected `BoatInput` array immediately before fleet
integration, including repeated and neutral controls, and writes the fleet hash immediately
after integration. The ordinary reader runs native fleet physics and checks that hash before
applying any recorded mutation. A mismatch is an error, never an invitation to install a
replacement checkpoint.

Between ticks and after native rules, a changed fleet is recorded as an explicit replacement
checkpoint. This captures joins becoming ready, spawn/reset requests, leaves, race placement,
combat resets and other native rule effects, in their authority order. Race GO time accompanies
the fleet state and binds the authored storm and ferry clock. Room definitions own scene data.
The stream also retains ordered crew/mark events and mirrored `ObjectiveState` messages.
Event-queue overflow can suspend an objective between callbacks; a native objective checkpoint
preserves this transition even when recording ends before the next callback.
Transport queues, packet retransmissions, peer names, authority race/match internals and network
input queues are not restored; native rule results are applied from the recording.

Objective callbacks have before/after checkpoints. They contain committed state, RNG, timers,
HUD, commands, quarantine, queued events, crew presence, event sequence and pending reload
candidates. Definition callbacks have separate before/after records, including the callback's
new definition, because a definition commits after that tick's objective update and HUD poll. The running Lua source is embedded
with its hash, effective state version, migration flag and marks. Pending source candidates are
also retained, including candidates whose evaluation fails. The original manifest text and
filesystem paths are not needed for reconstruction.

Initial, ten-second and accepted source/definition revision keyframes hold the room definition,
fleet checkpoint, objective checkpoint, source and mirrored HUD. Definition, objective source
and mirrored HUD revisions are distinct counters. Sequential playback checks the reconstructed
fleet, objective digest, command batch, quarantine, source identity/revision and
HUD against each keyframe before accepting it. Accepted definition callbacks are represented
explicitly before their revision keyframe.

Ordinary reconstruction applies recorded objective results without evaluating Lua. The optional
`--diagnostic` mode evaluates the embedded pre-callback source and pending candidates, comparing
committed state, command batch, suspension, source revision and queued-event progress against
the recorded result. Operational timing diagnostics are not deterministic comparison values.
Diagnostic mode checks callbacks independently from their recorded preconditions; it does not
reconstruct transport delivery or resume the authority.

`--seek TICK` first validates the complete file, selects the latest keyframe at or before the
requested simulation tick, then reconstructs forward through that tick. This is a linear scan
with a bounded record buffer, without an external index. An out-of-range tick is rejected.
`--diagnostic --seek TICK` diagnoses the complete recording before seeking. Bit identity is
qualified on the recording build; cross-compiler/platform identity is not promised.

## Byte contract

The codec lives in `src/room/boat_session_record.[ch]`, with the objective wrapper codec in
`src/room/boat_room_record_objective.[ch]`. Both process bounded buffers without files or Lua;
the server and validation tool own file access. `proteus_boat_net` exposes read-only observation
callbacks and retains its existing boundary.

Format version 1 uses a 32-byte frame header:

| Offset | Encoding | Value |
| --- | --- | --- |
| 0 | 4 bytes | `P5GR` |
| 4 | u32 | Record kind |
| 8 | u32 | Payload byte count, at most 512 KiB |
| 12 | u32 | Increasing record sequence, starting at zero |
| 16 | u64 | Simulation tick |
| 24 | u64 | FNV-1a(payload) XOR FNV-1a(first 24 header bytes) |

Frame scalars and outer payload scalars are big endian. Embedded room/fleet/net message codecs
retain the multiplayer byte contract; the objective wrapper version 1 uses explicit little
endian fields and embeds the existing objective codec. C padding and pointers never enter the
file. The checksum detects accidental damage; it is not an authentication mechanism.

Record kinds are identity (1), keyframe (2), integrated inputs (3), step result (4), native
mutation (5), objective before/after (6/7), event or mirrored HUD (8), definition before/after
(9/10), native objective suspension (11), and end (12). Event payload tag 2 identifies a native rule event; tags 0/1 identify an
absent/present mirrored HUD. Sources and variable subpayloads carry explicit lengths. Inputs
preserve binary32 rate bits and weapon request serials.

The identity record pins recording version, network protocol, physics revision, tick rate,
compiled content digest, ambient-wave setting and descriptive source/compiler/architecture build
identity. Unsupported versions/content, unknown record types, nonfinite inputs, invalid nested
codecs, checksums, truncation, extra data, sequence gaps and a missing end marker are errors.
The build description is provenance; actual replay hashes enforce simulation agreement.
