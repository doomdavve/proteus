#!/usr/bin/env python3
"""Keep the common plugin staging code free of platform and JavaScript bridges.

src/boat_plugin_stage.c and its public and private headers are shared by every platform, so
they must not include OS headers or name OS/JavaScript identifiers. Platform worker code lives
only in the posix, windows and web backends, each of which may include just its own headers.
The private backend header stays private to the common file and the three backends, and
CMakeLists.txt selects exactly one backend file per platform.
"""

import argparse
import re
import sys
from pathlib import Path

COMMON_FILES = ("src/boat_plugin_stage.c", "src/boat_plugin_stage.h",
                "src/boat_plugin_stage_backend.h")
BACKEND_FILES = ("src/boat_plugin_stage_posix.c", "src/boat_plugin_stage_windows.c",
                 "src/boat_plugin_stage_web.c")
PRIVATE_HEADER = '#include "boat_plugin_stage_backend.h"'

COMMON_HEADERS = ("windows.h", "io.h", "fcntl.h", "unistd.h", "spawn.h", "signal.h",
                  "emscripten.h")
COMMON_IDENTIFIERS = re.compile(
    r"\b(EM_JS|EM_ASM|__EMSCRIPTEN__|_WIN32|posix_spawn|waitpid|kill|CreateProcessA|"
    r"CreateProcess|TerminateProcess|DuplicateHandle|HANDLE|pid_t|_setmode|Module)\b")
INCLUDE = re.compile(r'#include\s*[<"]([^>"]+)[>"]')

POSIX_HEADERS = ("windows.h", "io.h", "emscripten.h")
WINDOWS_HEADERS = ("spawn.h", "unistd.h", "signal.h", "sys/wait.h", "emscripten.h")
WEB_HEADERS = ("windows.h", "io.h", "spawn.h", "unistd.h", "signal.h")
BACKEND_HEADERS = {
    "src/boat_plugin_stage_posix.c": POSIX_HEADERS,
    "src/boat_plugin_stage_windows.c": WINDOWS_HEADERS,
    "src/boat_plugin_stage_web.c": WEB_HEADERS,
}


def strip_comments(text):
    """Replace // and /* */ comments with spaces, preserving line breaks and offsets."""
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group())
    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def includes(path, text):
    for number, line in enumerate(text.splitlines(), 1):
        for header in INCLUDE.findall(line):
            yield number, header


def check_common(root):
    problems = []
    for name in COMMON_FILES:
        path = root / name
        if not path.is_file():
            problems.append(f"{name}: missing common plugin staging file")
            continue
        code = strip_comments(path.read_text(encoding="utf-8"))
        for number, header in includes(name, code):
            if header.startswith("sys/") or header in COMMON_HEADERS:
                problems.append(f"{name}:{number}: common staging includes platform header "
                                f"<{header}>")
        for number, line in enumerate(code.splitlines(), 1):
            for match in COMMON_IDENTIFIERS.finditer(line):
                problems.append(f"{name}:{number}: common staging names platform identifier "
                                f"{match.group(0)}")
    return problems


def check_backends(root):
    problems = []
    for name, forbidden in BACKEND_HEADERS.items():
        path = root / name
        if not path.is_file():
            problems.append(f"{name}: missing platform backend file")
            continue
        code = strip_comments(path.read_text(encoding="utf-8"))
        for number, header in includes(name, code):
            if header in forbidden or (name == "src/boat_plugin_stage_web.c" and
                                       header.startswith("sys/")):
                problems.append(f"{name}:{number}: backend includes foreign header <{header}>")
    return problems


def check_private_header(root):
    allowed = set(COMMON_FILES[:1]) | set(BACKEND_FILES)
    problems = []
    for directory in ("src", "tools"):
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.glob("*.c")) + sorted(base.glob("*.h")):
            name = path.relative_to(root).as_posix()
            if PRIVATE_HEADER in path.read_text(encoding="utf-8") and name not in allowed:
                problems.append(f"{name}: includes the private plugin staging backend header")
    return problems


def check_cmake(root):
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    problems = []
    for name in BACKEND_FILES:
        count = len(re.findall(re.escape(name), cmake))
        if count != 1:
            problems.append(f"CMakeLists.txt: names {name} {count} times, expected exactly once")
    return problems


def check(root):
    return (check_common(root) + check_backends(root) + check_private_header(root) +
            check_cmake(root))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, required=True, help="repository root to check")
    args = parser.parse_args()
    problems = check(args.root)
    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    print("plugin_stage_backend=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
