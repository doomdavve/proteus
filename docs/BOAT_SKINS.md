# Player speedboat skins

Implemented 2026-10-24 in the isolated `boat-speedboat-skins` worktree. The player craft
has a fine pointed bow, simple faceted V hull, flared shoulders, a dark rub rail, a broad
foredeck stripe, low swept windscreen, an open cockpit cutout with a lowered two-seat well and twin compact outboards.
The accepted baseline uses Sweden, Finland, Denmark and Norway. The user accepted the 2026-10-25 speed
profile in `boat-white-hulls`, refining the bow taper, forefoot, windscreen and stern/cowl
shapes. The user also accepted the twin outboards; the package is being prepared for main integration. The [owning plan](BOATING_PLAN.md#speedboat-skins--2026-10-24)
records the current review, before/after gallery and verification evidence.
Each livery now has its matching Nordic-cross flag; the accepted staff and cloth simulation
remain in place.
CPU and GPU use the same meshes, including reflections, shadows, replay and ghost views.
Nearest-neighbour presentation remains unchanged.

The owning status and acceptance record is in [the boating plan](BOATING_PLAN.md#speedboat-skins--2026-10-24).
The 2026-10-20 cabin refinement proposal is superseded by this speedboat direction.

## Try the four liveries

| Boat / local ID | Selection | Boat colors | Flag |
| --- | --- | --- | --- |
| 1 / 0 (default) | `sweden` | White hull, blue deck, yellow stripe | Yellow cross on blue |
| 2 / 1 | `finland` | White hull/deck, blue stripe | Blue cross on white |
| 3 / 2 | `denmark` | White hull, red deck, white stripe | White cross on red |
| 4 / 3 | `norway` | White hull, red deck, navy stripe | White-bordered navy cross on red |

From the worktree root:

```sh
./build/wake_velocity --skin finland --location river --waves --gpu-aa
./build/wake_velocity --skin denmark --renderer cpu --screenshot build/skin-follow.png --time 5
./build/boat_flag_review --skin sweden --angle front --screenshot build/skin-front.png
./build/boat_flag_review --skin norway --angle side --low-resolution --screenshot build/skin-side.png
./build/boat_flag_review --skin finland --renderer gpu --scenario turn --frames 180
```

`--skin` selects the local appearance, including when viewing a saved replay. It is not
saved in recordings or transmitted to other clients. The review tool also accepts
`--angle front|side|rear` (default rear) and the existing idle/cruise/turn/reverse scenarios.
GPU modes need a desktop graphics context; CPU screenshots do not open a window.

## Customization boundary

`src/boat_skin.[ch]` belongs to the presentation library. `BoatSkinPalette` exposes hull,
deck, accent and upholstery colors; `boat_skin_generate` builds caller-owned geometry
for a palette, while `boat_skin_mesh` caches the four immutable presets for rendering.
The legacy `waterfront_player_*_mesh` accessors return Sweden for existing review callers.
The existing pose transforms remain intact; the legacy `cabin` transform now carries
the cockpit and windscreen.

Set `BoatViewState.skin` after constructing a view. The selected ID flows into
`WaterfrontPlayerState.skin`; GPU ghost views use `WaterfrontSceneFrame.ghost_skin`.
Zero selects Sweden and unknown IDs fall back to Sweden. Each actor can select its own preset
without recoloring another actor's cached mesh. Custom palettes currently require a code
change and GPU upload; there is no palette editor or runtime arbitrary-color picker.

The same view ID selects the national flag in CPU and GPU, including ghosts. Render scratch
for `boat_flag_skin_mesh` is caller-owned, so drawing another country cannot recolor a retained
mesh. GPU flag colors refresh along with vertices, including when a mesh is reused for a new
skin. Nordic-cross artwork is fitted to the common 0.64 × 0.40 m cloth canvas; country-specific
fabric proportions are not simulated. Render-only subdivisions preserve Norway's white border
without changing the 54 simulation particles, forces or staff attachment.

All presets have identical vertices and topology. No skin enters `Boat`, physics,
contacts, thrust, wake generation, audio tuning, course rules or network serialization.
The hull shape suggests speed without changing speed or introducing another gameplay
class. The two outboards share the original steering rotation at separate hull-relative mounts;
they do not add simulated thrust or mass. The flag keeps its world-space
attachment and simulation. Harbor/traffic boat recipes are unchanged.

In multiplayer (`wake_velocity --connect`) every client draws room slot 1 as Sweden, 2 Finland,
3 Denmark and 4 Norway, through the fleet renderer (`BoatRivals`, `boat_draw_scene`,
`boat_gpu_draw_scene`); `--skin` is refused there. Choosing a country independently of the
slot and persistent loadouts would need room metadata and are not implemented. See the
[fleet rendering handoff](MULTIPLAYER_PLAN.md#m4-step-4a-fleet-rendering-handoff--2026-10-24).

## Verification

`validate_boat_skin` checks matching geometry across all four palettes, nondegenerate and
finite triangles, the existing footprint, separate twin mounts through steering and hull
rotation, arbitrary palette generation, name parsing and
invalid-ID fallback. Flag checks additionally probe the field and cross colors, all four
sides of Norway's white border, matching topology, independent storage and unchanged source cloth. The normal core-boundary gates still enforce the simulation/rendering
separation. See the owning plan for the dated build, full-suite and visual review results.

The baseline speedboat shape and Nordic color/flag pass are user-accepted; the current
speed profile and twin-outboard extension are also user-accepted. The
[integration handoff](BOAT_SKINS_HANDOFF.md) records the accepted package and preparation
for the current main branch. Hands-on driving, Windows and browser review remain unverified.
Scripted desktop playback is a smoke check, not a manual driving or audio assessment.
