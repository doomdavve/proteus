#!/usr/bin/env python3
"""Keep browser bridges and platform service backends out of shared application code (AR3).

Rule 9 of docs/ARCHITECTURE_PLAN.md: platform code sits behind small interfaces in CMake-selected
files. Two checks over src/ and tools/:

1. Inline JavaScript (EM_JS, EM_ASYNC_JS, EM_ASM) appears only in browser implementation files
   (*_web.c) or in a file listed in LEGACY with the reason it still has bridges. A legacy file
   may not gain bridges beyond its recorded count, so the list only shrinks.
2. Files migrated onto a service do not reach the service's backends directly (MIGRATED).

--self-test runs both checks against synthetic violations and fails unless they are caught.
"""

import argparse
import re
import sys
from pathlib import Path

BRIDGE = re.compile(r"\b(EM_JS|EM_ASYNC_JS|EM_ASM\w*)\s*\(")

# File -> (bridges allowed, owner of their extraction).
LEGACY = {
    "tools/wake_velocity.c": (17, "AR2b: viewport, canvas, touch, mouse and keyboard capabilities"),
    "tools/wake_console.c": (5, "AR2b/AR3: console text entry, paste and notices"),
    "tools/boat_plugin_stage_worker.c": (1, "AR4: plugin worker backends"),
    "src/boat_plugin_stage.c": (5, "AR4: plugin worker backends"),
}

# File -> forbidden patterns: the backends its services own.
MIGRATED = {
    "tools/boat_title.c": [
        r"#\s*include\s*[<\"]emscripten", r"#\s*include\s*\"boat_directory\.h\"",
        r"#\s*include\s*\"boat_net_enet\.h\"", r"\bboat_directory_\w+\s*\(",
        r"\bboat_net_enet_\w+\s*\(", r"\bboat_net_lan_(search|query|receive|close)\s*\(",
        r"boatServers",
    ],
    "tools/wake_velocity.c": [
        r"boatScenery", r"boatServers", r"boatPlugin", r"\bemscripten_get_now\s*\(",
        r"\bboat_scenery_load_all\s*\(", r"\bboat_directory_\w+\s*\(",
    ],
    "tools/wake_velocity_plugin.c": [
        r"#\s*include\s*[<\"]emscripten", r"\bModule\s*[.\[]", BRIDGE.pattern,
    ],
}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def check(files):
    """FILES maps a repository-relative path to its text. Returns the problems found."""
    problems = []
    for path, text in sorted(files.items()):
        code = strip_comments(text)
        count = len(BRIDGE.findall(code))
        if count and not path.endswith("_web.c"):
            allowed = LEGACY.get(path, (0, ""))[0]
            if count > allowed:
                problems.append(f"{path}: {count} inline JavaScript bridges, {allowed} allowed; "
                                "put new bridges in a CMake-selected *_web.c backend")
        for pattern in MIGRATED.get(path, []):
            if re.search(pattern, code):
                problems.append(f"{path}: reaches a service backend directly ({pattern})")
    return problems


def self_test():
    cases = {
        "tools/new_feature.c": 'EM_JS(int, f, (void), { return 1; })\n',
        "tools/boat_title.c": '#include "boat_directory.h"\n',
        "tools/wake_velocity.c": "double t = emscripten_get_now();\n",
        "tools/wake_velocity_plugin.c": "EM_JS(void, g, (void), { Module.x = 1; })\n",
    }
    caught = {problem.split(":")[0] for problem in check(cases)}
    missed = sorted(set(cases) - caught)
    ok = check({"tools/new_web.c": 'EM_JS(int, f, (void), { return 1; })\n',
                "tools/boat_title.c": "// boat_directory_refresh() moved\n"}) == []
    if missed or not ok:
        print(f"platform_bridges self-test failed: missed {missed}, clean={ok}")
        return 1
    print("platform_bridges_self_test=ok")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    files = {}
    for directory in ("src", "tools"):
        for path in sorted((args.root / directory).rglob("*.[ch]")):
            files[path.relative_to(args.root).as_posix()] = path.read_text(errors="replace")
    for path in list(LEGACY) + list(MIGRATED):
        if path not in files:
            print(f"platform_bridges: {path} is listed but missing; update the lists")
            return 1
    problems = check(files)
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print("platform_bridges=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
