#!/usr/bin/env python3
"""Marina M5 W5: shared plugin modules stay shared, and plugins/_shared is never a package.

plugins/_shared holds the source of modules that several packages carry as checked-in copies
(plugins/<package>/modules/<name>.lua). A module whose name exists in _shared is a copy and must be
byte-equal to it; `python3 tools/plugin_author.py sync-shared` rewrites the copies. Packages are
enumerated by explicit lists (CMake, the release tools, the authoring layout), and none may name a
directory starting with "_"; _shared has no plugin.json. --self-test plants violations in a
temporary tree.
"""

import argparse
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import plugin_author  # noqa: E402

FIX = "python3 tools/plugin_author.py sync-shared"
# Files that list packages, and so must not list a "_" directory.
LISTS = ("CMakeLists.txt", "src/plugins/CMakeLists.txt", "tools/package_plugin_tools.py",
         "tools/package_web.py", "tools/cut_release.py")
PACKAGE_REF = re.compile(r"plugins/(_[A-Za-z0-9_.-]*)")


def problems(root):
    root = Path(root)
    plugins = root / "plugins"
    found = []
    try:
        result = plugin_author.sync_shared(plugins, check=True)
    except (OSError, ValueError) as error:
        return [f"{error}"]
    for path in result["drifted"]:
        found.append(f"plugins/{path} differs from its source in plugins/_shared; run: {FIX}")
    for shared in sorted(plugins.glob("_*")):
        if (shared / "plugin.json").exists():
            found.append(f"plugins/{shared.name} has a plugin.json; a _-prefixed directory is not "
                         "a package")
    for name in LISTS:
        path = root / name
        if path.is_file():
            for match in PACKAGE_REF.finditer(path.read_text(encoding="utf-8")):
                found.append(f"{name} names plugins/{match.group(1)}; _-prefixed directories "
                             "are shared sources, not packages")
    layout = (root / "tools/package_plugin_tools.py")
    if layout.is_file():
        examples = re.search(r"EXAMPLES = \(([^)]*)\)", layout.read_text(encoding="utf-8"))
        for name in re.findall(r'"([^"]+)"', examples.group(1) if examples else ""):
            if name.startswith("_"):
                found.append(f"tools/package_plugin_tools.py EXAMPLES lists {name}")
    return found


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def tree(base):
    plugins = base / "plugins"
    write(plugins / "_shared/marina.lua", "return { berths = 3 }\n")
    for name in ("one", "two"):
        write(plugins / name / "plugin.json",
              '{"id": "x.%s", "modules": ["marina"]}\n' % name)
        write(plugins / name / "main.lua", "return {}\n")
        write(plugins / name / "modules/marina.lua", "return { berths = 3 }\n")
    write(plugins / "three/plugin.json", '{"id": "x.three"}\n')  # No modules: not a copy.
    write(base / "src/plugins/CMakeLists.txt", "set(P plugins/one plugins/two plugins/three)\n")
    write(base / "tools/package_plugin_tools.py", 'EXAMPLES = ("one", "two")\n')


def self_test():
    with tempfile.TemporaryDirectory() as scratch:
        base = Path(scratch)
        tree(base)
        if problems(base):
            print("plugin_shared self-test failed: clean tree is not clean", problems(base),
                  file=sys.stderr)
            return 1
        # A drifted copy fails and names the fix; sync-shared repairs it.
        write(base / "plugins/two/modules/marina.lua", "return { berths = 4 }\n")
        found = problems(base)
        if len(found) != 1 or "plugins/two/modules/marina.lua" not in found[0] or FIX not in found[0]:
            print("plugin_shared self-test failed: drift not reported", found, file=sys.stderr)
            return 1
        result = plugin_author.sync_shared(base / "plugins", check=True)
        if result["drifted"] != ["two/modules/marina.lua"] or result["changed"]:
            print("plugin_shared self-test failed: --check wrote or missed", result, file=sys.stderr)
            return 1
        result = plugin_author.sync_shared(base / "plugins")
        if result["changed"] != ["two/modules/marina.lua"] or problems(base):
            print("plugin_shared self-test failed: sync did not repair", result, file=sys.stderr)
            return 1
        # A declared copy that is missing is created.
        (base / "plugins/one/modules/marina.lua").unlink()
        if len(problems(base)) != 1:
            print("plugin_shared self-test failed: missing copy not reported", file=sys.stderr)
            return 1
        plugin_author.sync_shared(base / "plugins")
        if problems(base) or not (base / "plugins/one/modules/marina.lua").exists():
            print("plugin_shared self-test failed: missing copy not created", file=sys.stderr)
            return 1
        # An undeclared file that shares a name with a shared module is still a copy.
        write(base / "plugins/three/modules/marina.lua", "return {}\n")
        if len(problems(base)) != 1:
            print("plugin_shared self-test failed: undeclared copy not compared", file=sys.stderr)
            return 1
        plugin_author.sync_shared(base / "plugins")
        # A differently named module is not a copy and is left alone.
        write(base / "plugins/three/modules/local.lua", "return 1\n")
        if problems(base):
            print("plugin_shared self-test failed: unshared module rejected", file=sys.stderr)
            return 1
        # _shared must not become a package, by manifest or by a package list.
        write(base / "plugins/_shared/plugin.json", "{}\n")
        if not any("not a package" in line for line in problems(base)):
            print("plugin_shared self-test failed: _shared manifest accepted", file=sys.stderr)
            return 1
        (base / "plugins/_shared/plugin.json").unlink()
        write(base / "src/plugins/CMakeLists.txt", "set(P plugins/one plugins/_shared)\n")
        if not any("CMakeLists.txt names plugins/_shared" in line for line in problems(base)):
            print("plugin_shared self-test failed: CMake list accepted _shared", file=sys.stderr)
            return 1
        write(base / "src/plugins/CMakeLists.txt", "set(P plugins/one)\n")
        write(base / "tools/package_plugin_tools.py", 'EXAMPLES = ("one", "_shared")\n')
        if not any("EXAMPLES lists _shared" in line for line in problems(base)):
            print("plugin_shared self-test failed: EXAMPLES accepted _shared", file=sys.stderr)
            return 1
    print("plugin_shared self-test=ok")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    found = problems(args.root)
    for line in found:
        print(line, file=sys.stderr)
    if found:
        return 1
    print("plugin_shared=ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
