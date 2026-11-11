#!/usr/bin/env python3
"""Keep the common plugin staging code free of platform and JavaScript bridges.

src/plugins/boat_plugin_stage.c and its public and private headers are shared by every platform, so
they must not include OS headers or name OS/JavaScript identifiers. Platform worker code lives
only in the posix, windows and web backends, each of which may include just its own headers.
The private backend header stays private to the common file and the three backends, and
CMakeLists.txt selects exactly one backend file per platform. Comments and string literals are
blanked before identifier matching, and --self-test plants violations in a temporary copy.
"""

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path

COMMON_FILES = ("src/plugins/boat_plugin_stage.c", "src/plugins/boat_plugin_stage.h",
                "src/plugins/boat_plugin_stage_backend.h")
BACKEND_FILES = ("src/plugins/boat_plugin_stage_posix.c", "src/plugins/boat_plugin_stage_windows.c",
                 "src/plugins/boat_plugin_stage_web.c")
PRIVATE_HEADER = "boat_plugin_stage_backend.h"
PRIVATE_HEADER_ALLOWED = set(COMMON_FILES[:1]) | set(BACKEND_FILES) | {
    "tests/plugins/validate_boat_plugin_stage_web.c"}

COMMON_HEADERS = ("windows.h", "io.h", "fcntl.h", "unistd.h", "spawn.h", "signal.h",
                  "emscripten.h")
COMMON_IDENTIFIERS = re.compile(
    r"\b(EM_[A-Z_]+|emscripten_\w+|CreateProcess\w*|posix_spawn|waitpid|kill|"
    r"TerminateProcess|DuplicateHandle|HANDLE|pid_t|_setmode|Module|"
    r"__linux__|__APPLE__|_MSC_VER|_WIN32|__EMSCRIPTEN__)\b")
COMMON_CALLS = re.compile(r"\b(system|popen)\s*\(")
INCLUDE = re.compile(r'#include\s*[<"]([^>"]+)[>"]')

POSIX_HEADERS = ("windows.h", "io.h", "emscripten.h")
WINDOWS_HEADERS = ("spawn.h", "unistd.h", "signal.h", "sys/wait.h", "emscripten.h")
WEB_HEADERS = ("windows.h", "io.h", "spawn.h", "unistd.h", "signal.h")
BACKEND_HEADERS = {
    "src/plugins/boat_plugin_stage_posix.c": POSIX_HEADERS,
    "src/plugins/boat_plugin_stage_windows.c": WINDOWS_HEADERS,
    "src/plugins/boat_plugin_stage_web.c": WEB_HEADERS,
}

SELF_TEST_APPENDS = (
    ("src/plugins/boat_plugin_stage.c", '#include <unistd.h>'),
    ("src/plugins/boat_plugin_stage.c", '#include <sys/wait.h>'),
    ("src/plugins/boat_plugin_stage.c", "EM_ASYNC_JS(int, f, (void), {})"),
    ("src/plugins/boat_plugin_stage.c", "static int x = emscripten_get_now;"),
    ("src/plugins/boat_plugin_stage.c", "static void *p = CreateProcessW;"),
    ("src/plugins/boat_plugin_stage.c", 'static int y(void) { return system("x"); }'),
    ("src/plugins/boat_plugin_stage.c", 'static void *q(void) { return popen("x", "r"); }'),
    ("src/plugins/boat_plugin_stage.c", "#if defined(__linux__)\n#endif"),
    ("src/plugins/boat_plugin_stage.c", "#ifdef __APPLE__\n#endif"),
    ("src/plugins/boat_plugin_stage.c", "#if _MSC_VER\n#endif"),
    ("src/plugins/boat_plugin_stage.c", "#ifdef _WIN32\n#endif"),
    ("src/plugins/boat_plugin_stage.c", "static pid_t z;"),
    ("src/plugins/boat_plugin_stage_posix.c", '#include <windows.h>'),
    ("src/plugins/boat_plugin_stage_windows.c", '#include <spawn.h>'),
    ("src/plugins/boat_plugin_stage_web.c", '#include <sys/types.h>'),
)

SELF_TEST_ACCEPTED = ("// waitpid _WIN32 EM_JS\n"
                      'static const char *s = "system( waitpid _WIN32";')


def strip_comments(text):
    """Replace // and /* */ comments with spaces, preserving line breaks and offsets."""
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group())
    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def strip_code(text):
    """Blank comments and string/char literals with spaces, preserving line breaks."""
    out = list(text)
    limit = len(text)

    def blank(start, end):
        for index in range(start, end):
            if out[index] != "\n":
                out[index] = " "

    index = 0
    while index < limit:
        current = text[index]
        following = text[index + 1] if index + 1 < limit else ""
        if current == "/" and following == "/":
            end = text.find("\n", index)
            end = limit if end < 0 else end
            blank(index, end)
        elif current == "/" and following == "*":
            end = text.find("*/", index + 2)
            end = limit if end < 0 else end + 2
            blank(index, end)
        elif current in "\"'":
            end = index + 1
            while end < limit and text[end] != current:
                end += 2 if text[end] == "\\" else 1
            end = min(end + 1, limit)
            blank(index, end)
        else:
            index += 1
            continue
        index = end
    return "".join(out)


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
        text = path.read_text(encoding="utf-8")
        commented = strip_comments(text)
        code = strip_code(text)
        for number, header in includes(name, commented):
            if header.startswith("sys/") or header in COMMON_HEADERS:
                problems.append(f"{name}:{number}: common staging includes platform header "
                                f"<{header}>")
        for number, line in enumerate(code.splitlines(), 1):
            for match in COMMON_IDENTIFIERS.finditer(line):
                problems.append(f"{name}:{number}: common staging names platform identifier "
                                f"{match.group(1)}")
            for match in COMMON_CALLS.finditer(line):
                problems.append(f"{name}:{number}: common staging calls platform process API "
                                f"{match.group(1)}")
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
            if header in forbidden or (name == "src/plugins/boat_plugin_stage_web.c" and
                                       header.startswith("sys/")):
                problems.append(f"{name}:{number}: backend includes foreign header <{header}>")
    return problems


def check_private_header(root):
    problems = []
    for directory in ("src", "apps", "tests", "tools"):
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.c")) + sorted(base.rglob("*.h")):
            if not path.is_file():
                continue
            name = path.relative_to(root).as_posix()
            code = strip_comments(path.read_text(encoding="utf-8"))
            for number, header in includes(name, code):
                if header != PRIVATE_HEADER:
                    continue
                if name not in PRIVATE_HEADER_ALLOWED:
                    problems.append(f"{name}:{number}: includes the private plugin staging "
                                    "backend header")
                break
    return problems


def target_blocks(cmake):
    for match in re.finditer(r"target_sources\(\s*proteus_plugins", cmake):
        end = cmake.find(")", match.end())
        yield cmake[match.end():] if end < 0 else cmake[match.end():end]


CMAKE_FILE = "src/plugins/CMakeLists.txt"  # Names its own sources relative to src/plugins.


def check_cmake(root):
    cmake = (root / CMAKE_FILE).read_text(encoding="utf-8")
    problems = []
    blocks = list(target_blocks(cmake))
    for name in BACKEND_FILES:
        local = name.rsplit("/", 1)[-1]
        count = sum(len(re.findall(r"(?<![\w/.-])" + re.escape(local) + r"(?![\w.])", block))
                    for block in blocks)
        if count != 1:
            problems.append(f"{CMAKE_FILE}: names {local} {count} times, expected exactly once")
    return problems


def check(root):
    return (check_common(root) + check_backends(root) + check_private_header(root) +
            check_cmake(root))


def append_text(path, snippet):
    text = path.read_text(encoding="utf-8")
    if text and not text.endswith("\n"):
        text += "\n"
    path.write_text(text + snippet + "\n", encoding="utf-8")


def copy_tree(root, destination):
    # Only the files the checks read or allow; planted cases add any other includer.
    names = set(COMMON_FILES) | set(BACKEND_FILES) | PRIVATE_HEADER_ALLOWED | {CMAKE_FILE}
    for name in sorted(names):
        source = root / name
        if not source.is_file():
            continue
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def self_test(root):
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary) / "base"
        copy_tree(root, base)
        problems = check(base)
        if problems:
            print("plugin_stage_backend self-test failed: clean copy is not clean", file=sys.stderr)
            for problem in problems:
                print(problem, file=sys.stderr)
            return 1

        cases = [(f"{name}: {snippet.splitlines()[0]}",
                  lambda destination, name=name, snippet=snippet:
                  append_text(destination / name, snippet))
                 for name, snippet in SELF_TEST_APPENDS]
        cases.append(("apps/x/other.c includes the private backend header",
                      lambda destination: (
                          (destination / "apps" / "x").mkdir(parents=True, exist_ok=True),
                          (destination / "apps" / "x" / "other.c").write_text(
                              '#include "boat_plugin_stage_backend.h"\n', encoding="utf-8"))))
        cases.append(("CMakeLists.txt duplicates the posix backend",
                      lambda destination: append_text(
                          destination / CMAKE_FILE,
                          "target_sources(proteus_plugins PRIVATE boat_plugin_stage_posix.c)")))

        for index, (label, plant) in enumerate(cases):
            case = Path(temporary) / f"case{index}"
            shutil.copytree(base, case)
            plant(case)
            if not check(case):
                print(f"plugin_stage_backend self-test failed: planted case not caught: {label}")
                return 1

        accepted = Path(temporary) / "accepted"
        shutil.copytree(base, accepted)
        append_text(accepted / "src" / "plugins" / "boat_plugin_stage.c", SELF_TEST_ACCEPTED)
        problems = check(accepted)
        if problems:
            print("plugin_stage_backend self-test failed: comment or string literal was rejected",
                  file=sys.stderr)
            for problem in problems:
                print(problem, file=sys.stderr)
            return 1

    print("plugin_stage_backend self-test=ok")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, required=True, help="repository root to check")
    parser.add_argument("--self-test", action="store_true",
                        help="plant violations in a temporary copy and confirm they are caught")
    args = parser.parse_args()
    if args.self_test:
        return self_test(args.root)
    problems = check(args.root)
    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    print("plugin_stage_backend=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
