# Room content loading and distribution

Status, 2026-10-30: **proposed architecture; no implementation.** The intended experience is
to install a compatible Wake Velocity client once, then join rooms whose maps and scenarios
are downloaded as needed. Engine, protocol and physics changes can still require a client
update. This plan owns content loading/distribution; the [multiplayer plan](MULTIPLAYER_PLAN.md)
owns sessions, the [protocol](MULTIPLAYER_PROTOCOL.md) owns wire formats, and the
[plugin plan](PLUGIN_PLAN.md#multiplayer-prediction-and-recording) owns authored gameplay.

Completed: overall scope, join lifecycle and staged direction documented. Next: when this
track is scheduled, select one existing map/scenario and refine the package and loading
contract with plugin P5. No implementation order is implied for other active work tracks.

## Starting point

The server already supplies room settings, live boats, wakes and match state, including a
checkpoint for late joiners. Clients supply the playable world's static definitions.
The handshake checks protocol, physics revision and a digest of every compiled location,
water geometry and shared gameplay recipes. A change to another location can therefore
reject a client even when the selected room's content is unchanged.

Visual scenery is already separate: `boat_scenery` loads binary packs at runtime, and those
packs are excluded from the multiplayer digest. Their water must match the compiled map.
The browser can fetch the outer scenery on demand from its hosted build, but that is not
yet arbitrary room-content discovery or distribution. Existing explicit world/resource
ownership and prediction are useful foundations, not a finished dynamic-map loader.
Plugin gameplay remains local; P5 currently starts with matching preinstalled packages.

## Content and compatibility

Keep the engine responsible for physics, collision queries, rendering, networking and the
supported native gameplay primitives. A room package supplies data that those primitives
already understand. Adding a new physics primitive still needs an engine update.

| Content | Proposed responsibility |
| --- | --- |
| Simulation definitions | Water/collision geometry, spawns, course, rules, contact envelopes and relevant actor/weather recipes; exact identity shared by authority and prediction |
| Required presentation | Scenery and other resources needed for a usable, coherent view of this room; available before entry |
| Optional presentation | Cosmetic alternatives that cannot change contacts or gameplay; defaults allowed where explicitly supported |
| Authored gameplay | Versioned descriptors and, later, restricted scripts under the plugin/P5 authority contract |
| Live state | Existing session checkpoints and updates, extended as necessary; not baked into a downloadable map package |

Start with one bounded package set for one room revision, with a manifest listing schema,
required engine capabilities, immutable object identities, byte sizes and required/optional
roles. Use a cryptographic content hash for downloaded objects; the existing FNV digest is
not a download-integrity contract. Names and release labels are for people, not identity.
Exact encoding, compression and size budgets are implementation decisions to measure later.

Separate engine compatibility from selected-room content identity. Check protocol/physics
and required capabilities first, then obtain the selected room's manifest. Only the required
simulation definitions must match for prediction; presentation compatibility follows the
manifest's declared requirements. This replaces the global compiled-bundle prerequisite
deliberately, through a protocol change, rather than disabling its safety check.

## Join and loading lifecycle

1. Discover the room and check engine compatibility. Obtain a bounded manifest identifying
   the active content revision and its download source.
2. Resolve packaged defaults and cached objects by identity. Show missing download size and
   progress, with cancellation and a clear error/retry path.
3. Fetch missing objects, verify size/hash, decode and validate into staged resources.
   Build collision/query caches outside the simulation tick. Invalid or incomplete content
   must never become the active world.
4. Bind the validated world to authority/client simulation and presentation atomically.
   Confirm readiness for that exact room revision before spawning or accepting inputs.
5. Install a fresh authoritative checkpoint for that revision and enter play. Avoid holding
   a gameplay slot throughout a long download; finalize admission after preparation and
   report normally if the room has filled meanwhile.

The server must validate and load its package before advertising a ready room. It only
needs simulation data and server-owned scenario execution, not graphics/audio assets.
Files, downloads and decoding remain outside `proteus_boat_core` and `proteus_boat_net`;
the application/resource layer supplies validated, explicitly owned data to them.

Pin manifest, resources and checkpoint to one revision. If the room changes while a player
downloads, recheck and prepare the current revision instead of joining a mixture. Start
with changes between rounds or room restarts: prepare replacement resources, coordinate
readiness, then commit the new revision together. Failed preparation retains the previous
working world or leaves the client outside the room. Reconnects and recordings must retain
enough content identity to obtain the same definitions or explain that they are unavailable.
Seamless mid-round map replacement is deferred.

## Delivery, storage and trust

Prefer HTTPS object delivery from the room host or a configured static host, keeping large
content transfers off the gameplay channels. Support both dedicated rooms and host-from-game
without requiring every player host to operate an HTTPS service: resolve the delivery or
bounded peer-transfer fallback before claiming automatic distribution for player-hosted rooms.
The exact transport is open; it must work with native and browser clients.

Cache verified objects by content hash and reuse bundled defaults through the same resolver.
Publish atomically, bound temporary storage and disk use, and evict unused objects without
removing an active session's resources. Native filesystem and browser storage can use
different adapters. Browser quotas, CORS, cancellation and unavailable persistent storage
need explicit behavior; neither client should download from inside a simulation callback.

Treat remote bytes as untrusted: bound manifests, object counts, decoded memory and geometry
complexity; reject unsafe paths, invalid references and unsupported formats before binding.
Constrain download origins/redirects and decompression work. Hash verification establishes
byte identity, not author trust or permission to execute scripts.

Begin with data-only/curated content. Do not automatically execute a downloaded Lua package
because a room names it. Script distribution needs its own trust policy and proven bounded,
isolated evaluation, following the plugin host's capabilities and authority/prediction
contract. Prefer authority-owned callbacks and validated replicated commands/state; clients
need the definitions and effects required for prediction and presentation, not necessarily
the server's source. Data loading and script execution should remain separate decisions.

## Proposed stages

| Stage | Outcome and gate |
| --- | --- |
| R1 — content contract | Choose one built-in map and proven scenario; define manifest, identities, budgets and engine/content compatibility alongside P5. P5 can retain its preinstalled-package first slice. |
| R2 — local loading | Load that map's simulation definitions and scenery through owned resources on server and client; bundled content uses the same path. Prove parity, repeated load/unload and failed-load recovery before networking downloads. |
| R3 — preinstalled room packages | Join with matching locally available room content; add revision-aware readiness, late join, reconnect, prediction and recording identity. Coordinate shared protocol work with P5 rather than creating a second plugin contract. |
| R4 — automatic distribution | Advertise manifests, fetch/cache missing data, show progress/cancellation and support native/browser joins. Qualify dedicated and player-hosted delivery separately. |
| R5 — revision and operational qualification | Stage round-boundary updates, preserve usable previous content on failure, publish immutable packages with hosting/releases, and qualify mixed-platform cache/storage/network failures. Extend script distribution only after its trust gate is resolved. |

Scenery-only fetching could be an earlier experiment, but does not complete R2/R3 or enable
a client to join an unknown playable map. Whole-room downloads are enough initially;
spatial streaming, mutable terrain, a public content marketplace, arbitrary native mods and
a general dependency resolver are outside this first system.

## Verification and decisions to refine

Each implementation stage needs deterministic tests for its contract and failure behavior.
Key gates are bundled-versus-loaded simulation parity; authority/prediction agreement;
missing, corrupt, oversized and incompatible objects; cancellation/retry; room revision
changes during download; late joins/reconnects; failed updates; bounded resource lifetime;
and native/browser interoperability. Test clocks remain virtual. Use separate CPU/GPU
captures for loaded scenery parity and manual joins for progress, responsiveness and recovery.

Before implementation, decide the first scenario, concrete formats/budgets, download policy,
player-hosted delivery, cache retention, revision/slot admission limits and which resources
are mandatory. Recheck map and audio redistribution licenses when publishing packages.
Script trust and recording-content retention remain explicit open questions. Do not promise
compatibility across engine updates merely because content can be downloaded.

Documentation verification, 2026-10-30, Apple Silicon/macOS: checked the baseline against
the current handshake, scenery loader and plugin P5 plan; all 368 local links/anchors across
the four affected documents resolve. Debug configure/build passed using the existing pinned
dependency sources (one upstream `stb_vorbis.c` warning). The full CTest preset passed all
321 enabled tests; 13 retired field tests remained disabled and GPU checks were off. The
initial sandboxed run failed socket checks and timed out two authoring checks; the full
rerun with local process/network access passed. No loading/distribution feature, manual
gameplay or user acceptance is claimed. Other platforms and browser builds were not run;
Markdown-only PRs are skipped by the native/browser CI path filters.
