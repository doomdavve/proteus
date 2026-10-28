#!/usr/bin/env python3
"""Check that the boating core library cannot draw, read input, open windows or audio, or do
file I/O.

Sources: every quoted include must be another core header (or raylib.h/raymath.h for vector
types and math, or a map's compiled water, water.h), system headers come from a short
allowlist without <stdio.h>, raylib-style PascalCase calls must be raymath, and C library
I/O/process calls are rejected. Archives (when nm is given): every undefined symbol is defined in one of the archives or is
raymath, libm, libc memory/sort/assert, clock(), or the Accelerate DFT backend.

Run with --self-test to confirm each rule rejects a known violation.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ALLOWED_QUOTED = {"raylib.h", "raymath.h"}
GEOMETRY_INCLUDE = re.compile(r"\.\./assets/geometry/[a-z]+/water\.h")
ALLOWED_SYSTEM = {
    "assert.h", "float.h", "limits.h", "math.h", "stdbool.h", "stddef.h", "stdint.h",
    "stdlib.h", "string.h", "time.h", "Accelerate/Accelerate.h",
}
RAYMATH_CALL = re.compile(
    r"(Vector[234]|Matrix|Quaternion)\w*|Clamp|Lerp|Normalize|Remap|Wrap|FloatEquals")
# C library console/file/process calls; raylib's own I/O is caught as non-raymath PascalCase.
FORBIDDEN_CALLS = {
    "fopen", "freopen", "fclose", "fread", "fwrite", "fflush", "fseek", "ftell", "rewind",
    "fprintf", "printf", "vprintf", "vfprintf", "fputs", "fputc", "puts", "putchar", "fgets",
    "fgetc", "getchar", "fscanf", "scanf", "remove", "rename", "tmpfile", "mkdir", "rmdir",
    "opendir", "readdir", "unlink", "stat", "open", "read", "write", "close", "system",
    "popen", "getenv",
}
FORBIDDEN_IDENTIFIERS = {"stdin", "stdout", "stderr", "FILE"}
MATH_SYMBOL = re.compile(
    r"(a?(sin|cos|tan)h?|sincos|atan2|exp|exp2|expm1|log|log2|log10|log1p|pow|sqrt|cbrt|hypot|"
    r"fmod|remainder|floor|ceil|round|trunc|l?lround|l?lrint|rint|nearbyint|fabs|fmin|fmax|copysign|fma|"
    r"ldexp|frexp|modf)[fl]?")
RUNTIME_SYMBOL = re.compile(
    r"malloc|calloc|realloc|free|memcpy|memmove|memset|memset_pattern\d+|memcmp|bzero|strcmp|"
    r"strlen|qsort|clock|_?_assert\w*|__\w*chk\w*|__stack_chk_\w+|__chkstk\w*|"
    r"__sincosf?_stret|vDSP_\w+|abort")


def strip_comments_and_strings(text):
    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'',
        re.S)

    def keep_lines(match):
        token = match.group(0)
        if token.startswith('"'):
            return '""'
        return "\n" * token.count("\n") + " "

    return pattern.sub(keep_lines, text)


def check_source(name, text, core_headers):
    problems = []
    for number, line in enumerate(text.splitlines(), 1):
        quoted = re.match(r'\s*#\s*include\s*"([^"]+)"', line)
        system = re.match(r"\s*#\s*include\s*<([^>]+)>", line)
        if quoted:
            header = quoted.group(1)
            if not (header in core_headers or header in ALLOWED_QUOTED
                    or GEOMETRY_INCLUDE.fullmatch(header)):
                problems.append(f"{name}:{number}: includes non-core header \"{header}\"")
        elif system and system.group(1) not in ALLOWED_SYSTEM:
            problems.append(f"{name}:{number}: includes <{system.group(1)}>")
    code = strip_comments_and_strings(text)
    for number, line in enumerate(code.splitlines(), 1):
        if line.lstrip().startswith("#"):
            continue
        # A "Type (*name)(...)" function-pointer declarator is not a call.
        for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\((?!\s*\*)", line):
            call = match.group(1)
            pascal = call[0].isupper() and any(c.islower() for c in call)
            if pascal and not RAYMATH_CALL.fullmatch(call):
                problems.append(f"{name}:{number}: calls {call}(), not raymath")
            elif call in FORBIDDEN_CALLS or re.fullmatch(r"rl[A-Z]\w*", call):
                problems.append(f"{name}:{number}: calls {call}()")
        for match in re.finditer(r"\b(\w+)\b", line):
            if match.group(1) in FORBIDDEN_IDENTIFIERS:
                problems.append(f"{name}:{number}: uses {match.group(1)}")
    return problems


def check_sources(root, files):
    paths = [root / f for f in files]
    headers = {p.name for p in paths if p.suffix == ".h"}
    problems = []
    for path in paths:
        if path.suffix == ".c" and path.with_suffix(".h").exists() \
                and path.with_suffix(".h").name not in headers:
            problems.append(f"{path.name}: header {path.with_suffix('.h').name} not listed")
        problems += check_source(path.name, path.read_text(), headers)
    return problems


def symbol_allowed(symbol, defined):
    return (symbol in defined or RAYMATH_CALL.fullmatch(symbol) or MATH_SYMBOL.fullmatch(symbol)
            or RUNTIME_SYMBOL.fullmatch(symbol))


def check_archives(nm, archives):
    def symbols(archive, *options):
        output = subprocess.run([nm, *options, archive], check=True, capture_output=True,
            text=True).stdout
        names = set()
        for line in output.splitlines():
            fields = line.split()
            if not fields or line.endswith(":"):
                continue
            name = fields[-1].split("@")[0]
            if sys.platform == "darwin" and name.startswith("_"):
                name = name[1:]
            names.add(name)
        return names

    # Archives listed together link together: a symbol defined in one may be used by another.
    defined = set()
    for archive in archives:
        defined |= symbols(archive, "-g", "--defined-only")
    problems = []
    for archive in archives:
        problems += [f"{Path(archive).name}: references {s}"
                     for s in sorted(symbols(archive, "-u")) if not symbol_allowed(s, defined)]
    return problems


def self_test():
    headers = {"boat.h", "boat_water.h"}
    violations = {
        "drawing header": '#include "renderer3d.h"\n',
        "scene header": '#include "waterfront_scene.h"\n',
        "stdio": "#include <stdio.h>\n",
        "drawing call": "void f(void) { DrawCube(p, 1, 1, 1, RED); }\n",
        "input": "bool f(void) { return IsKeyDown(KEY_UP); }\n",
        "window": "void f(void) { InitWindow(320, 200, t); }\n",
        "audio": "void f(void) { PlaySound(s); }\n",
        "raylib file": "void f(void) { SaveFileText(p, t); }\n",
        "libc file": "void f(void) { fopen(p, m); }\n",
        "console": "void f(void) { fputs(t, out); }\n",
        "stream": "void f(void) { use(stderr); }\n",
        "rlgl": "void f(void) { rlDrawRenderBatchActive(); }\n",
    }
    failures = [k for k, v in violations.items() if not check_source(k, v, headers)]
    clean = ('#include "boat.h"\n#include "raymath.h"\n#include <math.h>\n'
             "// DrawCube(fopen) in a comment\n"
             "typedef struct { BoatStats (*stats)(void *context); } Table;\n"
             'float f(Vector2 v) { const char *s = "printf("; (void)s;\n'
             "    return Clamp(Vector2Length(Vector2Scale(v, 2)), 0, sinf(1)); }\n")
    noisy = check_source("clean", clean, headers)
    if failures or noisy:
        for k in failures:
            print(f"self-test: {k} violation was not reported")
        for p in noisy:
            print(f"self-test: clean source reported: {p}")
        return False
    if not symbol_allowed("Vector3Add", set()) or symbol_allowed("DrawCube", set()) \
            or symbol_allowed("fopen", set()) or not symbol_allowed("atan2f", set()):
        print("self-test: symbol allowlist is wrong")
        return False
    print(f"self-test: {len(violations)} violations rejected; clean source accepted")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--nm")
    parser.add_argument("--archive", action="append", default=[])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("files", nargs="*")
    args = parser.parse_args()
    if not self_test():
        return 1
    if args.self_test:
        return 0
    if not args.files:
        parser.error("no core sources given")
    problems = check_sources(args.root, args.files)
    print(f"sources: {len(args.files)} core files checked")
    if args.nm and args.archive:
        problems += check_archives(args.nm, args.archive)
        names = ", ".join(Path(a).name for a in args.archive)
        print(f"symbols: {names} checked with {Path(args.nm).name}")
    else:
        print("symbols: skipped (no nm)")
    for problem in problems:
        print(problem)
    print("core_boundary=" + ("fail" if problems else "ok"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
