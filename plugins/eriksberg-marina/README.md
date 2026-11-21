# Eriksberg marina

An agent-authored rebuild of the marina along the east quay of the Eriksberg basin (location `outer`).

- Seven pontoon sections off the east quay: three north of the Dockbron footbridge (z 236, 199, 161)
  and four south of it (z 94, 48, -6, -62). The southernmost has boats on its north side only.
- 32 berths and 63 modules, the schema caps. That leaves 2-3 boats per side, not the ~5 of the real marina.
- Two resident vessels (`ruth`, `nore`) berthed alongside the south side of the last section. They
  leave 6 s and 25 s after the start, make a lap at the basin mouth or in the river, and return.
  Services are one-shot and routes cannot reverse, so `ruth`'s lap is small.
- The ferry, schooner and gull flock are retained; the looping `passing` and `river_runabout` craft are removed.

Section spacing is scaled from a photo; the footbridge (z about 125) and crane (z about 3) are estimates.
Not played or accepted by the user beyond the first look.

```sh
./build/wake_velocity --plugin plugins/eriksberg-marina --start -10,280,0
```

Authoring tools: [PLUGIN_API.md](../../docs/PLUGIN_API.md#map-query).
