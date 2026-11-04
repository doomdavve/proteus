# River location retirement

Prepared and implemented 2026-11-04 on branch `feat/retire-river`. This document owns the
retirement of the `river` location, its compatibility consequences and the steps still owed.
[All plans](README.md).

## Why

The `river` location was a crop of the outer map's water in the same frame: the Eriksberg dock
towards Älvsborgsbron. The outer map holds all of it plus Lindholmen, Lilla Bommen, Hisingsbron
and the fjord to Långedrag. The title's default outing was the river, so new players boated to
the crop's edge, hit it, and never saw the Gothenburg content. The user asked to retire it.

## Decisions

- **One mapped location.** `BoatLocationId` is now basin (0) and outer (1). The title's
  default PLACE (solo and host) is OUTER RIVER, with the Standard (Långedrag voyage) or Harbor life
  content. Saved `title.txt` files naming `river` fall back to that default.
- **Races move to outer, unchanged.** `plugins/river-sprint` and `plugins/harbor-race` keep their
  coordinates; outer shares the crop's frame. Race manifests, rooms, the server, the protocol and
  `--host race` require `outer`. The package IDs keep the name "river sprint": it is still a
  sprint down the river.
- **`proteus.river-free` is deleted.** Free boating on the mapped water is `proteus.outer-free`.
- **Protocol v12.** Accept's and the room definition's location byte is 0 basin or 1 outer; v11
  and older peers are incompatible ([protocol](MULTIPLAYER_PROTOCOL.md)).
- **The crop stays as data, not a location.** On the outer map the river kit (crane, bridge deck,
  cafe, frontages, quay fittings, street life and harbour) lays itself out from the crop's scenery
  pack, so `waterfront_city_river_map` and `assets/geometry/river/scenery.bin` remain in the
  build. Loading the outer location now also loads that pack. The crop's water atlas and ambient
  sidecars no longer ship in the browser bundle; validators still use them.
- **Validators keep the crop as a fixture.** Map, physics, scenery, harbor, traffic and water
  review checks drive it through `TEST_RIVER` (`tools/boat_test_location.h`), a test-only
  definition with the retired record's values. Moving them to outer would have changed numeric
  expectations across about twenty validators and slowed the suite, for no player-visible gain.
  Race, room, network, plugin and minimap checks moved to outer.
- **A clear refusal.** `wake_velocity --location river` and `boat_server --location river` print
  that the location is retired and that `--location outer` holds its water.

## Status

Implemented and automatically verified; not yet played by a human, reviewed or released.

### Verification, 2026-11-04 (Apple Silicon/macOS, Debug)

- Build warning-clean; `ctest --preset full` passed all 366 enabled tests (13 retired
  wake-field tests disabled, GPU tests off). New tests `boat_river_location_retired` and
  `boat_server_river_location_retired` check the refusal message.
- With `PROTEUS_GPU_TESTS=ON`, all 34 enabled `gpu` tests passed, including the outer GPU
  screenshots, the windowed outer `--host deathmatch` session and the GPU water reviews on the
  crop fixture.
- Race checks pass on outer with the unchanged course: `validate_boat_race` gate/approach water
  checks, `validate_boat_plugin_race`, `boat_race_trial` (bot and ferry runs) and the race rooms in
  `validate_boat_net` and `validate_boat_room_package`.
- CPU and GPU (`--gpu-aa`) captures of `--location outer --time 3`, the `--title-start
  outer-standard` capture and a `boat_race_trial --plugin plugins/river-sprint` preview at 18 s
  were inspected: the dock, crane kit, quays and the sprint's buoys under Älvsborgsbron draw.
- Browser: the `web` target builds with Emscripten 6.0.12. `tools/check_web_scenery.py` passed
  against a local server with Chrome: the default outer place fetches the outer pack once
  (5.3 MB) and a second session reuses it. `tools/check_web_title.py` passed its basin and Harbor
  life sessions, then timed out when its desktop console edit of Harbor life produced a Lua
  syntax error (`main.lua:127`). That path does not involve this change and was not checked on
  `main`.

### Known limitations

- The race now shares the outer map's upstream traffic and harbour. Automated races finish, but
  no human has raced on outer.
- Browser players reach the outer map on the title's default START and so download its 5.3 MB pack
  before their first session (basin players still download none).
- The other browser checks (`check_web_controls`, `check_web_resolution`,
  `check_web_multiplayer`, `check_web_plugin_author`) were updated for the new PLACE order but
  not run.
- Dated plan logs still describe the river location as it was; they are history.

## Next tasks

1. **Release (owed with the first v12 release).** The hosted Winterfell room runs
   `--mode deathmatch --location river` (`wake_velocity_location: river` in
   `davve-srv/ansible/roles/wake_velocity/defaults/main.yml`). The v12 server refuses that, so
   set it to `outer` in the same release cutover; also rename the service description's "river
   room". See [hosting](MULTIPLAYER_HOSTING.md#routine-deployments-davve-srv).
2. **Play it.** A solo outer outing from the title, a hosted outer deathmatch and a two-player
   race on outer.
3. **Optional.** Move the remaining validators from the crop fixture to outer and drop the crop's
   water atlas and ambient sidecars with their bake checks.
