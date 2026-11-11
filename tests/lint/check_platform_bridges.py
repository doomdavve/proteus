#!/usr/bin/env python3
"""Keep browser bridges and platform service backends out of shared application code (AR3).

Rule 9 of docs/ARCHITECTURE_PLAN.md: platform code sits behind small interfaces in CMake-selected
files. Two checks over src/ and tools/:

1. Inline JavaScript (EM_JS, EM_ASYNC_JS, EM_ASM*, MAIN_THREAD_EM_ASM*, emscripten_*run_script*)
   appears only in browser implementation files (*_web.c) or in a file listed in LEGACY with the
   reason it still has bridges. A legacy file must have exactly its recorded count, so a
   migration lowers the count in the same change and the list only shrinks.
2. Files migrated onto a service do not reach the service's backends directly (MIGRATED).
3. CMakeLists.txt names *_web.c sources only inside an EMSCRIPTEN branch, or in the sources of
   a native test target listed in NATIVE_WEB_TESTS (which compiles a backend against a C model
   of its page bridges).

The CMake parser is deliberately small: it reads each build file on its own (the root, module
CMakeLists.txt files, tests/**/*.cmake and cmake/*.cmake), follows
if/elseif/else/endif only when each sits on a single line, and tracks the target of
add_executable/add_library/target_sources across continuation lines by parenthesis depth.

--self-test runs both checks against synthetic violations and fails unless they are caught.
"""

import argparse
import re
import sys
from pathlib import Path

BRIDGE = re.compile(r"\b(EM_JS|EM_ASYNC_JS|(?:MAIN_THREAD_)?EM_ASM\w*|emscripten_\w*run_script\w*)"
                    r"\s*\(")
WEB_SOURCE = re.compile(r"[\w/.-]+_web\.c\b")
TARGET_COMMAND = re.compile(r"\s*(add_executable|add_library|target_sources)\s*\(\s*([\w.+-]+)")

# Native test targets allowed to compile *_web.c sources outside an EMSCRIPTEN branch -> reason.
NATIVE_WEB_TESTS = {
    "validate_boat_plugin_stage_web": "AR4: browser staging paths driven natively on virtual time",
}

# File -> (bridges allowed, owner of their extraction).
LEGACY = {
    "src/plugins/boat_plugin_stage_worker.c": (1, "AR4: plugin worker backends"),
}

# File -> forbidden patterns: the backends its services own.
MIGRATED = {
    "apps/wake_velocity/boat_title.c": [
        r"#\s*include\s*[<\"]emscripten", r"#\s*include\s*\"(?:wake_velocity/)?boat_directory\.h\"",
        r"#\s*include\s*\"(?:net/)?boat_net_enet\.h\"", r"\bboat_directory_\w+\s*\(",
        r"\bboat_net_enet_\w+\s*\(", r"\bboat_net_lan_(search|query|receive|close)\s*\(",
        r"boatServers",
    ],
    "apps/wake_velocity/wake_velocity.c": [
        r"boatScenery", r"boatServers", r"boatPlugin", r"\bemscripten_get_now\s*\(",
        r"\bboat_scenery_load_all\s*\(", r"\bboat_directory_\w+\s*\(", r"boatFitCanvas",
        r"boatConsole", r"matchMedia", r"\bSetTargetFPS\s*\(", r"\bInitAudioDevice\s*\(",
        r"boatViewportSize", r"boatMobile", r"boatMouse", r"#\s*include\s*[<\"]emscripten",
        r"\b(Disable|Enable)Cursor\s*\(", r"\bKEY_F11\b", r"\bKEY_M\b",
    ],
    "apps/wake_velocity/wake_velocity_input.c": [
        r"#\s*include\s*[<\"]emscripten", r"\bModule\s*[.\[]", BRIDGE.pattern,
    ],
    "apps/wake_velocity/wake_console.c": [
        r"#\s*include\s*[<\"]emscripten", r"\bModule\s*[.\[]", r"\bSetClipboardText\s*\(",
        r"\bGetClipboardText\s*\(",
    ],
    "apps/wake_velocity/wake_velocity_plugin.c": [
        r"#\s*include\s*[<\"]emscripten", r"\bModule\s*[.\[]", BRIDGE.pattern,
    ],
}


def strip_comments(text):
    """Drop C comments. String and character literals are kept verbatim (bridge names inside
    JavaScript strings still count) but are skipped while looking for comments, so "WS://"
    does not start one."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            i = text.find("\n", i)
            i = n if i < 0 else i
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def cmake_files(root):
    """The project's build files: the root, each module's CMakeLists.txt and the test lists."""
    found = [root / "CMakeLists.txt"]
    for directory in ("src", "apps", "tools"):
        found += sorted((root / directory).rglob("CMakeLists.txt"))
    found += sorted((root / "tests").rglob("*.cmake")) + sorted((root / "cmake").glob("*.cmake"))
    return [path for path in found if path.is_file()]


def check_cmake(text, path="CMakeLists.txt"):
    """*_web.c sources may appear only where EMSCRIPTEN is true or in a NATIVE_WEB_TESTS target."""
    problems, stack = [], []  # Each frame: True browser, False native, None other condition.
    target, depth = None, 0  # Target of the open add_executable/add_library/target_sources.
    for number, line in enumerate(text.splitlines(), 1):
        code = line.split("#", 1)[0]
        if depth == 0:
            started = TARGET_COMMAND.match(code)
            target = started.group(2) if started else None
        line_target = target
        depth = max(0, depth + code.count("(") - code.count(")")) if target else 0
        if depth == 0:
            target = None
        command = re.match(r"\s*(if|elseif|else|endif)\s*\((.*)\)", code)
        if command:
            word, condition = command.group(1), command.group(2).strip()
            kind = {"EMSCRIPTEN": True, "NOT EMSCRIPTEN": False}.get(condition)
            if word == "if":
                stack.append(kind)
            elif word == "elseif" and stack:
                stack[-1] = None if stack[-1] is None else kind
            elif word == "else" and stack:
                stack[-1] = None if stack[-1] is None else not stack[-1]
            elif word == "endif" and stack:
                stack.pop()
        for source in WEB_SOURCE.findall(code):
            if line_target in NATIVE_WEB_TESTS:
                continue
            if False in stack or True not in stack:
                problems.append(f"{path}:{number}: {source} is built outside an EMSCRIPTEN branch")
    return problems


def check(files):
    """FILES maps a repository-relative path to its text. Returns the problems found."""
    problems = []
    for path, text in sorted(files.items()):
        code = strip_comments(text)
        count = len(BRIDGE.findall(code))
        if path.endswith("_web.c"):
            pass
        elif path in LEGACY and count != LEGACY[path][0]:
            allowed = LEGACY[path][0]
            problems.append(f"{path}: {count} inline JavaScript bridges, {allowed} recorded; "
                            + (f"lower LEGACY to {count}" if count < allowed else
                               "put new bridges in a CMake-selected *_web.c backend"))
        elif path not in LEGACY and count:
            problems.append(f"{path}: {count} inline JavaScript bridges; put them in a "
                            "CMake-selected *_web.c backend")
        for pattern in MIGRATED.get(path, []):
            if re.search(pattern, code):
                problems.append(f"{path}: reaches a service backend directly ({pattern})")
    return problems


def self_test():
    legacy = "src/plugins/boat_plugin_stage_worker.c"  # Recorded with one bridge.
    bridge = 'EM_JS(int, f, (void), { return 1; })\n'
    cases = [  # (files, cmake text, expected problem fragment)
        ({"tools/new_feature.c": bridge}, "", "put them in a CMake-selected"),
        ({"tools/new_feature.c": 'void f(void) { MAIN_THREAD_EM_ASM({ x(); }); }\n'}, "",
         "put them in"),
        ({"tools/new_feature.c": 'void f(void) { emscripten_run_script("x()"); }\n'}, "",
         "put them in"),
        ({"tools/new_feature.c": 'void f(void) { emscripten_async_run_script("x()", 1); }\n'},
         "", "put them in"),
        ({"tools/new_feature.c": 'const char *a = "WS://";\n' + bridge}, "", "put them in"),
        ({legacy: ""}, "", "lower LEGACY to 0"),
        ({legacy: bridge + bridge}, "", "put new bridges"),
        ({"apps/wake_velocity/boat_title.c": '#include "boat_directory.h"\n'}, "", "boat_directory"),
        ({"apps/wake_velocity/wake_velocity_plugin.c": "void f(void) { Module.x = 1; }\n"}, "", "Module"),
        ({"apps/wake_velocity/wake_velocity.c": "void f(void) { DisableCursor(); }\n"}, "", "Cursor"),
        ({}, "add_executable(x tools/x_web.c)\n", "outside an EMSCRIPTEN branch"),
        ({}, "if(NOT EMSCRIPTEN)\n  target_sources(x PRIVATE tools/x_web.c)\nendif()\n",
         "outside an EMSCRIPTEN branch"),
        ({}, "if(EMSCRIPTEN)\nelse()\n  target_sources(x PRIVATE tools/x_web.c)\nendif()\n",
         "outside an EMSCRIPTEN branch"),
        ({}, "add_executable(validate_other tools/validate_other.c\n  src/x_web.c)\n",
         "outside an EMSCRIPTEN branch"),
        ({}, "add_executable(validate_boat_plugin_stage_web tools/v.c)\n"
         "add_executable(other src/x_web.c)\n", "outside an EMSCRIPTEN branch"),
    ]
    failed = []
    for number, (files, cmake, expected) in enumerate(cases):
        problems = check(files) + check_cmake(cmake)
        if not any(expected in problem for problem in problems):
            failed.append(f"case {number} missed: {expected} ({problems})")
    clean = check({"tools/new_web.c": bridge, legacy: bridge,
                   "apps/wake_velocity/boat_title.c": 'const char *h = "WS://"; // boat_directory_refresh()\n'})
    clean += check_cmake("if(EMSCRIPTEN)\n  target_sources(x PRIVATE tools/x_web.c) # a_web.c\n"
                         "else()\n  target_sources(x PRIVATE tools/x.c)\nendif()\n"
                         "# Comments may name tools/x_web.c.\n"
                         "add_executable(validate_boat_plugin_stage_web tools/v.c\n"
                         "  src/plugins/boat_plugin_stage.c src/plugins/boat_plugin_stage_web.c)\n")
    if clean:
        failed.append(f"clean input flagged: {clean}")
    for failure in failed:
        print(f"platform_bridges self-test: {failure}")
    if failed:
        return 1
    print("platform_bridges_self_test=ok")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    files = {}
    for directory in ("src", "apps", "tests", "tools"):
        for path in sorted((args.root / directory).rglob("*.[ch]")):
            files[path.relative_to(args.root).as_posix()] = path.read_text(errors="replace")
    for path in list(LEGACY) + list(MIGRATED):
        if path not in files:
            print(f"platform_bridges: {path} is listed but missing; update the lists")
            return 1
    problems = check(files)
    cmake_text = ""
    for cmake in cmake_files(args.root):
        name = cmake.relative_to(args.root).as_posix()
        text = cmake.read_text()
        problems += check_cmake(text, name)
        cmake_text += text + "\n"
    for target in NATIVE_WEB_TESTS:
        if not re.search(r"add_executable\s*\(\s*" + re.escape(target) + r"\s", cmake_text):
            problems.append(f"CMake: NATIVE_WEB_TESTS lists {target}, which no build file "
                            "builds; update the list")
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print("platform_bridges=ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
