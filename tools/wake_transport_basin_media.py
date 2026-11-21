#!/usr/bin/env python3
"""Capture matched closed-loop and frozen-encounter CPU basin movies."""
import argparse
from pathlib import Path
import subprocess
from wake_geometry import banner, save, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = args.output / "manifest.json"
    if manifest.exists():
        raise ValueError("Use a fresh output directory")
    sources = ["tools/wake_transport_basin.c", "tools/wake_transport.c", "tools/wake_transport.h",
               "tools/wake_capture.c", "tools/wake_capture.h", "tools/wake_geometry.c",
               "tools/wake_geometry.h", "src/core/boat_water.c", "src/core/boat_water.h",
               "src/core/boat.c", "src/render/boat_water_mesh.c", "src/render/boat_gpu.c"]
    report = dict(binary=str(args.binary.resolve()), binary_sha256=sha(args.binary),
                  sources={p: sha(Path(p)) for p in sources}, driver_sha256=sha(Path(__file__)),
                  shared_driver_sha256=sha(Path(__file__).with_name("wake_geometry.py")), runs=[], movies=[])
    header = args.output / "banner.png"
    banner(header, "spread")
    for scenario, ticks in (("self", 1200), ("gap12", 240), ("gap50", 240), ("eight-trough", 240)):
        folders = []
        for mode in ("baseline", "spread"):
            folder = args.output / f"{scenario}-{mode}"
            folder.mkdir()
            command = [str(args.binary.resolve()), "--mode", mode, "--capture", str(folder),
                       "--ticks", str(ticks)]
            if scenario != "self":
                command.extend(("--encounter", scenario))
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            frames = sorted(folder.glob("*.png"))
            assert [p.name for p in frames] == [f"{i:04d}.png" for i in range(ticks // 12)]
            report["runs"].append(dict(scenario=scenario, mode=mode, ticks=ticks, command=command,
                                       output=result.stdout, frames={str(p.resolve()): sha(p) for p in frames}))
            folders.append(folder)
        movie = args.output / f"{scenario}-paired.mp4"
        command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-framerate", "10",
                   "-i", str(folders[0] / "%04d.png"), "-framerate", "10", "-i", str(folders[1] / "%04d.png"),
                   "-framerate", "10", "-i", str(header), "-filter_complex",
                   "[0:v]scale=640:400:flags=neighbor[a];[1:v]scale=640:400:flags=neighbor[b];"
                   "[a][b]hstack[body];[2:v]crop=1280:32:0:0[caption];[caption][body]vstack[v]",
                   "-map", "[v]", "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p",
                   "-frames:v", str(ticks // 12), str(movie)]
        subprocess.run(command, check=True)
        count = subprocess.check_output(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
                                         "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", str(movie)], text=True).strip()
        assert int(count) == ticks // 12
        report["movies"].append(dict(path=str(movie.resolve()), sha256=sha(movie), frames=int(count),
                                     fps=10, command=command))
        save(manifest, report)
    print(f"manifest: {manifest}")


if __name__ == "__main__":
    main()
