# Lua plugin API (voyage and race descriptors)

Status, 2026-10-29: **P1 voyage and P2 local race authoring implemented** on top of the P0
loader. P2 adds `boat_race_trial --plugin plugins/river-sprint` and headless native race checks.
A native solo session can take its outer voyage from a plugin (`--plugin`), reload it
when the file is saved or on F5, undo, and apply route or water changes with an explicit
restart. Rooms, the course and replays, and the browser game keep the compiled voyage. The
[plugin plan](PLUGIN_PLAN.md) owns the staged roadmap; this guide owns the current contract.
[All plans](README.md).

## Runtime

| Item | Decision |
| --- | --- |
| Interpreter | Lua **5.4.9**, `https://www.lua.org/ftp/lua-5.4.9.tar.gz`, SHA-256 `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`, matching lua.org's published checksum. Fetched by CMake `FetchContent` with `URL_HASH`. |
| Why 5.4 | It is the mature release line. 5.5 (2025-12) is newer; moving to it is a deliberate later choice, not a patch update. |
| Compiled files | VM/compiler sources plus `lauxlib`, `lbaselib`, `lstrlib`, `ltablib`, `lmathlib` only (`proteus_lua`). `loadlib`, `liolib`, `loslib`, `ldblib`, `lcorolib`, `lutf8lib`, `linit` and the `lua`/`luac` programs are **not compiled**. |
| Configuration | Unmodified sources; the portable default `luaconf.h` (no platform `LUA_USE_*`). `cmake/proteus_lua.h` is force-included to fix the string-hash seed, so table traversal and diagnostics are identical from run to run. The project-wide `-ffp-contract=off` applies. |
| Web | The same sources compile with Emscripten 6.0.12; the validator passes under Node (see the [P0 result](PLUGIN_PLAN.md#p0-result-2026-10-28)). The browser game does not link the plugin host: it has no `--plugin` and no files to edit (browser import is P4). |
| License | Lua is MIT-licensed ("Copyright (C) 1994-2026 Lua.org, PUC-Rio"); the notice is at the end of `lua.h`. Configure extracts it to `build/lua-LICENSE.txt` and fails if it is missing. Native packages ship it as `lua-LICENSE.txt` beside the raylib/jsmn/ENet notices (`tools/macos/package.py`, `.github/workflows/windows-boat.yml`). The web site does not contain Lua, so `tools/package_web.py` is unchanged. |

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
| `capabilities` | Exactly `["voyage.define"]` for voyage or `["race.define"]` for race |
| `state_version` | `1` (reserved for P3 script state) |

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
| F6 | Discard a held candidate; otherwise hold the previous revision (Undo) |
| F7 | Apply a held candidate with its restart |

Staging never changes the running voyage. A candidate is compared with the active revision
(`boat_plugin_classify`):

| Change | Policy |
| --- | --- |
| Identical values (comments and layout ignored) | Nothing happens; the panel shows UNCHANGED |
| Display names only: shelter `name`, manifest `name`; ordered shelter IDs and every number identical | Applied at once. The boat, clock, water, wakes and all voyage progress are kept; the HUD shows the new names |
| Shelter IDs, order, count, position, heading or radius; arrival; `exposed_fraction` | Held until F7, then **restart voyage**: progress, timing and recoveries begin again at the start shelter; the boat, clock, water and any weather already met continue. A boat already inside a shelter or the arrival area is observed by the ordinary rules on the next tick |
| `exposure` or `weather_range` | Held until F7, then **restart session**: the same full restart as Backspace without a voyage (tick zero, the boat at the spawn, calm water until the weather begins) with the new revision |
| Manifest `id`, `kind`, `location`, `api` or `state_version` | Not applied; relaunch to use another plugin |

A failed stage (I/O, manifest, syntax, runtime, memory, budget, schema or map check) keeps the
active revision and drops any held candidate; the panel shows the file, line when Lua gave
one, field path and message. Revisions are numbered R1 (launch), R2, ... in the order they are
staged; a held candidate staged again unchanged keeps its number. Undo keeps the eight
previous revisions. It reinstalls a revision through the same rules and does not rewind the
boat or the clock; a rename is undone at once, a route or water change is held for F7. A new
revision after an Undo discards the undone ones (no Redo). Progress is never inferred from an
old shelter index: every restart begins at shelter 0.

## Native interface

`src/boat_plugin.h` works on immutable bytes and does no I/O. `src/boat_plugin_io.h` is the only
plugin code that touches files.

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
| Instructions | 1 000 000 | Count hook every 100 VM instructions while the entry runs. Exceeding it is `BOAT_PLUGIN_BUDGET`, deterministically and without a clock. After exhaustion every instruction fails, so nothing can resume. |

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
