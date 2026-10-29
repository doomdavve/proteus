# Lua plugin API (voyage/race authoring and optional voyage objectives)

Status, 2026-10-29: **P0–P3 native descriptors/objectives and P4.1–P4.3 authoring tools implemented**.
Native tooling includes schema discovery, isolated validation/preview and live descriptor staging;
the browser supports source bundles for solo voyage descriptors.
A native solo session can take its outer voyage from a plugin (`--plugin`), reload it
when the file is saved or on F5, undo, and apply route or water changes with an explicit
restart. Rooms, the course and replays keep compiled content. The browser offers an opt-in
[descriptor-only import workflow](#browser-source-bundles-p43). The
[plugin plan](PLUGIN_PLAN.md) owns the staged roadmap; this guide owns the current contract.
[All plans](README.md).

## Runtime

| Item | Decision |
| --- | --- |
| Interpreter | Lua **5.4.9**, `https://www.lua.org/ftp/lua-5.4.9.tar.gz`, SHA-256 `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`, matching lua.org's published checksum. Fetched by CMake `FetchContent` with `URL_HASH`. |
| Why 5.4 | It is the mature release line. 5.5 (2025-12) is newer; moving to it is a deliberate later choice, not a patch update. |
| Compiled files | VM/compiler sources plus `lauxlib`, `lbaselib`, `lstrlib`, `ltablib`, `lmathlib` only (`proteus_lua`). `loadlib`, `liolib`, `loslib`, `ldblib`, `lcorolib`, `lutf8lib`, `linit` and the `lua`/`luac` programs are **not compiled**. |
| Configuration | Unmodified sources; the portable default `luaconf.h` (no platform `LUA_USE_*`). `cmake/proteus_lua.h` is force-included to fix the string-hash seed, so table traversal and diagnostics are identical from run to run. The project-wide `-ffp-contract=off` applies. |
| Web | The same sources compile with Emscripten 6.0.12; the validator passes under Node (see the [P0 result](PLUGIN_PLAN.md#p0-result-2026-10-28)). The browser links the shared descriptor transactions and evaluates imported source only in a disposable Web Worker; its [P4.3 subset](#browser-source-bundles-p43) is solo voyage descriptors. |
| License | Lua is MIT-licensed ("Copyright (C) 1994-2026 Lua.org, PUC-Rio"); the notice is at the end of `lua.h`. Configure extracts it to `build/lua-LICENSE.txt` and fails if it is missing. Native packages ship it as `lua-LICENSE.txt` beside the raylib/jsmn/ENet notices (`tools/macos/package.py`, `.github/workflows/windows-boat.yml`). The web package now ships `lua-LICENSE.txt` alongside its lazy Lua/Wasm descriptor worker (`tools/package_web.py`). |

## Package

One directory, one manifest, one text entry. The reference is
[`plugins/langedrag`](../plugins/langedrag/).

```text
plugins/langedrag/
  plugin.json
  main.lua
  README.md
```

`plugin.json` is a single strict JSON object of at most 4 KiB. Unknown or repeated members are
rejected. Strings are non-empty printable ASCII.

| Member | Accepted value |
| --- | --- |
| `id` | 1–63 characters of `a-z 0-9 . _ -`, starting with a letter, e.g. `proteus.langedrag` |
| `name` | 1–63 printable ASCII characters |
| `api` | `1` |
| `kind` | `"voyage"` or `"race"` |
| `entry` | `"main.lua"`: a plain file name in the package directory |
| `location` | `"outer"` for voyage; `"river"` for race |
| `capabilities` | `["voyage.define"]`, optionally followed by `"objective.hud"` and then `"objective.timers"`; races use `["race.define"]` |
| `state_version` | Objective packages: authored schema integer `1..65535`; descriptor-only packages: `1` |

The manifest requests capabilities and the host grants them. Limits are the host's, never the
package's.

## Entry text

`main.lua` is compiled as **text only** (`luaL_loadbufferx` mode `"t"`), so precompiled bytecode
is refused. It must return a table. A fresh Lua state evaluates it, and that state is closed
before the load returns. Nothing the script does persists except the returned values, which
are copied into native storage.

The environment keeps these globals, and everything else is removed:

- Base: `assert error ipairs next pairs rawequal rawget rawlen select tonumber tostring type
  _G _VERSION`. There is no `load`, `loadfile`, `dofile`, `require`, `print`, `pcall`,
  `xpcall`, `setmetatable`, `getmetatable`, `rawset`, `collectgarbage` or `warn`.
- `string`: `byte char format len lower rep reverse sub upper`. Method calls such as `s:sub(1)`
  work. There are no patterns (`find match gmatch gsub`), whose backtracking the instruction
  hook cannot bound, and no `dump`.
- `table`: `concat insert pack remove sort unpack`. There is no `move`, whose loop runs in C
  outside the hook.
- `math`: every function except `random` and `randomseed`.
- `io`, `os`, `debug`, `package`, `coroutine` and `utf8` do not exist.

Calling a removed function is an ordinary runtime error on its line.

## Voyage schema

Coordinates are simulation metres of the selected map: **x = −east, z = north**. Headings are
radians, with forward `(sin(heading), −cos(heading))`. Every value is checked; nothing is
clamped.

```lua
return {
  voyage = {
    shelters = {         -- 2 to 4, a list numbered from 1
      { id = "dock", name = "DOCK", x = -12, z = 60, heading = 0, radius = 60 },
      -- ...
      { id = "langedrag", name = "LANGEDRAG", x = 3600, z = -3370, heading = 1.57, radius = 0 }
    },
    arrival = { x = 3595, z = -3370, radius = 75, max_speed = 6 },
    exposure = { x = 4300, z = -2650, radius = 700, height = 1.6, build = 60 },
    exposed_fraction = 0.8,
    weather_range = 2500
  }
}
```

| Field | Type and bound | Meaning |
| --- | --- | --- |
| `shelters[i].id` | 1–23 of `a-z 0-9 _`, starting with a letter; unique | Stable authored identity for diagnostics and later migration (sidecar, not in `BoatVoyageRoute`) |
| `shelters[i].name` | 1–23 of `A-Z 0-9`, space, `.`, `-`, `'`; no leading or trailing space | HUD spelling |
| `shelters[i].x`, `.z` | finite, \|v\| ≤ 20 000 m | Recovery position |
| `shelters[i].heading` | finite, \|h\| ≤ 2π | Recovery heading |
| `shelters[i].radius` | (0, 1000] m; the **last must be 0** | Passing within it reaches the shelter. The first shelter is the start; the last belongs to the destination and is reached only by arriving. |
| `arrival.x`, `.z` | finite, \|v\| ≤ 20 000 m | Arrival area centre; the last shelter must lie inside it |
| `arrival.radius` | [1, 1000] m | |
| `arrival.max_speed` | [0.5, 50] m/s | Arrive at or below this ground speed |
| `exposure.x`, `.z` | finite, \|v\| ≤ 20 000 m | Stationary rough-water centre |
| `exposure.radius` | [0, 5000] m; 0 disables it | |
| `exposure.height` | [0, 5] m | |
| `exposure.build` | [0, 600] s | Growth time once the weather starts |
| `exposed_fraction` | [0.05, 1] | Inside this fraction of the radius counts as exposed. No shelter may lie there. |
| `weather_range` | [0, 20 000] m; 0 means grown from the start | The patch grows once the boat comes this close |

Every table must contain exactly these fields; strings must be strings and numbers numbers,
without coercion. Values are read with raw access, so no metamethod runs, and only the schema's
own levels are walked. Lua doubles are narrowed to `float` exactly as C narrows a literal: the
reference decodes bit for bit to `boat_voyage_outer`. The decoder sets exposure velocity and
`started` to zero, so P0 supports only a stationary patch.

Staging also checks a decoded definition against the session's location
(`boat_plugin_check_location`): the manifest's location must be the session's, the first
shelter must be the location's spawn, every shelter must be open water with 8 m clear and the
arrival centre with 20 m clear (`boat_map_water` on the compiled water). These errors carry
the field path, such as `voyage.shelters[2]`, and no line.

## River race authoring (P2)

The second reference package is [`plugins/river-sprint`](../plugins/river-sprint/). Its
`main.lua` returns `{ race = { start, gates, grid, source, storm, rules } }`; see the
[complete example](../plugins/river-sprint/main.lua). All fields are required. Records reject
unknown keys; lists must be dense. This uses the same restricted Lua evaluator, limits,
diagnostics, save poller and revision/Undo host as the voyage.

| Record | Fields and units |
| --- | --- |
| `start`, each `gates` entry | `x`, `z`, `dx`, `dz`, `half_width`; simulation metres, approximately unit travel direction (length within 0.01 of 1), width 4–500 m |
| `gates` | 1–8 ordered entries; final entry is the finish |
| `grid` | Exactly four `{ x, z }` positions, behind the start and at least 13 m apart |
| `source` | `x`, `z`, `dx`, `dz`, `started`, `speed`, `height`, `duration`, `lifetime`, `wavelength`, `spread`, `half_width`; the existing native ferry wake recipe |
| `storm` | `x`, `z`, `vx`, `vz`, `radius`, `height`, `started`, `build`; metres, m/s and seconds |
| `rules` | `grid_ticks`, `countdown_ticks`, `results_ticks`, `after_winner_ticks`, `cap_ticks`, `recovery_ticks`, `skip_ticks`, `park_ticks`, `min_players` |

Coordinates are finite within ±20000 m. Gate directions are normalized by `boat_race_init`,
as for compiled courses. Source direction must satisfy the native squared-length tolerance
of 0.0001; it is not silently normalized. Ferry speed is 0.1–30 m/s, height 0.001–5 m,
duration/lifetime 1–60 s, wavelength 2–40 m, spread 1–10 m/s, half-width 2–80 m.
Source/storm `started` is within ±600 s relative to GO. Storm velocity components are within
±50 m/s, radius 0–5000 m, height 0–5 m and build 0–600 s. Zero storm radius or height disables
it. Rules are integer 120 Hz ticks, at most 432000 (one hour); countdown, cap and
after-winner window must be positive, `skip_ticks <= recovery_ticks`, and `min_players` is 1–4.

Staging validates the selected river map: gate openings, forward nondegenerate segments
(20–5000 m), centre and outer approach lanes sampled at no more than 5 m, grid water and hull
clearance, and the full ferry path's corner clearance sampled at no more than 5 m. Every
anchor must have a usable candidate from the native recovery/finish-parking recipe. These
are conservative authored-route checks, not a proof that every possible driving line or
moving fleet will always have an immediately available recovery point.

```sh
./build/boat_race_trial --plugin plugins/river-sprint --play
./build/boat_race_trial --plugin plugins/river-sprint --renderer cpu --output build/race.png --time 18
./build/validate_boat_plugin_race
./build/validate_boat_race_plugin
```

The trial is a local course/source preview, with scripted steering unless `--play` is set.
It displays authored gates, ferry and storm. It spawns at the first `grid` position,
facing the start direction. Edit that position and restart with F7 to offset the boat
without moving the start-line buoy row. It has no native round countdown, standings or
results HUD; the headless fixture exercises the rules, crossings, recovery, deadlines and
four-boat races through the real C engine. `--plugin` is refused with historical `--check`
and `--gpu-check` modes. A voyage package belongs in `wake_velocity`, not this trial.

Saves settle through P1's injected-time poller. F5 stages explicitly; F6 undoes or discards;
F7 commits gameplay edits and restarts the preview's boat, clock, water and wakes. Only a
manifest `name` change applies immediately without resetting progress. Other gameplay edits
remain held. Incompatible identities require relaunch. Undo uses the same rules and does
not modify files. Synchronous staging discards simulation delta rather than catching up.
The trial panel shows active/staged revision and errors; stderr retains the full diagnostic.

`BoatPluginRace` owns a pointer-free `BoatRaceCourse`, `BoatRaceRules`, manifest and gameplay
digest. `boat_plugin_load_race` writes output only on success; `boat_plugin_race_check_location`
checks a decoded descriptor against the map. The shared `BoatPluginHost` selects its kind at
startup and retains eight prior revisions. Race renames compare gameplay digests; gameplay
changes classify as `BOAT_PLUGIN_RESTART_SESSION`. The digest folds explicit native fields as
little-endian IEEE double values (float fields promoted exactly), excluding manifest labels
and padding. `BoatViewState.race.course` is an optional frame-borrowed rendering override;
NULL retains the compiled river course. No plugin dependency enters core or net.

Production network rooms and browser clients remain compiled-content modes. P2 introduces no
wire-format, replay-format or browser-import changes; P5 owns network content compatibility.

## Playing and live authoring (P1)

Native builds only. From the repository root:

```sh
./build/wake_velocity --plugin plugins/langedrag
```

`--plugin DIR` selects the manifest's location (`outer`), so `--location` may be omitted; a
different `--location` is refused. It cannot be combined with `--connect`, `--host`, `--course`,
`--replay` or `--ghost`. Other solo options (`--start`, `--demo`, `--screenshot`, skins,
renderers) work as before. If the package does not load at launch, the game prints the
diagnostic and exits. The reference package is equal to the compiled route, so play is
unchanged until it is edited. Packaged builds ship it at `assets/plugins/langedrag` (macOS:
`Wake Velocity.app/Contents/Resources/assets/plugins/langedrag`); copy it somewhere writable
and pass that directory.

A small panel at the bottom left shows the active revision and the latest outcome; each
outcome is also printed to stderr as `Plugin DIR: ...`.

| Input | Effect |
| --- | --- |
| Save the files | Polled every 0.25 s. Once the package's bytes have stayed unchanged for 0.3 s they are staged once, so an editor's several writes (or a manifest and entry saved one after the other) become one candidate. The browser does not poll. |
| F5 | Read and stage the package now |
| F6 | Cancel in-flight main-game staging and discard held output; otherwise discard a held candidate or hold the previous revision (Undo) |
| F7 | Apply a held migration or native restart |

Staging never changes the running voyage. A candidate is compared with the active revision
(`boat_plugin_classify`):

| Change | Policy |
| --- | --- |
| Identical descriptor values (comments/layout ignored for descriptor-only packages) | Nothing happens; the panel shows UNCHANGED |
| Behavior bytes, objective schema or timer grant change, with unchanged native gameplay and `migrate` in the candidate | Held for F7 transactional migration; native progress kept; comments/layout count for behavior packages |
| Behavior edit without candidate `migrate`, or adding/removing `objective.hud` | Held for F7, restarting native voyage and objective together |
| Display names only: manifest `name`, or shelter `name` in descriptor-only packages; ordered shelter IDs and every number identical | Applied at once. The boat, clock, water, wakes and all voyage progress are kept; the HUD shows the new names |
| Shelter IDs, order, count, position, heading or radius; arrival; `exposed_fraction` | Held until F7, then **restart voyage**: progress, timing and recoveries begin again at the start shelter; the boat, clock, water and any weather already met continue. A boat already inside a shelter or the arrival area is observed by the ordinary rules on the next tick |
| `exposure` or `weather_range` | Held until F7, then **restart session**: the same full restart as Backspace without a voyage (tick zero, the boat at the spawn, calm water until the weather begins) with the new revision |
| Manifest `id`, `kind`, `location` or `api` | Not applied; relaunch to use another plugin |

A failed stage (I/O, manifest, syntax, runtime, memory, budget, schema or map check) keeps the
active revision and drops any held candidate; the panel shows the file, line when Lua gave
one, field path and message. Revisions are numbered R1 (launch), R2, ... in the order they are
staged; a held candidate staged again unchanged keeps its number. Undo keeps the eight
previous revisions. It reinstalls a revision through the same rules and does not rewind the
boat or the clock; a rename is undone at once, a route or water change is held for F7. A new
revision after an Undo discards the undone ones (no Redo). Progress is never inferred from an
old shelter index: every restart begins at shelter 0.

## Native interface

`src/boat_plugin.h` works on immutable bytes and does no I/O. `src/boat_plugin_io.h` acquires package files;
`src/boat_plugin_stage.[ch]` owns the private temporary-stream/worker transport.

```c
BoatPluginFiles *files = malloc(sizeof(*files)); // About 68 KiB of bounded buffers.
BoatPluginDiagnostic d;
BoatPluginVoyage voyage; // Last good definition; untouched by a failed load.
if (boat_plugin_read("plugins/langedrag", NULL, files, &d) == BOAT_PLUGIN_OK) {
    BoatPluginBundle bundle = boat_plugin_bundle(files);
    boat_plugin_load_voyage(&bundle, NULL, &voyage, &d);
}
```

- `boat_plugin_load_voyage` writes `out` only on success, so a failure leaves the caller's
  last good value intact. `BoatPluginVoyage` owns its strings: `route.shelters[i].name` points
  into it. Copy it with `boat_plugin_voyage_copy`, which rebinds those pointers, never by plain
  assignment.
- `boat_plugin_read` opens `plugin.json`, checks it, then opens its entry. On POSIX each file
  is opened with `O_NOFOLLOW` and must `fstat` as a regular file; on Windows reparse points and
  directories are refused. Reads stop at the limit, so an oversized file is detected by one
  extra byte, not a size query.
- `boat_plugin_route_digest` is FNV-1a over every route field with field names, float bits
  least significant byte first, and shelter names. The reference digest is `e8509272172cb7c4`,
  the same for the compiled route and on Wasm.

### Session seam and host (P1)

`BoatSession.voyage_route` is the route that init and restarts begin: the definition's compiled
`boat_voyage_outer` unless the application replaces it. The global location record is never
modified. `boat_session_rebind_voyage` swaps the borrowed route and keeps progress (renames);
`boat_session_restart_voyage` begins a route at the current time and keeps the weather
(`boat_voyage_restart`); a session restart begins whatever `voyage_route` names.

`src/boat_plugin_host.h` owns one plugin's revisions and does no I/O and reads no clock:

```c
BoatPluginHost host; // Static or long-lived: the session borrows its active route.
boat_plugin_host_start(&host, location, &resources->map, &bundle); // Revision 1, or false.
boat_session_restart_voyage(&session, &boat_plugin_host_active(&host)->voyage.route);
// Later, between ticks:
if (boat_plugin_host_stage(&host, &candidate) && boat_plugin_host_held(&host)) {
    // host.change says the policy; commit when it allows (renames) or the player asks.
    const BoatPluginRevision *r = boat_plugin_host_commit(&host);
    boat_session_rebind_voyage(&session, &r->voyage.route); // Or restart, per host.change.
}
```

Revisions live in fixed slots and never move. A commit installs the held slot and the caller
rebinds every borrower at once; a slot is reused only by a later stage, after the session has
been rebound away from it. A commit is refused if the revision it was staged against is no
longer active. `BoatPluginWatch` is the save poller's state machine; the application passes
the time (`GetTime()` in the game, a virtual clock in tests) and keys each read with
`boat_plugin_read_key`. The game's glue is `tools/wake_velocity_plugin.[ch]`.

### Limits

| Limit | Default (`boat_plugin_default_limits`) | Notes |
| --- | --- | --- |
| Manifest | 4 KiB | Also the hard capacity |
| Entry text | 64 KiB | Also the hard capacity |
| Lua heap | 2 MiB | Counting allocator, compiler and libraries included. Exceeding it is `BOAT_PLUGIN_MEMORY`. |
| Entry instructions | 1 000 000 | Count hook every 100 VM instructions while the entry runs. Exceeding it is `BOAT_PLUGIN_BUDGET`, deterministically and without a clock. After exhaustion every instruction fails, so nothing can resume. |
| Init/reset/update callback instructions | 100 000 | Separate allowance after entry evaluation; also capped by native `limits.instructions`. |
| Migration callback instructions | 500 000 | Separate allowance after entry evaluation; also capped by native `limits.instructions`. |

The instruction budget covers Lua execution only. Compiling is bounded by text size and heap,
and the remaining library calls are linear in heap-bounded data. This is containment for
trusted local edits, not an OS security boundary.

### Diagnostics

Each call fills a `BoatPluginDiagnostic`:

- `status`: `limit io manifest syntax runtime memory budget schema`, from
  `boat_plugin_status_name`.
- `file` and `line`: the line comes from Lua's own position for syntax, runtime and budget
  errors. It is 0 when unknown, which is always the case for schema errors.
- `field`: a path such as `voyage.shelters[3].heading`, or a manifest member such as
  `capabilities[1]`.
- `message`: the reason, e.g. `is 30000; it must be from -20000 to 20000 m`.
- `cost`: `instructions` (completed 100-instruction steps), `peak_bytes`, `leaked_bytes` (always
  zero) and `fields` read by the decoder.

Validation stops at the first failure, and the order is deterministic.

## Commands

```sh
./build/validate_boat_plugin                       # every case, from the repository root
./build/validate_boat_plugin --case reference_matches_compiled
./build/validate_boat_plugin_session                # P1 staging, policies, Undo, polling, parity
ctest --test-dir build -R boat_plugin --output-on-failure
./build/wake_velocity --renderer cpu --plugin plugins/langedrag --time 3 --screenshot build/p.png
```

Web preflight, using emsdk at `../emsdk` (README web section):

```sh
../emsdk/upstream/emscripten/emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-web --target validate_boat_plugin
node build-web/validate_boat_plugin.js
```

## Objective transactions (P3.1 / P3.2)

[`boat_plugin_state.h`](../src/boat_plugin_state.h) exposes the transaction operations.
P3.2 grants opt-in `objective.hud` for native solo voyages; race packages remain descriptors.
The [P3.2 result](PLUGIN_PLAN.md#p32-result-2026-10-29) owns current integration status and limits.
Run the deterministic native checks with:

```sh
cmake --build build --target validate_boat_plugin_state
./build/validate_boat_plugin_state
ctest --test-dir build -R '^boat_plugin_state\.' --output-on-failure
```

The harness accepts bounded immutable source returning `init(context)`, `update(context,state)`
and, when requested, `migrate(context,state)`. Each returns **state, commands**; both are
required. A fresh Lua instance loads the source for every call, so hidden globals/closures do
not persist. The caller retains accepted source, the native checkpoint and pending event batch.
The native host supplies a nonzero RNG seed and generation before initialization. Update ticks
increase on multiples of 12; migration/reset are explicit operations at a nondecreasing tick.

```lua
return {
  init = function(context)
    return { visits = 0 }, {}
  end,
  update = function(context, state)
    for _, event in ipairs(context.events) do
      if event.kind == "voyage.shelter_reached" then
        state.visits = state.visits + 1
      end
    end
    return state, {
      { kind = "hud.set", generation = context.generation,
        id = "visits", label = "STOPS", value = state.visits }
    }
  end,
  migrate = function(context, state)
    state.version = 2
    return state, {}
  end
}
```

`context` holds integer `tick`, integer host `generation`, ordered `events` and `random()`.
Events have integer `tick`/`sequence` and bounded `kind`/`id` strings. The example above consumes
synthetic events in the isolated validator; the live adapter below supplies native events.
`random()` returns
the next unsigned 32-bit xorshift32 result represented exactly as a Lua integer. Failed batches
discard draws along with state, HUD changes and event acknowledgment. This is deterministic
scenario randomness, not a cryptographic stream. The optional timer grant is described below.

State supports booleans, signed 64-bit integers, byte strings, dense arrays and string-keyed
records. Even integral Lua floats such as `1.0` are rejected; use `1`. Empty tables encode as
records. Record keys sort lexicographically by bytes; graph aliases/cycles and metatables are
forbidden. Canonical bytes use one-byte tags `f`, `t`, `i`, `s`, `a`, `r`, little-endian 64-bit
integer bits, 16-bit string/container lengths, and one-byte record-key lengths. Records store
sorted key bytes followed by values; arrays store values in order. Limits: 16 KiB total,
16 nesting levels below the root, 256 tables, 256 entries per table, 1024 bytes per string and
1–63 non-NUL bytes per key. The codec is experimental, not a persisted release save format.

The HUD command is `hud.set`: exact fields `kind`, `generation`, `id`, `label`, `value`, with
1–23 printable ASCII characters in ID/label and an integer value. All commands are staged
before commit. Duplicate IDs within one resource kind in a batch are conflicts, and eight rows/32 commands are
host limits. Rows persist until overwritten, removed or reset. The live adapter has one plugin
owner and native generation; a voyage restart retires all its rows. P3.3b adds explicit removal
and bounded native composition; see the [resource contract](#live-migration-and-review-p33b).

A callback, budget, schema, event overflow or update scheduling failure quarantines updates
without changing the committed checkpoint. Explicit reset or a successful migration clears
quarantine. A failed candidate migration preserves the complete current checkpoint. Source,
state and error costs retain the 64 KiB source and 2 MiB heap ceilings; callbacks may request
lower native test limits, never raise them. Entry evaluation has its own 1 M instruction
allowance; init/reset/update callbacks have 100,000 and migration has 500,000. Each callback
allowance is additionally capped by the supplied native `limits.instructions`. Entry work
cannot consume the callback allowance or borrow from it. One 2 MiB heap covers both phases.
These limits do not bound compiler or C-library wall time. See the
[runtime qualification](#native-runtime-qualification-p34) for measurements and staging policy.

`boat_plugin_objective_digest` hashes state, HUD rows, RNG, acknowledgment, tick, generation
and initialization field-by-field; the timer grant adds a tagged timer-count/ID/deadline extension,
including an empty timer list when granted. Timer-free package hashes remain unchanged.
It excludes quarantine and C padding. Native checkpoint
assignment supports an **isolated harness** restore/replay. It must not be used to restore old
script state beside a world that kept advancing. The [P3.3b adapter](#live-migration-and-review-p33b)
now provides schema-aware migration, complete compatible solo review and developer inspection.

### Live optional voyage objective (P3.2)

```sh
./build/wake_velocity --plugin plugins/shelter-tour
```

The source-tree example uses the compiled Långedrag route and adds an optional tour of
`bridge` and `east_shore` before arrival. The dock and destination do not count. Repeated
visits and recovery do not count twice. The row shows `SHELTERS / 2: 0..2`, then
`TOUR COMPLETE: 2` or `TOUR MISSED: 0..1` at arrival. Arrival stays completeable independently.
The standard packaged `langedrag` example remains a descriptor without callbacks.

Request `["voyage.define", "objective.hud"]` in `plugin.json`. Return `voyage`, `init` and
`update` from the same `main.lua`; both callbacks are required and must be functions. Without
the capability, callbacks are rejected. The source executes afresh for every callback and
therefore reconstructs the descriptor too, within the entry budget before the separate
callback budget. Init is probed
while staging, then runs at the actual native restart tick with a fresh nonzero generation
and RNG seed 12345. A candidate can explicitly migrate current state with the [P3.3b contract](#live-migration-and-review-p33b).

Every native observation exposes a bounded ordered output list, retaining the old latest-event
field for presentation. The adapter copies all notices, including several from the same tick,
into a queue with stable authored shelter IDs. Kinds are `voyage.shelter_reached`,
`voyage.weather`, `voyage.exposed`, `voyage.sheltered`, `voyage.arrived` and `voyage.recovered`.
Each has the exact native integer tick and a strictly increasing per-run sequence starting at
1. Initial dock occupancy is not a visit; initial already-grown weather is not a weather-start
event. Recovery is observed immediately at the current tick and never synthesizes a visit.

After native tick T, when T is divisible by 12 and later than init/the previous update, a
callback sees the queue through T. Successful state, RNG, timers, acknowledgment and HUD output stay
staged until immediately before physics tick T+1. A recovery issued after dispatch at T
stays queued for the next callback; committing T's output retires only acknowledged notices. Rendering between T and T+1 sees the previous
committed HUD. The native panel renders the bounded rows above the instruments. Failures
retain committed state/HUD/timers and unacknowledged events, show `OBJECTIVE SUSPENDED`, and stop
callbacks; native physics, weather, recovery and arrival continue. A full 64-event queue
suspends explicitly instead of overwriting an event or retrying endlessly.

Accepted revisions own their source bytes, including Undo history. Manifest-only renames keep
objective state and pending output. Behavior and schema edits use the [migration/restart
policy](#live-migration-and-review-p33b). Route gameplay changes still restart the voyage;
water edits restart the session. F6 Undo applies old code to current state under that same
policy. A new voyage/session clears queued events, pending output, quarantine, timers and rows.
A rejected candidate leaves active source, state, generation and route intact. Browser objectives are explicitly unsupported; room
behavior remains unqualified. Trusted native descriptor staging now uses the [P4.2 live
worker](#live-descriptor-staging-p42); startup, updates and Apply-time init/migration remain
synchronous under the [P3.4 qualification](#native-runtime-qualification-p34).
This API is experimental.

### Deterministic timers (P3.3a)

Voyages may request exactly `["voyage.define", "objective.hud", "objective.timers"]`.
The host grants the last capability only alongside the objective callbacks. Timer commands
without it fail the whole transaction. Existing examples do not request it.

| Command | Exact fields beyond `kind`, `generation`, `id` | Behavior |
| --- | --- | --- |
| `timer.set` | `tick`: signed 64-bit integer strictly greater than `context.tick` | Create or replace the local timer with an absolute deadline in 120 Hz simulation ticks |
| `timer.cancel` | None | Remove the timer; an absent ID succeeds without effect |

IDs use 1–23 printable ASCII characters. A host generation owns at most 16 timers; HUD rows
and timers have separate ID namespaces. The combined command list remains capped at 32.
Commands stage in list order: cancel an existing timer before adding a new one when full.
Writing one timer ID twice in a batch (including set followed by cancel) is a conflict.
No wall clock or floating-point deadline participates; scripts should avoid overflowing integer
arithmetic when constructing deadlines. The host rejects deadlines at/before the current tick.

With the grant, `context.timers` is a dense list of due `{id, tick}` records sorted by deadline,
then bytewise ID. `tick` is the authored deadline, even when it falls between callbacks or
several update boundaries were skipped. Delivery occurs on the first successful update at/after
that tick. These records are separate from `context.events`: native event sequence numbers and
acknowledgment are unchanged. Init, reset and migration receive an empty timer list.

Due timers leave the staged checkpoint before commands execute, allowing an update to re-arm
the same ID with a later deadline. Delivery and timer commands commit atomically with state,
RNG, native acknowledgment and HUD output; any failure retains all committed timers. In the
live adapter this includes T/T+1 publication and cancellation of pending output on overflow.
Quarantine stops updates without consuming due timers. A successful isolated migration retains
pending timers unless its commands explicitly replace/cancel them; overdue timers deliver on
the next successful update. A failed migration retains the complete checkpoint.

For example, keep the `voyage` descriptor and add callbacks to the returned table:

```lua
init = function(c)
  return { reminders = 0 }, {
    { kind = "timer.set", generation = c.generation, id = "reminder", tick = c.tick + 1200 }
  }
end,
update = function(c, s)
  s.reminders = s.reminders + #c.timers
  return s, {
    { kind = "hud.set", generation = c.generation,
      id = "reminders", label = "REMINDERS", value = s.reminders }
  }
end
```

A manifest-only display rename preserves pending timers and staged output. Explicit restart
retires every old timer and pending delivery before init at the current native tick with a fresh
host generation. Compatible migration retains timers unless canceled or their grant is removed;
its exact boundary contract follows below. Timers are private to each objective owner and do not
share a global timer namespace. This is caller-owned native data, not a persisted/network format.

### Live migration and review (P3.3b)

An objective package may return `migrate(context, state)` alongside `voyage`, `init` and
`update`. Non-function migrations and migrations without init/update are rejected. Init is
still probed under the candidate's schema when staging. `context.state_version` is the target
manifest schema; migration additionally receives `context.previous_state_version`. Both are
exact integers. Versions describe authored meaning, not interpreter versions. Equal versions
do not silently copy edited behavior: a candidate migration must explicitly accept it.

When native route gameplay is unchanged, a behavior/schema/timer-grant edit with a candidate
migration is held for **F7 MIGRATE STATE**. It runs against a copy of the last committed
objective, at the current native tick with a new generation. It receives no voyage events or
due timers. The queued notices remain unacknowledged for the next update, including notices
used by a pending old-code update. On success, source/schema/state/RNG/resources/generation
swap together, pending old-code output is retired, quarantine clears, and the native boat,
clock, voyage, presentation, water and wakes continue. No old staged command publishes later.
Failure retains all of those values, including quarantine, queued events and pending output;
the candidate stays held for retry or F6 discard. The panel and developer trace report failure.

Retained HUD rows and timers belong to the new generation after migration. Use
`{kind="hud.remove", generation=c.generation, id="old-row"}` to retire a row and
`timer.cancel` to retire a timer. Removal is idempotent. HUD removal accepts exactly `kind`,
`generation`, `id`; set/remove of the same ID in one batch conflicts. Missing IDs in an update
are retained rather than implicitly removed. Removing the timer grant retires all its timers
as part of the candidate transaction. A later invalid command rolls back retirement too.
Adding/removing the entire objective, editing native route gameplay, or omitting `migrate`
uses the explicit native restart policy. To select reset instead of migration, omit the
candidate migration callback. Undo uses the destination revision's migration when available;
otherwise it resets current native voyage/objective state. It never installs historical state
beside the moving world.

HUD composition uses `(owner plugin ID, local ID)` in caller-selected owner order, at most
eight owners, with an explicit destination capacity. Duplicate owner IDs or capacity failure
leave the destination untouched. Identical local IDs in different owners do not conflict.
The native game still hosts one voyage plugin: opening a second owner on an already owned
session is rejected before touching either owner. This composition primitive does not enable
multiple live voyage owners or shared weather/music ownership.

The developer-only review command accepts a package and optional absolute checkpoint/seek
ticks (default 120/360, range 0..72000, checkpoint no later than seek):

```sh
./build/boat_plugin_review plugins/shelter-tour 120 360
./build/validate_boat_plugin_session --case preview_seek
```

The calm solo fixture includes player and authored traffic wakes. It applies throttle for the
first 120 ticks and alternates steering every 240 ticks. It saves a complete compatible native
checkpoint, restores it, then replays the remaining authored inputs without polling saves or
waiting for time. Inspection prints revision/schema/generation, canonical state bytes in hex,
RNG/acknowledgment, pending-output digest, HUD/timers, queued events and the last 16 transitions
with before/after canonical hashes and accepted command intents. Failed transitions contain no
accepted commands. Command provenance is inspection data and excluded from the objective digest.

`wake_plugin_preview_save/restore/seek` expose the same harness contract to native callers.
The checkpoint owns all revision source/route strings, objective and pending output, event
queue, diagnostics/trace, native boat/presentation/voyage/clock/water history and both wake
pools. Restore rebinds internal pointers; immutable map resources, handling and directory stay
borrowed and must outlive it. Review inputs also support recovery before a step. Unsupported
recordings/course playback, other physical boats, combat/radial sources and legacy wake-field endpoints are rejected;
this is a solo review checkpoint, not a whole application save, recording format or a room
rewind. Camera/window/audio/effect playback is outside this display-free harness. Allocate
these large bounded checkpoint/revision histories off the stack. Interactive seek controls,
source-edit timelines and persisted replay provenance remain future authoring work.

### Native runtime qualification (P3.4)

`boat_plugin_bench` measures the trusted native authoring path without rendering:

```sh
./build/boat_plugin_bench plugins/shelter-tour
./build/boat_plugin_review plugins/shelter-tour 120 360
```

The benchmark takes 100 synchronous samples per case and reports elapsed UTC milliseconds
(p50/p95/maximum), last-call status, Lua peak, entry/callback instructions and live bytes
remaining after close. It exercises reference/read-and-stage reload, padded and dense 64 KiB
sources, compiler rejection, callback/migration/entry exhaustion, heap rejection and a
successful expensive entry plus callback. It requires an objective package and 128 bytes of
source headroom for generated variants. It does not edit the package. Run it without other
heavy workloads; UTC clock adjustments can distort measurements. This is an explicit experiment,
not a CTest timing assertion or an enforced deadline.

`BoatPluginCost.instructions` sums the entry and callback phases. Their separate counters
are hook quanta of 100, with less than one additional quantum of uncounted work per successful
phase. The existing exhaustion hook rejects after the allowance (at most 100 extra counted
instructions). Descriptor loads use only the entry counter. Host staging aggregates descriptor
and init-probe instruction/field counts and the maximum of their sequential Lua peaks, including
when location validation fails. Every VM closes on success or failure.

`boat_plugin_host_memory` reports the fixed native host reservation, reachable revision count
and retained source payload. History and a distinct held candidate count once; held Undo is
already in history. At most nine committed revisions plus one held candidate retain source:
589,824 and 655,360 bytes respectively at full capacity. Discard/eviction makes payload
unreachable; stale bytes still occupy fixed slots and remain included in the reservation.
Payload is a subset of the reservation, so adding the two would double count it.

Native inspection and the benchmark report the complete `WakePlugin` reservation (all ten
revision slots, files, state/pending output, events, diagnostics and trace) plus the single
allowed Lua heap as `owned_ceiling`. A compile-time assertion keeps the native owner within
1 MiB; Lua adds at most 2 MiB. This is owned storage, excluding allocator metadata, C stack,
borrowed directory/map/world/view resources and process RSS. Stack scratch includes candidate
revisions, transaction copies and recursive state decoding; the validators also run under a
1 MiB stack limit. Preview checkpoints own another full plugin/session/wake copy; their
`sizeof` is reported separately and each caller-requested checkpoint adds that amount.
The game has no retained VM per revision. The [P4.1 validator](#native-authoring-tools-p41)
and [P4.2 live staging](#live-descriptor-staging-p42) each use a separate disposable worker
whose world, Lua heap and process overhead are outside the parent owner reservation.

**Historical P3.4 staging policy:** trusted native edits staged synchronously outside the fixed
step, with simulation delta during staging discarded. P4.2 replaces descriptor staging in the
main native game with the [live worker](#live-descriptor-staging-p42); the race trial, native
startup and objective transitions keep their synchronous execution contract.
The local experiment targets 100 ms per stage and one 8.33 ms native tick for a reference
update, as review thresholds rather than promises or runtime deadlines. The measured sample
set meets those thresholds; expensive permitted module evaluation can still hitch a tick.
This qualifies the historical local workflow, not uninterrupted background execution. P4.2
adds disposable-process descriptor staging, but arbitrary imports still require isolation of
startup/updates/Apply-time evaluation and target-specific deadline qualification. An instruction hook
cannot cancel compilation or a C library call. Native Windows/Linux timing, browser/worker
costs, human migration/Undo/recovery acceptance and an exhaustive hostile-input bound remain
unqualified. See the [measured result](PLUGIN_PLAN.md#p34-result-2026-10-29).


## Native authoring tools (P4.1)

Build normally, then run from the source root (Python 3.10+):

```sh
python3 tools/plugin_author.py capabilities
python3 tools/plugin_author.py schema
python3 tools/plugin_author.py export plugins/langedrag > build/langedrag.wake-plugin.json
python3 tools/plugin_author.py preview plugins/shelter-tour --ticks 360 --base-revision 3
python3 tools/plugin_author.py preview plugins/river-sprint --ticks 600
python3 tools/plugin_author.py validate plugins/langedrag
python3 tools/plugin_author.py validate plugins/shelter-tour --base-revision 3 --timeout 5
python3 tools/plugin_author.py validate plugins/river-sprint
```

`--worker PATH` selects another native build's `boat_plugin_worker` executable (Windows defaults
to `build/boat_plugin_worker.exe`). These are development tools; release packages do not yet
include them. `capabilities` returns JSON with limits, supported operations and unsupported
features. `schema` returns JSON Schema projections for strict manifests, voyage/race records
and objective commands plus callback/context/state descriptions and native-only constraints.
It describes data values; Lua functions are not JSON values. Geometry, duplicate JSON keys,
resource ownership and cross-field rules still require native validation.

Validation snapshots `plugin.json` (4 KiB) and `main.lua` (64 KiB) from regular local files.
Links and special files are rejected; the native manifest parser remains authoritative.
A snapshot captures the bytes actually read, not an atomic filesystem transaction spanning
two editor saves. It is never reread during validation. Every job is identified by SHA-256
over its framed snapshot and an unsigned `base_revision` (default 0 for offline validation).
`--expect-bundle HEX` optionally pins that exact snapshot, rejecting a mismatch before execution.
The base revision is correlation metadata; no live owner or stale-commit check exists here.

The worker uses the production host to check the descriptor, compiled location and objective
init. JSON output includes `protocol`, `operation`, `bundle_id`, `base_revision`, `status`
and, after normal completion, `evidence` with native status, file/line/field/message,
gameplay digest and cost counters. This tests init, not future updates/migrations or playability.
Exit 0 means valid; exit 1 means rejected/input error/worker failure/deadline/cancellation;
invalid CLI syntax exits 2. Worker failures never produce accepted evidence.

The default deadline is 5 seconds, configurable in (0, 60]. Ctrl-C cancels the job. The
reusable `ValidationJob` in the script starts a disposable worker, polls with caller-supplied
monotonic time and kills/reaps it on expiry or cancellation. Close each job in `finally`.
Temporary handles are closed at completion. Polling at/after the deadline discards the result.
This bounds acceptance by the supervising caller's polling schedule; it is not a hard real-time
guarantee. Input acquisition runs before that deadline and assumes responsive local storage.
The worker receives only immutable bytes through stdin; its stdout is bounded structured JSON.
Its private framing is two big-endian uint32 lengths followed by manifest then source, with
no trailing bytes. Raw native structs and pointer values never cross the process boundary.

Process isolation and the restricted Lua API protect responsiveness and separate worker faults;
this is not an OS security sandbox against interpreter vulnerabilities.
See the [P4.1 result](PLUGIN_PLAN.md#p41-result-2026-10-29) for historical evidence and
[P4.2](PLUGIN_PLAN.md#p42-result-2026-10-29) for live descriptor staging and preview.

`preview` uses the same deadline/cancellation/snapshot path with `--ticks 0..72000` (default
360). Voyage fixture `voyage-calm-steer-v1` reproduces the complete native checkpoint/seek
review, splitting at `ticks/2`, with player/traffic wakes, throttle for the first 120 ticks
and steering alternating every 240. Its evidence includes position, wake counts, generation,
committed/pending objective digests and current HUD rows (int64 values are decimal strings).
Race fixture `race-four-grid-v1` joins all four native entrants, applies throttle for 120 ticks,
and reports phase, round and the canonical fleet digest. Neither fixture promises to finish
the authored route. A quarantined objective rejects the preview. Cost counters describe staging,
not aggregate simulation work. Successful preview output adds `artifact_id`, SHA-256 over the
bundle/base pins and canonical scenario evidence; it is evidence, never permission to install
anything. No frame capture or application playback effects run in the worker.

## Live descriptor staging (P4.2)

Native `wake_velocity --plugin DIRECTORY` starts trusted packages as before, then runs settled
save/F5 **descriptor staging** in a disposable child of that same executable, using its private
`--plugin-stage-worker` mode before window/audio initialization. It needs no Python or separate
worker binary, including after package relocation. The active native session keeps stepping.
One request owns immutable bytes, a five-second poll-driven acceptance deadline and the current
revision/commit epoch. F5 or a settled newer save cancels/reaps the previous request; F6 cancels
it and discards held output. F7 does nothing while staging. Completion can auto-apply a display
rename or offer the existing F7 migration/restart policy. Undo changes the commit epoch, so an
old response cannot apply merely because its revision number reappears. A failed current
request drops held output and preserves active source/state/world; stale responses retain the
newer active/held revisions and report the stale-base diagnostic. Shutdown cancels/reaps. Preview checkpoint save/restore refuses an owner with
an in-flight process.

The private response is `PST2`, fieldwise network-order unsigned integers, IEEE binary64 data
and bounded length-prefixed strings, at most 4096 bytes. Completion checks the version, exact
framing, exit/status agreement and native schema/map constraints, then installs checked data
without running the candidate Lua again **during staging**. The immutable request retains any
objective source for the existing trusted callbacks. This channel is local to a matching worker
and live host; a CLI artifact/digest is not an installation receipt. The standalone race trial
keeps its synchronous staging path.

The request's fixed native reservation is at most 80 KiB (reported as `staging_native` by
inspection), plus temporary-stream/OS overhead and the separate child's world/Lua/process memory.
Only one worker exists per owner; no request backlog accumulates. File acquisition, spawn and
small temporary-stream writes still occur synchronously and assume responsive local storage.
Native file reads reject FIFOs without waiting for a writer.

**Trust boundary:** native startup, objective update callbacks and Apply-time init/migration
still evaluate trusted Lua synchronously. Process isolation of descriptor staging does not
qualify arbitrary imported code or guarantee uninterrupted objective execution. Browser
import/reload/export must isolate those remaining paths or explicitly restrict its capability
subset, and qualify storage failure, cancellation and repeated sessions on the target. The descriptor-only
browser implementation below resolves this gate for that subset; frame capture, workspace
bridge and human recovery acceptance remain open.


## Browser source bundles (P4.3)

In a built/served browser client, expand **PLUGIN VOYAGE** below the launcher. Load the example
or import a `*.wake-plugin.json` source bundle, edit `plugin.json`/`main.lua`, then select
**Stage / reload**. **Play staged voyage** enters solo `outer` directly. Normal PLAY still
opens the shared title. During a plugin session the same panel appears above the canvas;
opening it pauses solo input. Stage source edits, use **Apply held revision** for route/water
restarts, and **Undo / discard** for the existing native revision policy. Undo while UI staging
is in flight cancels that request first. **Export draft** downloads the editor's current source,
including invalid drafts; exporting is not validation or permission to apply.

The browser accepts exactly voyage kind, outer location, API/schema 1 and `["voyage.define"]`.
It rejects objective/timer grants and race imports explicitly. All imported Lua evaluation
happens in a disposable Web Worker; the game rechecks decoded data and never evaluates that
source at startup, staging completion, Apply, Undo or a tick. Native trusted objectives keep
their existing contract; this subset does not enable imported callbacks or browser races.

Native authors can create the same source bundle with:

```sh
python3 tools/plugin_author.py export plugins/langedrag > build/langedrag.wake-plugin.json
```

The JSON wrapper has exactly `format: "wake-velocity-plugin"`, `version: 1`, `manifest` and
`source` strings. Text is UTF-8, with at most 4096 and 65536 encoded bytes respectively;
imports also cap the complete wrapper at 200000 bytes. The embedded manifest retains strict
native JSON/duplicate-key rules. There are no paths or additional package files in a bundle.
The schema operation describes this wrapper too. Native export checks source sizes/encoding,
not Lua validity. Browser import loads a draft; staging must validate it before use.

Successful staging saves source in host-owned localStorage where permitted. Storage denial
or invalid saved source is visible and nonfatal; export remains available. A new page/session
restores source only and requires validation again. Native world progress, callback state and
Undo history are not exported/persisted. Reloading the page ends the plugin session.

Every request pins its exact bytes and current native revision/commit epoch. A successful UI
receipt can be consumed once by that owner; a changed base rejects it. UI validation and a
native-triggered reload each own at most one request (transport cap two), and terminate/release
workers on completion, cancellation, timeout or page hide. The five-second deadline follows
browser scheduling; suspended tabs can delay its delivery. Worker code/module/Wasm/example
URLs are content-addressed in the same package, loaded on demand, with a shipped Lua notice.

Build/serve instructions and repeatable browser checks are in the [web guide](WEB_BUILD.md#plugin-source-bundles-p43).
Current Chrome evidence and other-browser/device limits belong to the [P4.3 result](PLUGIN_PLAN.md#p43-result-2026-10-29).
