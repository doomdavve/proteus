#!/usr/bin/env python3
"""Enforce qualified project includes and the transitive module ownership graph."""
import argparse
from pathlib import Path
import re
import tempfile

GRAPH = {
    'core': set(), 'json': set(), 'platform': set(), 'audio': set(),
    'scenery': {'core'}, 'net': {'core'}, 'render': {'scenery', 'platform'},
    'plugins': {'core', 'json', 'platform'},
    'session': {'render', 'net', 'plugins'},
    'room': {'net', 'plugins', 'platform'},
}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.M)


def allowed(module):
    result = {module}
    pending = [module]
    while pending:
        for dependency in GRAPH[pending.pop()]:
            if dependency not in result:
                result.add(dependency)
                pending.append(dependency)
    return result


def check(root):
    paths = [p for directory in ('src', 'apps', 'tests', 'tools')
             for p in (root / directory).rglob('*') if p.suffix in {'.c', '.h'}]
    headers = {p.relative_to(root).parts[-1] for p in paths if p.suffix == '.h'}
    qualified = {(p.relative_to(root).as_posix() if p.relative_to(root).parts[0] == 'tools'
                  else p.relative_to(root).as_posix().split('/', 1)[1]): p
                 for p in paths if p.suffix == '.h'}
    problems = []
    for path in paths:
        relative = path.relative_to(root)
        module = relative.parts[1] if relative.parts[0] == 'src' else None
        # Preserve newlines so diagnostics identify the source line.
        source = re.sub(r'/\*.*?\*/|//[^\n]*',
                        lambda m: '\n' * m[0].count('\n'), path.read_text(), flags=re.S)
        for match in INCLUDE.finditer(source):
            header = match[1]
            number = source.count('\n', 0, match.start()) + 1
            target = qualified.get(header)
            # Resolve aliases too: relative paths must not bypass qualification or ownership.
            resolved = (path.parent / header).resolve()
            project = target or (resolved if resolved in paths else None)
            if target is None and (Path(header).name in headers or project):
                # Developer tool headers have no module prefix; keep their local convention.
                if relative.parts[0] == 'tools' and (root / 'tools' / header).is_file():
                    continue
                problems.append(f'{relative}:{number}: unqualified project include {header}')
            elif target and module:
                target_relative = target.relative_to(root)
                if target_relative.parts[0] != 'src' or target_relative.parts[1] not in allowed(module):
                    problems.append(f'{relative}:{number}: {module} cannot include {header}')
    return problems


def self_test():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name in ('src/core/a.h', 'src/render/b.h', 'src/net/new/nested.c'):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('')
        source = root / 'src/net/new/nested.c'
        for include in ('a.h', 'render/b.h', '../../render/b.h'):
            source.write_text(f'#include "{include}"\n')
            assert check(root), include
        source.write_text('#include "core/a.h"\n')
        assert not check(root)
    print('module_includes_self_test=ok')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('.'))
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    problems = check(args.root.resolve())
    print('\n'.join(problems) if problems else 'module_includes=ok')
    return bool(problems)


if __name__ == '__main__':
    raise SystemExit(main())
