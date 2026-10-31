#!/usr/bin/env python3
"""PD2/PD3 source guards: no networked client path reads the compiled river course, and the
engine digest no longer carries the authored harbour contact values. The room definition is the
only course and contact source a client binds; the solo race trial passes its course to the view
explicitly."""
import pathlib
import sys

FILES = (
    "src/boat_net_client.c",
    "src/boat_net_protocol.c",
    "src/boat_prediction.c",
    "src/boat_view.c",
    "tools/wake_velocity_net.c",
    "tools/boat_bot.c",
)
NEEDLE = "boat_race_river_course"


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    failures = []
    for name in FILES:
        text = (root / name).read_text(encoding="utf-8")
        if NEEDLE in text:
            failures.append(name)
    if failures:
        print("compiled river course referenced by: " + ", ".join(failures))
        return 1
    digest = (root / "src/boat_net_protocol.c").read_text(encoding="utf-8")
    for needle in ("waterfront_harbor_berths", "waterfront_harbor_ferry"):
        if needle in digest:
            print(f"engine digest still reads {needle}")
            return 1
    print("room_course_source=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
