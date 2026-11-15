# Marina berth demo

An M4 example of plugin-authored vessels and berth services on the outer map's open river reach
north of the low bridge. Two empty berths (`home`, `slip`) sit beside two floating platforms.
Two vessels use them on the shared simulation clock (120 ticks per second):

| Seconds | Resident `tender` (berth `home`) | Visitor `guest` (berth `slip`) |
| --- | --- | --- |
| 0–2 | Moored | Absent |
| 2–14 | Moored until 5; then a 20 s racetrack | Arrives: line, turn, slow approach |
| 14–16 | Racetrack | Moored at the slip (dwell) |
| 16–21 | Racetrack | Departure turn |
| 21–25 | Racetrack until 25 | Absent again |
| After 25 | Moored | Absent |

Each vessel is one hull with one owner at every tick: the berth owns it while moored, its route
owns it while travelling, and a visitor has no hull outside its window. The services are one-shot.
The berth is the single source of the terminal pose; if a path ends elsewhere, `validate` prints
the expected berth pose. Moving the visitor's `start_tick` to 360 makes the two paths cross, which
the pairwise survey refuses (`scene.traffic.vessels[2] crosses scene.traffic.vessels[1]`).

```sh
python3 tools/plugin_author.py validate plugins/marina-berth-demo
./build/wake_velocity --plugin plugins/marina-berth-demo --demo --demo-idle \
  --start -2,190,90 --camera-distance 30 --camera-height 30 --fixed-fov --no-vsync --frames 3300
./build/wake_velocity --plugin plugins/marina-berth-demo --demo-idle \
  --start -2,190,90 --camera-distance 36 --camera-height 36 --fixed-fov \
  --renderer cpu --time 15 --screenshot build/marina-stay-cpu.png
```

Use `--time` equal to `tick / 120` for handoff views: 119/120/121 s-ticks around 600 (tender
departs), 1679/1680 (visitor moors), 1919/1920 (visitor leaves), 2999/3000 (tender re-moors).
Hull colours show from above through `deck`; the `hull` colour is the side.

Not covered: serviced vessels have no visual wake, a parked visitor and repeat are deferred, and
depth is not surveyed. The [API](../../docs/PLUGIN_API.md) owns the fields. A service timetable
edit needs held Apply and restarts the session; Undo restores the previous one.
[Dated evidence](../../docs/evidence/marina-m4/2026-11-15/README.md).
