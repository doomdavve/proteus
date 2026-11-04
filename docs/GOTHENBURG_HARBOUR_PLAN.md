# Gothenburg Harbour and Grodan

Proposed 2026-11-04 on branch `docs/gothenburg-harbour-plan`. This document owns the renaming of
the `outer` location to Gothenburg Harbour, the Grodan canal tour, its canal boat, the canal
bridges and the sights along the route. [All plans](README.md).

## Why

Since the [river retirement](RIVER_RETIREMENT_PLAN.md), `outer` is the only Gothenburg location.
The title still calls it OUTER RIVER, but it runs from Långedrag to Gullbergsvass and the city
canals. The user wants the name to say where the player is, and as much life as possible in it.

The Paddan sightseeing tour, which has run in the canals since 1939, passes the sights the map
already holds and some it lacks. A virtual version, Grodan, gives the location a route that
shows off the city. The bridges are its most visible gap: the boat passes under about twenty low
bridges, and the bake draws only Hisingsbron and the river kit's deck.

## Decisions

- **Display name GOTHENBURG HARBOUR** (user, 2026-11-04). Only player-visible text changes. The
  id stays `outer` in the wire protocol, plugin manifests (`"location": "outer"`), saved title
  files, command lines and Winterfell's room config. Renaming the id would need a protocol bump and
  a davve-srv change, and players would gain nothing.
- **The tour and its boats are called GRODAN** (user, 2026-11-04), a made-up name in place of
  Strömma's Paddan brand, as VINGA LINE stands in for Stena Line. See [naming](#naming).
- **The player drives a canal boat on the tour** (user: "A paddan boat would be awesome!"). It is a
  new boat with its own look and handling, not a skin on the explorer.
- **Bridge decks are scenery first.** They are drawn and cast shadows, but boats do not collide with
  them. Low-bridge contact (the duck-your-head moment) is a separate, later step: [GH8](#gh8-low-bridge-contact-later).
- **The tour route follows the operator's published map.** See the
  [route](#route) for the user's screenshot of the Kungsportsplatsen tour and the
  [Swedish Wikipedia article](https://sv.wikipedia.org/wiki/Paddan). The operator's temporary
  autumn 2026 route around the Rosenlund works is ignored.

## Naming

**GRODAN** (the frog) names both the boats and the tour (user, 2026-11-04: "Grodan is
perfect"). Paddan means the toad, so Grodan is a near relative, the way VINGA LINE plays on
Stena Line. It is plain ASCII and needs no new font glyphs. Kanalturen was the working name
until then.

Candidates considered on 2026-11-04:

- SKÖLDPADDAN / THE TURTLE, the user's first suggestion. Paddan is short for *sköldpadda*:
  Gundel Matsson described the first rebuilt boat (1939) as a tortoise lying upside down.
- SKÖLDEN, the front half of *sköldpadda*.
- VITSEN, the producer-gas boat of 1940; the word also means "the joke".
- ANKAN, for the low bridge where everyone has to *buka* (duck).

## Route

Proposed 2026-11-04, traced by eye from the operator's map. [GH5](#gh5-result-2026-11-04)
fixed it against the compiled water and changed step 2: Fattighusån leaves the map at its
eastern edge (east 4200 m) and is too narrow to turn in, so the tour turns west at Slussen into
Stora Hamnkanalen and leaves it at Stora Bommen.

1. Start at the Kungsportsplatsen pier on Vallgraven (Kungsportsbron).
2. Along Vallgraven past Trädgårdsföreningen, then west through Fattighusån and Stora Hamnkanalen
   towards Nordstan and Brunnsparken.
3. Out into the river near Lilla Bommen and Göteborgsoperan, upstream towards Hisingsbron and
   Frihamnen, and turn.
4. Down the river past Lindholmen and Stenpiren to Masthugget/Stigberget, and turn.
5. Back east into Rosenlundskanalen past Feskekörka, then Vallgraven past Pustervik, Nya Allén and
   Kungsparken to Kungsportsplatsen.

**Water (checked 2026-11-04).** The user confirmed the canals are in the outer map's water.
Point tests against the compiled water (`assets/geometry/outer/water.h`) found water at
Brunnsparken, Kungsportsplatsen, Vallgraven by Trädgårdsföreningen and Rosenlundskanalen by
Feskekörka. GH5 measured the widths and drove the loop: see its [result](#gh5-result-2026-11-04).

**Facts from the [Swedish Wikipedia article](https://sv.wikipedia.org/wiki/Paddan)** (read
2026-11-04). The lowest bridge is at Stora Bommen. It is nicknamed *Osthyveln* (the cheese
slicer) because even at low water it often makes passengers bend down. It was raised in a 2011
renovation and given a cheese slicer as decoration underneath. The article gives no clearances in
metres.

## Inventory (pinned OSM corridor, checked 2026-11-04)

`docs/adventure/sources/osm-corridor.json.gz` already holds what the bake needs. No new extract is
required for GH2 or GH6.

- **Bridge outlines.** The canal area has 19 `man_made=bridge` areas with `bridge:structure`.
  - Arch: Fontänbron, Fredsbron, Slussbron and Västra Stampbron.
  - Beam: Kvarnbron, Drottningtorgsbron, Kungsportsbron, Residensbron, Stora Bommens Bro,
    Pusterviksbron, Tyska Bron, Bananbron, Vasabron, Kämpebron, Östra Stampbron, Brunnsparksbron,
    two unnamed, and Hisingsbron (already modelled).

  There are also 97 `bridge=*` road and path ways, among them Viktoriabron, Bazarbron and Södra
  Sjöfarten. No bridge carries a clearance tag; `maxheight` tags are for road traffic.
- **Sights with footprints.**
  - With OSM heights: Göteborgsoperan (`height` 32 m), Feskekörka (14.3 m), Börsen (18 m) and
    Skansen Lejonet (24 m).
  - With levels or no height: Kronhuset, Stora Teatern, Skansen Kronan and Palmhuset.
  - Barken Viking and Lilla Bommen (Läppstiftet) are already modelled ([A3.5g](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a35g-result-2026-10-28)).
- **Museum ships.** Maritiman's fleet at Packhuskajen is tagged as `seamark:type=hulk`: HMS Småland,
  Fladen, Dan Broström, Flodsprutan II, Nordkaparen, Ada I/II and others.

## Delivery and dependencies

Each step is its own worktree and pull request. Dependent steps stack on their parent's branch.

| Step | After | Result | Status |
| --- | --- | --- | --- |
| [GH1](#gh1-rename-to-gothenburg-harbour) | — | GOTHENBURG HARBOUR in the title, hints, package names and guides | Implemented 2026-11-04; [result](#gh1-result-2026-11-04) |
| [GH2](#gh2-canal-bridges) | — | The canal bridges baked from their OSM outlines, and a camera that stays readable under a deck | Proposed |
| [GH3](#gh3-the-canal-boat) | — | A low, wide, open canal boat with its own handling | Implemented 2026-11-04; [result](#gh3-result-2026-11-04) |
| [GH4](#gh4-tour-format) | — | A plugin route format with a free start and up to 32 ordered stops with captions | Implemented 2026-11-04; [result](#gh4-result-2026-11-04). Speed-limit zones and Å/Ä/Ö moved to GH4b |
| GH4b | GH4 | Å, Ä, Ö and É in the menu and HUD fonts and in stop names and captions; canal speed-limit zones | Proposed |
| [GH5](#gh5-the-grodan-package) | GH3, GH4 | The bundled Grodan tour, offered on the title | Implemented 2026-11-04; [result](#gh5-result-2026-11-04). User run 2026-11-04: not fun yet; [revised](#gh5-revision-after-the-users-run-2026-11-04) |
| [GH6](#gh6-sights-along-the-route) | — | The Opera, Feskekörka, Börsen, Kronhuset, Stora Teatern, Skansen Kronan/Lejonet, Palmhuset and Maritiman | Proposed |
| [GH7](#gh7-canal-boats-in-traffic) | GH3, [Marina M3](MARINA_AUTHORING_PLAN.md#m3-authored-routes-and-docking-motion) | Canal boats running the loop in free play | Proposed |
| [GH8](#gh8-low-bridge-contact-later) | GH2, GH5 | Low-bridge contact and the duck moment | Later |

GH2 is the user's priority ("really important"). It can start alongside GH1 and GH3.
GH5 plays without GH2, but the tour shows the city properly only once the bridges are in.

## GH1: rename to Gothenburg Harbour

Read `tools/boat_title.c`, `tools/wake_velocity.c` and `plugins/outer-free/plugin.json`.

- Replace OUTER RIVER in the title's PLACE row (`boat_title.c`), its hints ("THE STANDARD OUTER
  RIVER", "RIVER RACES ALWAYS RUN ON THE OUTER RIVER") and the in-game location label
  (`wake_velocity.c`). Check the longer label fits the panel at every supported resolution and on
  the phone layout.
- Rename the `outer-free` package's display name. Keep package ids.
- Update `docs/PLAYING.md`, `docs/COMMAND_LINE.md`, `tools/windows/README.txt`, the README and the
  comments in the web check scripts that name the outer river. Leave dated history as it is.

Acceptance: title captures (CPU, plus the browser title check) show the new name; `ctest --preset
full` passes; `--location outer` and the hosted room's command are unchanged.

### GH1 result, 2026-11-04

- `BoatWorldDefinition.title` holds the player-facing name (BASIN, GOTHENBURG HARBOUR). The
  title's solo and host PLACE rows and the browser's LOADING line use it; `name` stays the
  command-line and wire spelling `outer`.
- Hints now read THE STANDARD HARBOUR and RIVER RACES ALWAYS RUN IN THE HARBOUR. The
  `proteus.outer-free` package is named "Gothenburg Harbour free boating"; package ids are
  unchanged.
- Updated the live guides (PLAYING, COMMAND_LINE, WEB_BUILD, MACOS_BUILD, the index intro,
  AGENTS.md), the Windows README and the browser check scripts' comments. Dated history keeps
  OUTER RIVER.
- The content option is still spelled HARBOR LIFE (American); unchanged here.

Verification, 2026-11-04, Apple Silicon/macOS, Debug: build warning-clean (only the upstream
`stb_vorbis.c` warning); `ctest --preset full` passed all 366 enabled tests (13 retired wake-field
tests disabled, GPU tests off). `< GOTHENBURG HARBOUR >` measures 125 px in the m5x7 font
against the 126 px value column. CPU title captures of the solo and host panels (made with a
temporary local patch, not committed) show the full name. The browser title scripts and a
browser build were not run; no human play.

## GH2: canal bridges

Read `tools/prepare_outer_map.py`, `tools/outer_landmarks.py`, `tools/map_pack.py` and the
[A3.5g Hisingsbron result](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a35g-result-2026-10-28). This
takes over the bridge-outline part of A3.5d.

- Build each `man_made=bridge` area in the canal and river region from its OSM outline:
  - a deck with the road or path surface and railings;
  - abutments on the quay walls;
  - an arched underside for `bridge:structure=arch`, a flat beam soffit otherwise.

  Bridges that have only `bridge=*` ways get a deck from the way's width and the water it crosses.
- **Heights.** OSM has none, and Höjdmodell is a ground model with bridges removed. Proposed
  sources, in order (2026-11-04; none downloaded or checked over the canals yet):
  1. **Deck tops from an aerial point cloud.** Lantmäteriet's *Laserdata Nedladdning, skog* is
     open data (CC0), 1–2 points/m², and its classification level 3 marks bridge points
     (class 17). Bridge points minus nearby water points give each deck's top above the water.
     Check first that the tiles over central Gothenburg have level 3, and that download works
     with a free Geotorget account. Göteborgs Stad's own scans would be denser (the 2022 survey
     behind Höjdmodell had 10 points/m²), but only the derived ground model is confirmed open.
  2. **Clearance from the deck top.** Aerial scans do not see the underside. Subtract a deck
     depth for each structure (beam or arch crown), and use the arch's span and rise for its
     curve.
  3. **Calibrate with photographs** of tour boats passing under, scaled by the boat's known
     height or the quay's Höjdmodell height. The low bridge where passengers duck sets the
     minimum.

  Record each bridge's source and value in a table here. A bridge with no source gets a
  conservative default, marked as such.
- **Look.** Make a reference sheet per bridge: Wikimedia Commons photos (record their
  licences), Swedish Wikipedia's bridge articles, and photos or a phone video from a tour boat
  taken by the user. The sheet covers material (granite arch, iron or concrete beam),
  railing type, lamps and colours. Geometry comes from OSM and the heights above; photos are
  only references, and no photo textures are copied into the game.
- Keep the water continuous under every deck. The bake's bridge-leg carving must not close a canal.
  Add a check that the tour route's corridor stays connected water.
- **Camera.** At its distance of 12 m and height of 6 m, the camera sits inside a canal deck while
  the boat is underneath. Pull the camera in and down under a deck, or fade decks that come between
  camera and boat. Choose by having the user drive it; stills have misled on temporal effects before.
- Count triangles and check outer frame times against the
  [performance baseline](BOATING_PERFORMANCE.md) on both renderers.

Acceptance:

- CPU and GPU captures under and beside Kungsportsbron, Residensbron, an arch bridge and the
  lowest bridge.
- The water connectivity check passes.
- A user drive through Vallgraven with the camera change.

## GH3: the canal boat

Read `src/boat_skin.[ch]`, `src/boat_model.[ch]` and the
[cabin boat return](BOATING_PLAN.md#cabin-boat-return--2026-10-30).

- A new boat choice: low and flat, open passenger rows under a low windscreen, a helm aft and an
  inboard drive. Dimensions come from photographs of the current boats; record the reference used.
- Its own handling profile: slow, heavy, little heel, a wide turning circle. Solo only at first;
  rooms keep their current boats.
- A made-up livery with GRODAN on the hull ([naming](#naming)). No Strömma marks.

Acceptance: CPU/GPU captures from the chase camera; a user drive in the canals and on the open
river.

### GH3 result, 2026-11-04

The user chose real scale over a visual-only or toy-sized boat (2026-11-04).

- **Size.** No dimensions are published: Swedish Wikipedia gives 73–92 passengers per boat, the
  fleet's builders and years, but no length or beam. The Grodan is 15 × 4.2 m, an estimate from
  the seating; replace it if measured figures turn up.
- **Physics.** `BoatHandling` can carry a `BoatHull` (bow, stern, half beam, yaw inertia). Shore,
  quay, harbour-scene and traffic contacts use it, with side samples at most 2 m apart, so a
  quay corner cannot pass between the ends. The speedboat-sized boats keep their six exact
  samples and literal bounds, so their traces are unchanged. Room pair contacts, scene-clear
  checks and `boat_hull_clear` still use the speedboat footprint; the Grodan is solo only.
  Buoyancy, pitch, roll and wake emission are still the speedboat's: the long hull bobs like a
  small one in waves.
- **Handling.** Thrust 1.45 gives 4.15 m/s (8.1 knots) flat out in calm water, and a yaw inertia
  of 30 gives a turning circle of about 21 m at full lock.
- **Look.** The shared parametric hull recipe (frog green, dark green stripe, GRODAN painted on
  both sides, fenders), plus 8 rows of yellow benches, about 20 passengers, a raked bow
  windscreen, the guide's stand, the helm with its skipper, grab rails, and a stern ensign
  staff. A boat choice in SETTINGS and `--skin grodan`; rooms keep their Nordic liveries. The
  default camera sits 12 m farther back and 4 m higher for this boat.

Verification, 2026-11-04, Apple Silicon/macOS, Debug: build warning-clean (only the upstream
`stb_vorbis.c` warning); `ctest --preset full` passed all 367 enabled tests (13 retired wake-field
tests disabled, GPU tests off). `boat_voyage` now checks the Grodan's top speed (3.9–4.4 m/s), its
turning circle (15–35 m), and that a minute full ahead from the dock on 8 headings leaves no point
of its 15 × 4.2 m footprint on land. With the speedboat's footprint the last check fails (55,263
points on land), so the side samples matter. `boat_skin` covers the parse, meshes, outboards and
ensign. CPU and GPU captures at `--location outer --time 3/8`, including a low camera, were
inspected. Not checked: a human drive, canal manoeuvring, the flag cloth at the new staff (too
small to judge at 320×200), browser builds.

## GH4: tour format

Read the [voyage schema](PLUGIN_API.md#voyage-schema), `src/boat_plugin*.[ch]` and the
[objective transactions](PLUGIN_API.md#objective-transactions-p31--p32).

A voyage allows 2–4 shelters, and the first must be the location's spawn (the Eriksberg dock).
A race allows up to 8 gates and a four-boat grid. Neither describes a 20-bridge loop that starts
at Kungsportsplatsen.

- Add a new plugin kind, `tour`, rather than optional fields on `voyage`. The voyage schema requires
  every field exactly, and the reference must keep decoding bit for bit.
- A tour has:
  - a start pose on open water, anywhere on the map;
  - up to 32 ordered waypoints, each a gate (position, direction, half-width) with an id, a
    name and a short caption;
  - optional speed-limit zones;
  - an arrival area.

  Objectives can use waypoint events, so a tour can score keeping to the route, the speed limit
  and no contact.
- The HUD shows the next sight's name and caption as the boat passes. Add Å, Ä, Ö (and É) to the
  menu and HUD fonts (`tools/generate_menu_fonts.py`) so captions can spell Feskekörka and
  Trädgårdsföreningen, and allow them in names and captions.
- Staging checks gates and the route between them against the water, as the race does.
- Browser parity: the browser catalog and import accept the new kind.

Acceptance: schema and staging tests (valid tour, gate on land, broken order, too many
waypoints); a reference tour decodes identically native and in the browser worker.

### GH4 result, 2026-11-04

Implemented differently from the proposal above, to reuse the voyage runtime, HUD, recovery,
objectives, reload policies and browser path instead of adding a plugin kind:

- A `voyage` package returns `tour = { stops, arrival }` instead of `voyage`
  ([tour schema](PLUGIN_API.md#tour-schema)). Stops are reach circles like shelters, not
  directional gates, with a required caption. `BoatVoyageRoute.ordered` marks a tour; capacity is
  32 stops, while destination voyages keep their limit of four.
- Ordered progress, arrival only after every stop, and `boat_voyage_next`/`boat_voyage_target`
  for the cue. A session restart or voyage restart puts the boat at the first stop's pose; the
  spawn rule applies only to destination voyages, and a tour's arrival needs 8 m of water, not 20.
- The HUD names the next stop, shows each caption for 8 s on passing (else STOP N OF M), and the
  panel widens to fit long text; the minimap marks the next stop and draws no exposure for calm
  routes.
- Destination voyage digests are unchanged; a tour's digest also covers its captions, and a
  caption edit classifies as a rename. Browser wire version `PSTA` carries the tour flag and
  captions.
- Not done: speed-limit zones and Å/Ä/Ö (GH4b), directional gates, and a check that the water
  between stops is connected.

Verification, 2026-11-04, Apple Silicon/macOS, Debug: build warning-clean (only the upstream
`stb_vorbis.c` warning); `ctest --preset full` passed all 367 enabled tests (13 retired wake-field
tests disabled, GPU tests off). New checks: `boat_plugin.tours` (decode, captions, copy binding,
rename classification, outer staging off the spawn and on land, ordered progress and arrival,
recovery, wire round trip, invalid fields, 32/33 stops) and tour start placement in
`boat_voyage`. `boat_hud_review --voyage tour|tour-caption|shelter` captures were inspected: the
tour headline and a 46-character caption fit the widened panel, and the Långedrag panel is
unchanged. No browser build or browser checks, no game run with a tour package (none exists
until GH5), no human play.

## GH5: the Grodan package

- `plugins/grodan`: the [route](#route) as GH4 waypoints, one at each bridge and sight, with
  captions. Starts the player in the GH3 boat at Kungsportsplatsen.
- Trace the route with `plugin_author.py capture`/`anchors` and fix it against the map.
- A title entry next to the Långedrag voyage.
- An objective HUD: sights passed, time, speed-limit warnings in the canals.

Acceptance: the bundled catalog builds it; a scripted drive finishes the loop; CPU/GPU captures at
three sights; a user run of the whole tour.

### GH5 result, 2026-11-04

- **Package.** `plugins/grodan` (`proteus.grodan`, kind `voyage`) returns a 31-stop tour. Stop
  1 is the Kungsportsplatsen pier on the Vallgraven side north-east of Kungsportsbron. The tour
  arrives south-west of the bridge at 2 m/s or less. Native and browser catalogs both list it.
- **Stops.** 22 stops are at sights and bridges, including "OSTHYVELN, THE CHEESE SLICER: HEADS
  DOWN" at Stora Bommen. Nine more are at canal bends, named after the nearest bridge or place.
  Every stop has an ASCII caption. Apart from Osthyveln, the facts in the captions come from
  general knowledge and are not checked against sources: Palmhuset 1878, Centralstationen 1858,
  Barken Viking 1906, the Opera 1994, Hisingsbron 2021, Masthuggskyrkan 1914, Feskekörka 1874,
  Stora Teatern 1859, and the Gustav II Adolf statue's pointing legend.
- **Placement.** A scratch planner placed the stops from the compiled water and the OSM canal
  centrelines, re-centred on the water. It is not checked in.
  - Each sight may move 40 m along the route.
  - The planner adds the fewest bend stops for which every straight leg between stop centres keeps
    0.6 times the canal's centreline clearance, at most 6 m.
  - Slussen got a hand-placed turning arc.
- **Format change: tour stops need 4 m of water, not 8.** The 8 m GH4 rule fits no stop in Stora
  Hamnkanalen east of Fontänbron (12 m wide in the map for 400 m) or the Rosenlund narrows (9–11 m).
  A tour stop's recovery pose faces along the canal, so it needs room for the beam (2.1 m). The
  arrival keeps 8 m, and destination voyages are unchanged.
- **Title.** CONTENT on GOTHENBURG HARBOUR cycles STANDARD, HARBOR LIFE and GRODAN TOUR, with the
  hint "CANAL TOUR FROM KUNGSPORTSPLATSEN". It is saved as `outer_content=grodan`. GRODAN TOUR
  always launches `--package proteus.grodan --skin grodan`, whatever livery is chosen.
  `--title-start outer-grodan` exists for tests.
- **Not done.** The objective HUD (sights passed, time) and canal speed-limit warnings
  ([GH4b](#delivery-and-dependencies)). Directional gates. The tour package is not in
  `plugin_author.py`'s or `package_plugin_tools.py`'s example lists.

Verification, 2026-11-04, Apple Silicon/macOS, Debug:

- **Suite.** Warning-clean build (only the upstream `stb_vorbis.c` warning). `ctest --preset full`
  passed all 369 enabled tests (13 retired wake-field tests disabled, GPU tests off), among them
  `wake_velocity_title_start.outer-grodan`.
- **Scripted drive.** The new case `boat_plugin_session.grodan_tour` (slow lane, about 5 s) opens
  the bundled entry and drives the Grodan by pure pursuit along the lines between stop centres:
  half throttle in the canals, full on the river, damped steering. Its 15 × 4.2 m footprint never
  left the water. It passed all 30 stops after the start in order and arrived in 35.9 simulated
  minutes.
- **Track plots** at Slussen and the narrows were inspected (scratch tooling).
- **Captures inspected.**
  - CPU and GPU from tour copies starting at Stora Bommen and Göteborgsoperan, CPU only at Slussen
    and Feskekörka: the boat sits in the canal or river facing along the route, with Karlatornet
    or Läppstiftet and Hisingsbron ahead where expected.
  - The title's single-player panel with CONTENT < GRODAN TOUR > and its hint, through a
    temporary capture patch that was reverted.
  - `--title-start outer-grodan` opens the package; `outer-bogus` and `nowhere` are rejected.
- **Not checked.** No browser build or browser checks. The in-game tour HUD was not captured
  (GH4's `boat_hud_review` captures cover its layout). No human play: the user's run of the
  whole tour is still owed.
- **User run, 2026-11-04.** The user played the whole tour at 8× speed. They saw no bridges, and
  the canals looked as on earlier visits, only slower. Their verdict: "It's not fun yet." Not
  accepted.
- **Handling.** The autopilot first jammed against Vallgraven's west quay. The Grodan's yaw
  inertia (30, against the speedboat's 2.2) works against the speedboat's yaw damping, so a turn
  overshoots by about 0.5 rad unless steering is eased off early. Players will feel this in the
  12 m channels; GH3's handling may need more yaw damping after the user's run.

### GH5 revision after the user's run, 2026-11-04

The user agreed to three follow-ups, in this order: bridges with estimated heights, a shorter
loop, and the low-bridge duck and scrape score. They also asked to drive the same tour in their own
boat ("Same tour just for me, not the tourists").

- **Shorter loop.** The river legs to Hisingsbron and Masthugget are gone. From Stora Bommen the
  tour runs a short way down the river to Rosenlundskanalen. The loop is now 3.7 km, down from
  6.6 km, with 26 stops, planned the same way. It takes about 15 minutes in the Grodan at full
  speed, more than the 10–12 minutes proposed to the user.
- **Own boat.** CONTENT gains < CANAL TOUR > (saved as `canal-tour`): the same package in the
  livery and handling chosen in SETTINGS. < GRODAN TOUR >'s hint now says "IN THE GRODAN, WITH ITS
  PASSENGERS".
- **Drive check.** The drive case runs the loop twice, in the Grodan and in the speedboat. Each run
  scales its throttle to the Grodan's push and slows on legs into the narrowest stops. Neither
  footprint left the water: 21.9 and 19.9 simulated minutes.
- **Verification**, 2026-11-04, macOS Debug: `ctest --preset full` passed all 370 enabled tests,
  with `wake_velocity_title_start.outer-canal-tour` new. A CPU capture of the canal tour shows the
  speedboat at the pier. No human play of the revision.

## GH6: sights along the route

Read `tools/outer_landmarks.py` (Karlatornet, Lilla Bommen, Hisingsbron in
[A3.5g](FREE_RIDING_ADVENTURE_FIRST_STEPS.md#a35g-result-2026-10-28)).

- Model from OSM footprints and real heights:
  - Göteborgsoperan's sloping roof;
  - Feskekörka's steep gothic roof;
  - Börsen;
  - Kronhuset;
  - Stora Teatern;
  - Skansen Kronan and Skansen Lejonet on their hills;
  - Palmhuset in Trädgårdsföreningen.
- Maritiman's ships moored at Packhuskajen, as fleet hulls with fixed poses.
- Then the rest of A3.5d along the canals: `building:part` heights, roof shapes and facade colours,
  so the canal fronts read as old Gothenburg rather than boxes.

Acceptance: captures of each sight from the tour route on both renderers; frame times recorded.

## GH7: canal boats in traffic

After Marina M3's authored routes: a few GH3 boats run the tour loop on a timetable, so the canals
have life in free play too. They stop at Kungsportsplatsen, keep clear of the player, and
validate against shore, bridges and other traffic.

## GH8: low-bridge contact (later)

The duck moment. Boats get a ceiling contact under the lowest bridges, and the tour shows a
"DUCK!" cue with an objective penalty for speed under the deck. Bridge decks would then enter the
core contact data, which `proteus_boat_core` and the network must agree on. That needs its own
design before work starts.

## Status

GH1 implemented 2026-11-04 on branch `feat/gothenburg-harbour-name` ([result](#gh1-result-2026-11-04)),
GH4 on `feat/grodan-tour-format` ([result](#gh4-result-2026-11-04)), GH3 on `feat/grodan-boat`
([result](#gh3-result-2026-11-04)) and GH5 on `feat/grodan-tour` ([result](#gh5-result-2026-11-04)).
The user asked to continue with the steps that need no new data from them: GH6 (sights along the
route) next. GH2 waits for bridge height data. The user ran the Grodan tour on 2026-11-04: no
bridges, nothing new to see, too slow, not fun yet.

## Verification and handoff

Each step records what it checked: date, platform, build options, tests, captures inspected and
any user play. Captures alone do not count as acceptance for motion, camera or handling.

## Known limitations

- The tour follows the operator's map only roughly: Stora Hamnkanalen replaces Fattighusån, which
  leaves the map ([GH5](#gh5-result-2026-11-04)).
- The Grodan overshoots turns in the narrow canals (GH5); its handling has not been played.
- Bridge heights have proposed sources ([GH2](#gh2-canal-bridges)), but none has been checked
  over the canals.
- No sight has had its look checked against photographs yet.
