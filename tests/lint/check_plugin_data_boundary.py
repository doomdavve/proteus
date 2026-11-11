#!/usr/bin/env python3
"""Keep the plugin data library and its data-only consumers free of runtime edges.

proteus_plugin_data parses manifests, validates and decodes descriptors, computes digests and
reads the stage wire and objective checkpoint formats over caller-owned bytes. Its data-only
consumers (saves, the compiled catalog and the data validator) must stay equally pure: no Lua
evaluation, files, processes or browser bridges. Comments are stripped before include checks and
comments plus string literals are blanked before identifier and call matching. The CMake source
list of proteus_plugin_data must match DATA_FILES. With --nm and one or more --archive paths, the
first archive's undefined symbols must be defined by the archives or allowed libc/libm/raylib
symbols; each --consumer-archive is checked the same way against all the listed archives.
--self-test plants violations in a temporary copy.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DATA_FILES = ("src/plugins/boat_plugin.c", "src/plugins/boat_plugin.h", "src/plugins/boat_plugin_decode.h",
              "src/plugins/boat_plugin_revision.h", "src/plugins/boat_plugin_race.c",
              "src/plugins/boat_plugin_objective.h", "src/plugins/boat_plugin_objective_codec.c",
              "src/plugins/boat_plugin_wire.c", "src/plugins/boat_plugin_wire.h")
CONSUMER_FILES = ("src/session/boat_sandbox_save.c", "src/session/boat_sandbox_save.h",
                  "src/plugins/boat_plugin_catalog.c", "src/plugins/boat_plugin_catalog.h",
                  "tests/plugins/validate_boat_plugin_data.c")
FILES = DATA_FILES + CONSUMER_FILES
MACHO = sys.platform == "darwin"
TEST_PROGRAM = "tests/plugins/validate_boat_plugin_data.c"

EXECUTION_HEADERS = frozenset({
    "boat_plugin_lua.h", "boat_plugin_load.h", "boat_plugin_host.h", "boat_plugin_io.h",
    "boat_plugin_state.h", "boat_plugin_stage.h", "boat_plugin_stage_backend.h",
    "boat_plugin_eval.h", "boat_file.h", "lua.h", "lauxlib.h", "lualib.h",
})
SYSTEM_HEADERS = frozenset({
    "assert.h", "float.h", "limits.h", "math.h", "stdarg.h", "stdbool.h", "stddef.h",
    "stdint.h", "stdio.h", "stdlib.h", "string.h",
})

QUOTED_INCLUDE = re.compile(r'#include\s*"([^"]+)"')
ANGLE_INCLUDE = re.compile(r"#include\s*<([^>]+)>")
IDENTIFIERS = re.compile(
    r"\b(lua_State|lua_\w+|luaL_\w+|luaopen_\w+|boat_plugin_lua_\w+|boat_plugin_load_\w+|"
    r"boat_plugin_host_\w+|boat_plugin_stage_\w+|boat_plugin_read\w*|"
    r"boat_plugin_objective_step|boat_plugin_objective_migrate|FILE|stdin|stdout|stderr|"
    r"EM_[A-Z_]+|emscripten_\w+|__EMSCRIPTEN__|_WIN32|__APPLE__|__linux__|_MSC_VER)\b")
CALLS = re.compile(
    r"\b(fopen|freopen|fclose|fread|fwrite|fflush|fseek|ftell|rewind|fprintf|printf|"
    r"vprintf|vfprintf|fputs|fputc|puts|putchar|fgets|fgetc|getchar|fscanf|scanf|remove|"
    r"rename|tmpfile|mkdir|rmdir|opendir|readdir|unlink|stat|open|read|write|close|system|"
    r"popen|getenv|posix_spawn|fork|exec\w*|CreateProcess\w*)\s*\(")
TEST_IDENTIFIERS = {"stderr", "stdout"}
TEST_CALLS = {"printf", "fprintf", "puts"}

CMAKE_TARGETS = ("proteus_plugin_data", "proteus_sandbox_save", "proteus_plugin_catalog")
CMAKE_FORBIDDEN = re.compile(r"\b(proteus_lua|proteus_plugins)\b")
CMAKE_FILES = ("CMakeLists.txt", "src/plugins/CMakeLists.txt", "src/session/CMakeLists.txt")
CMAKE_DATA_LIBRARY = re.compile(r"add_library\(\s*proteus_plugin_data\s+STATIC\b")

LIBRARY_SYMBOLS = re.compile(
    r"(?:malloc|calloc|realloc|free|memcpy|memmove|memset|memset_pattern\d+|memcmp|memchr|"
    r"strchr|strrchr|strstr|strpbrk|strcmp|strncmp|strlen|strtod|strtol|strtoul|bzero|qsort|"
    r"snprintf|vsnprintf|__\w*chk\w*|__stack_chk_\w+|__chkstk\w*|_?_assert\w*|abort|"
    r"(?:sin|cos|tan|atan|atan2|sqrt|hypot|floor|ceil|round|trunc|fabs|fmin|fmax|fmod|exp|"
    r"log|pow|lround|lrint|copysign)[fl]?|__\w*isfinite\w*|__sincos[fl]?_stret)")
RAYLIB_SYMBOLS = re.compile(r"(?:Vector[234]|Matrix|Quaternion)\w*")
EXECUTION_SYMBOLS = re.compile(
    r"lua_|luaL_|luaopen_|boat_plugin_lua_|boat_plugin_load_|boat_plugin_host_|fopen|"
    r"posix_spawn|emscripten_")

SELF_TEST_APPENDS = (
    ("src/plugins/boat_plugin.c", '#include "boat_plugin_lua.h"'),
    ("src/plugins/boat_plugin_wire.c", "#include <unistd.h>"),
    ("src/plugins/boat_plugin_race.c", "static lua_State *L;"),
    ("src/session/boat_sandbox_save.c", 'static void f(void) { fopen("x", "r"); }'),
    ("src/plugins/boat_plugin_catalog.c",
     "static void g(void) { boat_plugin_load_voyage(0, 0, 0, 0); }"),
    ("src/plugins/boat_plugin_objective_codec.c", "#ifdef __EMSCRIPTEN__\n#endif"),
)

SELF_TEST_ACCEPTED = ('// lua_State fopen\nstatic const char *s = "lua_State fopen(";')

SELF_TEST_NM = """import sys

path = sys.argv[-1]
options = set(sys.argv[1:-1])
for line in open(path, encoding="utf-8"):
    parts = line.split()
    if len(parts) != 2:
        continue
    kind, name = parts
    if kind == "U" and "-u" in options:
        print(f"                 U {name}")
    elif kind == "T" and "-u" not in options:
        print(f"0000000000000000 T {name}")
"""


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


def quoted_includes(text):
    for number, line in enumerate(text.splitlines(), 1):
        for header in QUOTED_INCLUDE.findall(line):
            yield number, header


def system_includes(text):
    for number, line in enumerate(text.splitlines(), 1):
        for header in ANGLE_INCLUDE.findall(line):
            yield number, header


def check_sources(root):
    problems = []
    for name in FILES:
        path = root / name
        if not path.is_file():
            problems.append(f"{name}: missing plugin data boundary file")
            continue
        text = path.read_text(encoding="utf-8")
        commented = strip_comments(text)
        code = strip_code(text)
        is_test = name == TEST_PROGRAM
        for number, header in quoted_includes(commented):
            if header in EXECUTION_HEADERS:
                problems.append(f"{name}:{number}: data code includes runtime header "
                                f"\"{header}\"")
        for number, header in system_includes(commented):
            if header not in SYSTEM_HEADERS:
                problems.append(f"{name}:{number}: data code includes foreign system header "
                                f"<{header}>")
        for number, line in enumerate(code.splitlines(), 1):
            for match in IDENTIFIERS.finditer(line):
                if is_test and match.group(1) in TEST_IDENTIFIERS:
                    continue
                problems.append(f"{name}:{number}: data code names runtime identifier "
                                f"{match.group(1)}")
            for match in CALLS.finditer(line):
                if is_test and match.group(1) in TEST_CALLS:
                    continue
                problems.append(f"{name}:{number}: data code calls runtime API "
                                f"{match.group(1)}")
    return problems


def target_blocks(cmake, target):
    pattern = re.compile(r"target_link_libraries\(\s*" + re.escape(target) + r"\b")
    for match in pattern.finditer(cmake):
        end = cmake.find(")", match.end())
        yield cmake[match.end():] if end < 0 else cmake[match.end():end]


def check_cmake(root):
    # The data library and its consumers are defined in their modules' CMakeLists.txt files.
    paths = [path for path in CMAKE_FILES if (root / path).is_file()]
    if not paths:
        return ["CMakeLists.txt: missing build files"]
    texts = {path: (root / path).read_text(encoding="utf-8") for path in paths}
    cmake = "\n".join(texts.values())
    problems = []
    defining = [path for path, text in texts.items() if CMAKE_DATA_LIBRARY.search(text)]
    if not defining:
        problems.append("CMakeLists.txt: no add_library for proteus_plugin_data")
    else:
        text = texts[defining[0]]
        library = CMAKE_DATA_LIBRARY.search(text)
        end = text.find(")", library.end())
        sources = text[library.end():] if end < 0 else text[library.end():end]
        prefix = defining[0].rsplit("/", 1)[0] + "/" if "/" in defining[0] else ""
        listed = [name if "/" in name else prefix + name for name in sources.split()]
        for name in listed:
            if name not in DATA_FILES:
                problems.append(f"CMakeLists.txt: proteus_plugin_data lists {name} outside "
                                "DATA_FILES")
        for name in DATA_FILES:
            if name not in listed:
                problems.append(f"CMakeLists.txt: proteus_plugin_data does not list {name}")
    for target in CMAKE_TARGETS:
        blocks = list(target_blocks(cmake, target))
        if not blocks:
            problems.append(f"CMakeLists.txt: no target_link_libraries for {target}")
            continue
        for block in blocks:
            for match in CMAKE_FORBIDDEN.finditer(block):
                problems.append(f"CMakeLists.txt: {target} links {match.group(1)}")
    return problems


def nm_symbols(nm, archive, options):
    command = [nm] if isinstance(nm, str) else list(nm)
    try:
        result = subprocess.run([*command, *options, str(archive)], check=False,
                                capture_output=True, text=True)
    except OSError as error:
        raise SystemExit(f"cannot run {' '.join(command)}: {error}")
    if result.returncode != 0:
        raise SystemExit(f"{' '.join(command)} failed for {archive}: {result.stderr.strip()}")
    for line in result.stdout.splitlines():
        line = line.rstrip()
        if not line or line.endswith(":"):
            continue
        name = line.split()[-1].split("@")[0]
        # Only Mach-O prefixes C symbols with an underscore; ELF names are used as written.
        yield name[1:] if MACHO and name.startswith("_") else name


def archive_problems(nm, archive, defined):
    problems = []
    for name in sorted(set(nm_symbols(nm, archive, ("-u",)))):
        if EXECUTION_SYMBOLS.search(name):
            problems.append(f"{Path(archive).name}: undefined {name} reaches Lua, files "
                            "or browser bridges")
        elif name in defined or LIBRARY_SYMBOLS.fullmatch(name):
            continue
        elif RAYLIB_SYMBOLS.fullmatch(name):
            continue
        else:
            problems.append(f"{Path(archive).name}: undefined {name} is neither defined "
                            "by the allowed archives nor in the libc/libm allowlist")
    return problems


def check_archives(nm, archives, consumers=()):
    defined = set()
    for archive in list(archives) + list(consumers):
        defined |= set(nm_symbols(nm, archive, ("-g", "--defined-only")))
    problems = []
    for archive in [archives[0], *consumers]:  # Core and JSON only supply definitions.
        problems += archive_problems(nm, archive, defined)
    return problems


def check(root, nm=None, archives=None, consumers=()):
    problems = check_sources(root) + check_cmake(root)
    if nm and archives:
        problems += check_archives(nm, archives, consumers)
    return problems


def append_text(path, snippet):
    text = path.read_text(encoding="utf-8")
    if text and not text.endswith("\n"):
        text += "\n"
    path.write_text(text + snippet + "\n", encoding="utf-8")


def copy_tree(root, destination):
    # Only the files the checks read; planted cases add any other includer.
    names = set(FILES) | set(CMAKE_FILES)
    for name in sorted(names):
        source = root / name
        if not source.is_file():
            continue
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def self_test(root):
    # The stand-in nm prints names as written, so the self-test fixes the format itself.
    global MACHO
    saved, MACHO = MACHO, False
    try:
        return run_self_test(root)
    finally:
        MACHO = saved


def run_self_test(root):
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary) / "base"
        copy_tree(root, base)
        problems = check(base)
        if problems:
            print("plugin_data_boundary self-test failed: clean copy is not clean",
                  file=sys.stderr)
            for problem in problems:
                print(problem, file=sys.stderr)
            return 1

        cases = [(f"{name}: {snippet.splitlines()[0]}",
                  lambda destination, name=name, snippet=snippet:
                  append_text(destination / name, snippet))
                 for name, snippet in SELF_TEST_APPENDS]
        cases.append(("CMakeLists.txt links the plugin host",
                      lambda destination: append_text(
                          destination / "src" / "session" / "CMakeLists.txt",
                          "target_link_libraries(proteus_sandbox_save PUBLIC proteus_plugins)")))

        def add_data_source(destination):
            path = destination / "src" / "plugins" / "CMakeLists.txt"
            text = path.read_text(encoding="utf-8")
            path.write_text(
                text.replace("boat_plugin_wire.h)", "boat_plugin_wire.h boat_plugin_extra.c)", 1),
                encoding="utf-8")

        cases.append(("CMakeLists.txt adds an extra plugin data source", add_data_source))

        for index, (label, plant) in enumerate(cases):
            case = Path(temporary) / f"case{index}"
            shutil.copytree(base, case)
            plant(case)
            if not check(case):
                print(f"plugin_data_boundary self-test failed: planted case not caught: {label}")
                return 1

        nm_script = Path(temporary) / "fake_nm.py"
        nm_script.write_text(SELF_TEST_NM, encoding="utf-8")
        nm = [sys.executable, str(nm_script)]
        archive_dir = Path(temporary) / "archives"
        archive_dir.mkdir()

        def write_archive(name, lines):
            path = archive_dir / name
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return path

        data = write_archive("data.a", ["U memcpy", "U boat_core_thing"])
        core = write_archive("core.a", ["T boat_core_thing"])
        problems = check(base, nm, [data, core])
        if problems:
            print("plugin_data_boundary self-test failed: clean archives are not clean",
                  file=sys.stderr)
            for problem in problems:
                print(problem, file=sys.stderr)
            return 1

        helper = write_archive("helper.a", ["T sandbox_helper"])
        consumer = write_archive("consumer.a", ["U sandbox_helper"])
        problems = check(base, nm, [consumer, core], [helper])
        if problems:
            print("plugin_data_boundary self-test failed: consumer archive does not resolve "
                  "against the listed archives", file=sys.stderr)
            for problem in problems:
                print(problem, file=sys.stderr)
            return 1

        problems = check(base, nm, [write_archive("elf.a", [
            "U __stack_chk_fail", "U __memcpy_chk", "U __snprintf_chk", "U boat_core_thing"]), core])
        macho = write_archive("macho.a", ["U ___stack_chk_fail", "U _memcpy", "U _boat_core_thing"])
        mach_core = write_archive("mach_core.a", ["T _boat_core_thing"])
        MACHO_saved = globals()["MACHO"]
        globals()["MACHO"] = True
        try:
            problems += check(base, nm, [macho, mach_core])
        finally:
            globals()["MACHO"] = MACHO_saved
        if problems or not check(base, nm, [write_archive("bad.a", ["U _unknown"]), core]):
            print(f"plugin_data_boundary self-test failed: ELF/Mach-O names: {problems}",
                  file=sys.stderr)
            return 1

        archive_cases = (
            ("data archive reaches Lua", ["U lua_pushnil"], ()),
            ("data archive has an unknown symbol", ["U some_unknown_symbol"], ()),
            ("consumer archive opens files", ["U boat_core_thing"], (["U fopen"],)),
        )
        for index, (label, undefined, consumer_lines) in enumerate(archive_cases):
            data_archive = write_archive(f"data{index}.a", undefined)
            consumer_archives = [write_archive(f"consumer{index}.a", lines)
                                 for lines in consumer_lines]
            if not check(base, nm, [data_archive, core], consumer_archives):
                print(f"plugin_data_boundary self-test failed: planted archive case not "
                      f"caught: {label}")
                return 1

        accepted = Path(temporary) / "accepted"
        shutil.copytree(base, accepted)
        append_text(accepted / "src" / "plugins" / "boat_plugin.c", SELF_TEST_ACCEPTED)
        problems = check(accepted)
        if problems:
            print("plugin_data_boundary self-test failed: comment or string literal was "
                  "rejected", file=sys.stderr)
            for problem in problems:
                print(problem, file=sys.stderr)
            return 1

    print("plugin_data_boundary self-test=ok")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, required=True, help="repository root to check")
    parser.add_argument("--nm", help="nm command for the archive symbol check")
    parser.add_argument("--archive", action="append", default=[],
                        help="archive to check; the first is the plugin data library")
    parser.add_argument("--consumer-archive", action="append", default=[],
                        help="data-only consumer archive whose undefined symbols must resolve")
    parser.add_argument("--self-test", action="store_true",
                        help="plant violations in a temporary copy and confirm they are caught")
    args = parser.parse_args()
    if args.self_test:
        return self_test(args.root)
    problems = check(args.root, args.nm, args.archive, args.consumer_archive)
    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    print("plugin_data_boundary=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
