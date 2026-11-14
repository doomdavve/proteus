# Marina route demo

This native voyage package places a runabout and a small floating platform on the outer map.
The runabout approaches, turns toward the platform, pauses and turns away. Its four-piece open
path uses the shared simulation clock: the dockside dwell is from 12 to 14 seconds and the
departure ends at 19 seconds. The existing upstream ferry services and bridge-lift schooner are
retained. The package also retains two river craft; the other two river craft are omitted to
stay within the eight-actor limit.

From the repository root, validate and launch it with:

```sh
python3 tools/plugin_author.py validate plugins/marina-route-demo
./build/wake_velocity --plugin plugins/marina-route-demo --start 0,150,0
```

This is a route and clearance example, not a berth-occupancy schedule. The platform is a simple
stand-in for a dock; there are no passengers or automatic mooring ropes. Depth is not surveyed.
