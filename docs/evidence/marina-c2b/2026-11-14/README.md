# Marina C2b visual evidence — 2026-11-14

Apple Silicon, macOS 26.6.2, native Debug, low 320×200 render size. This is agent visual
review evidence, with independent review of the deterministic captures. It does not establish
human acceptance or browser/Windows/Linux qualification. The [marina plan](../../../MARINA_AUTHORING_PLAN.md)
owns status and limitations; the [package guide](../../../../plugins/marina-route-demo/README.md)
owns the short route schedule and launch commands.

- [CPU sequence](cpu-sequence.png) and [GPU sequence](gpu-sequence.png): 19 frames each.
  The filename labels give the exact 120 Hz simulation tick. Read left to right, top to bottom.
- [Window recording](window-gpu-full.mp4): 24 seconds of the normal GPU window, including HUD.
  Captured from the game window only, resized to 960 pixels wide and H.264 encoded; no audio.
- [Window recording samples 1](motion-sheet-0.png), [2](motion-sheet-1.png),
  [3](motion-sheet-2.png), [4](motion-sheet-3.png): all 96 samples at 4 fps from the recording.
  Labels are recording frame numbers, not simulation ticks. Each page covers about six seconds.
  These samples were inspected across the whole recording, including both turns and the dwell.
- [Final window](window-final.png): the terminal pose after the bounded run.
- [Location validation](validate.json) and [manifest](manifest.json): final bundle validation,
  camera/tick options and artifact checksums.

The full visitor hull and platform remain visible. The viewing boat stays still; the visitor
turns, slows, dwells, departs and holds its terminal pose. CPU/GPU positions and headings agree
visually; existing GPU reflections/shadows differ from CPU. Adjacent-tick captures have no
visible jump at the sampled joins, dwell boundaries or terminal hold. The windowed HUD clears
the whole route. The run completed with exit 0, 3,611 simulation ticks and zero restarts.
The first recording frame follows window startup; tick-zero stills cover the initial pose.

The paused hull remains about four metres from the platform edge. This is an approach/pause/
departure example under conservative clearance checks, with no contact, ropes or berth handoff.
M4 owns named berths and occupancy. The review does not assess audio or depth.

The screenshot batches used the following standard-library Python command from the repository
root (GPU needs desktop access):

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess

output = Path("build/marina-c2b")
output.mkdir(parents=True, exist_ok=True)
ticks = (0, 599, 600, 601, 900, 1199, 1200, 1201, 1439, 1440, 1441,
         1560, 1679, 1680, 1681, 1980, 2279, 2280, 2281)
for renderer in ("cpu", "gpu"):
    for tick in ticks:
        subprocess.run([
            "./build/wake_velocity", "--plugin", "plugins/marina-route-demo",
            "--demo-idle", "--start", "-5,185,90", "--camera-distance", "16",
            "--camera-height", "30", "--fixed-fov", "--renderer", renderer,
            "--time", str(tick / 120), "--screenshot",
            str(output / f"{renderer}-{tick:04}.png"),
        ], check=True)
PY
```

The windowed run used:

```sh
./build/wake_velocity --plugin plugins/marina-route-demo --demo --demo-idle \
  --start -5,185,90 --camera-distance 16 --camera-height 30 --fixed-fov \
  --no-vsync --frames 1800 --frame-capture build/marina-window-final.png
```

On this Mac, `screencapture -x -l WINDOW_ID -v -V 24 FILE.mov` recorded only that process's
window. The recording was sampled with `ffmpeg -i FILE.mov -vf fps=4,scale=320:200:flags=neighbor
motion-%03d.png`. The samples preserve their order but include window borders and are resized
for compact review. The native screenshot sheets preserve each 320×200 image without resampling;
only filename captions were added.
