# Marina route demo

This voyage package places a runabout and a small floating platform in the outer map's open
river reach north of the low bridge. The runabout approaches, turns toward the platform,
pauses and turns away. The shared simulation clock drives this four-piece open path:

| Seconds | Action |
| --- | --- |
| 0–5 | Straight approach from `(0,170)` to `(0,185)` |
| 5–10 | First turn, ending at `(10,195)` |
| 10–12 | Slow two-metre approach to `(12,195)` |
| 12–14 | Stationary dwell |
| 14–19 | Departure turn, ending at `(22,185)` |
| After 19 | Hold the terminal pose |

The existing upstream ferry services and bridge-lift schooner are retained. The package also
retains two river craft; the other two river craft are omitted to stay within the eight-actor
limit. The visitor's path is clear of the low bridge, so the elevated side view below keeps
its full hull and the platform visible through both turns and the dwell.

From the repository root, validate and run the full windowed review:

```sh
python3 tools/plugin_author.py validate plugins/marina-route-demo
./build/wake_velocity --plugin plugins/marina-route-demo --demo --demo-idle \
  --start -5,185,90 --camera-distance 16 --camera-height 30 --fixed-fov \
  --no-vsync --frames 1800
```

`--demo-idle` sends neutral helm input while traffic, waves and the simulation clock continue.
On this calm-water fixture the viewing boat stays still. The 60 Hz pace gives this bounded
run about 30 seconds, covering departure and the terminal hold. Omit `--demo --demo-idle` and
`--frames 1800` to drive normally from the same view.

For deterministic CPU/GPU stills, use simulated seconds with the same placement and camera:

```sh
./build/wake_velocity --plugin plugins/marina-route-demo --demo-idle \
  --start -5,185,90 --camera-distance 16 --camera-height 30 --fixed-fov \
  --renderer cpu --time 13 --screenshot build/marina-dwell-cpu.png
./build/wake_velocity --plugin plugins/marina-route-demo --demo-idle \
  --start -5,185,90 --camera-distance 16 --camera-height 30 --fixed-fov \
  --renderer gpu --time 13 --screenshot build/marina-dwell-gpu.png
```

GPU capture needs a graphics context; CPU capture needs no window. For boundary views, use
`--time` equal to `tick / 120`: ticks 599/600/601 bracket the first turn, 1199/1200/1201 its
end, 1439/1440/1441 the dwell start, 1679/1680/1681 the departure, and 2279/2280/2281 the
terminal hold. Times 7.5, 13 and 16.5 show the middle of the two turns and the dwell.

This is a route and clearance example with a platform approach, not a berth-occupancy schedule.
At dwell the hull remains about four metres from the platform edge because the conservative
swept-envelope checks still apply. It does not make contact or moor; there are no passengers
or automatic ropes. Named berths and vessel handoffs belong to M4. Depth is not surveyed.

The [API](../../docs/PLUGIN_API.md#location-life-scene-contract-l11l43) owns the route fields,
join/motion bounds and conservative schedule-proof limits. Route edits require held Apply
and restart the session; Undo restores the prior complete route under the same rule.
Native, browser-worker staging and room data carry the same checked path.

[Recorded CPU/GPU and windowed review](../../docs/evidence/marina-c2b/2026-11-14/README.md)
and [native/browser/Wasm qualification](../../docs/evidence/marina-c3/2026-11-14/README.md)
provide agent evidence. Human acceptance, depth and berth occupancy remain separate.
