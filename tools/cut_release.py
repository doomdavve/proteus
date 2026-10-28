#!/usr/bin/env python3
"""Publish a checked commit, verify downloads, and upgrade the matched Winterfell pair."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import zipfile


def run(*args, cwd=None):
    print('+', *map(str, args), flush=True)
    return subprocess.check_output(list(map(str, args)), cwd=cwd, text=True).strip()


def verify_archive(archive, tag, commit):
    checksum = archive.with_suffix('.zip.sha256').read_text().split()
    if len(checksum) != 2 or checksum[1].lstrip('*') != archive.name:
        raise ValueError(f'Unexpected checksum filename: {archive}')
    with archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if digest != checksum[0]:
        raise ValueError(f'Checksum mismatch: {archive}')
    with zipfile.ZipFile(archive) as package:
        if package.testzip() is not None:
            raise ValueError(f'Corrupt ZIP: {archive}')
        metadata = [n for n in package.namelist() if n.endswith('/build.txt')
                    and not n.startswith('__MACOSX/')]
        if len(metadata) != 1:
            raise ValueError(f'Expected one build.txt: {archive}')
        fields = dict(line.split(': ', 1) for line in package.read(metadata[0]).decode('utf-8-sig').splitlines()
                      if ': ' in line)
        if fields.get('Version') != tag or fields.get('Commit') != commit:
            raise ValueError(f'Package identity mismatch: {archive}: {fields}')


def version_pin(text, tag):
    updated, count = re.subn(r'(?m)^wake_velocity_version:.*$',
                             f'wake_velocity_version: {tag}', text)
    if count != 1:
        raise ValueError('Expected exactly one wake_velocity_version pin')
    return updated


def wait_for_publication(root, tag, commit, timeout):
    deadline = time.monotonic() + timeout
    workflows = ('release.yml', 'web-pages.yml')
    while time.monotonic() < deadline:
        complete = True
        for workflow in workflows:
            runs = json.loads(run('gh', 'run', 'list', '--workflow', workflow, '--commit', commit,
                                  '--event', 'push', '--limit', '20', '--json',
                                  'headBranch,status,conclusion,url', cwd=root))
            matching = [r for r in runs if r['headBranch'] == tag]
            if not matching or matching[0]['status'] != 'completed':
                complete = False
            elif matching[0]['conclusion'] != 'success':
                raise RuntimeError(f'Publication failed: {matching[0]["url"]}')
            else:
                print(matching[0]['url'], flush=True)
        if complete:
            return
        time.sleep(15)
    raise TimeoutError('Publication deadline exceeded; rerun after checking Actions')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag', help='New immutable vMAJOR.MINOR.PATCH tag')
    parser.add_argument('--hosting-repo', required=True, type=Path,
                        help='Clean, pushed davve-srv worktree with its controller/vault setup')
    parser.add_argument('--output', type=Path, default=Path('build/release'))
    parser.add_argument('--timeout', type=int, default=3600)
    parser.add_argument('--browser-python', default=sys.executable,
                        help='Python with Playwright installed for live named joins')
    args = parser.parse_args()
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', args.tag):
        parser.error('Expected a semantic version tag')
    root = Path(__file__).resolve().parents[1]
    hosting = args.hosting_repo.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Check prerequisites before creating the immutable tag or changing infrastructure.
    for repo in (root, hosting):
        if run('git', 'status', '--porcelain', cwd=repo):
            raise RuntimeError(f'Commit changes before releasing: {repo}')
        run('git', 'fetch', 'origin', cwd=repo)
        if run('git', 'rev-list', '--count', '@{u}..HEAD', cwd=repo) != '0':
            raise RuntimeError(f'Push the branch before releasing: {repo}')
    run(args.browser_python, '-c', 'import playwright.sync_api')
    if not (root / 'build/boat_bot').is_file():
        raise RuntimeError('Build the matching native boat_bot before releasing')
    commit = run('git', 'rev-parse', 'HEAD', cwd=root)
    if commit != run('git', 'rev-parse', 'origin/main', cwd=root):
        raise RuntimeError('Release from the reviewed and merged origin/main commit')
    existing = subprocess.run(['git', 'rev-parse', '--verify', f'refs/tags/{args.tag}^{{commit}}'],
                              cwd=root, text=True, capture_output=True)
    if existing.returncode == 0:
        if existing.stdout.strip() != commit:
            raise RuntimeError('Existing tag points elsewhere; never move a published tag')
    else:
        run('git', 'tag', '-a', args.tag, '-m', f'Wake Velocity {args.tag}', cwd=root)
    run('git', 'push', 'origin', f'refs/tags/{args.tag}', cwd=root)
    wait_for_publication(root, args.tag, commit, args.timeout)
    release = json.loads(run('gh', 'release', 'view', args.tag, '--json',
                             'isDraft,url', cwd=root))
    if release['isDraft']:
        raise RuntimeError('Release is still a draft')
    run('gh', 'release', 'download', args.tag, '--dir', output, '--clobber', cwd=root)
    for platform in ('macos-arm64', 'windows-x64'):
        verify_archive(output / f'wake_velocity-{args.tag}-{platform}.zip', args.tag, commit)
    pin = hosting / 'ansible/host_vars/winterfell/main.yml'
    previous = pin.read_text()
    updated = version_pin(previous, args.tag)
    if previous != updated:
        pin.write_text(updated)
        run('git', 'add', str(pin), cwd=hosting)
        run('git', 'commit', '-m', f'Upgrade Winterfell Wake Velocity to {args.tag}', cwd=hosting)
        run('git', 'push', '-u', 'origin', 'HEAD', cwd=hosting)
    # Use the normal role only; a failed build retains the previous room configuration.
    subprocess.run(['scripts/ansible-run.sh', '--limit', 'winterfell', '--tags', 'wake_velocity'],
                   cwd=hosting, check=True)
    health = run('ssh', 'winterfell',
                 'systemctl is-active wake-velocity wake-velocity-relay && '
                 'systemctl show wake-velocity --property=Description --value')
    if health.splitlines()[:2] != ['active', 'active'] or f' {args.tag} ' not in health:
        raise RuntimeError(f'Unexpected running release: {health}')
    pages = 'https://doomdavve.github.io/proteus/'
    hosted = 'https://wake.davve.net/'
    for url in (hosted, pages):
        html = run('curl', '-fsS', '--max-time', '30', url)
        if f'Version {args.tag}<' not in html:
            raise RuntimeError(f'Browser release mismatch: {url}')
        subprocess.run([args.browser_python, str(root / 'tools/check_web_server_directory.py'),
                        '--url', url, '--output', str(output / ('hosted' if url == hosted else 'pages'))],
                       check=True)
    directory = json.loads(run('curl', '-fsS', '--max-time', '30', hosted + 'servers.json'))
    protocol = int(re.search(r'#define BOAT_NET_VERSION (\d+)',
                            (root / 'src/boat_net_protocol.h').read_text())[1])
    physics = int(re.search(r'#define BOAT_PHYSICS_REVISION (\d+)',
                           (root / 'src/boat.h').read_text())[1])
    room = next(s for s in directory['servers'] if s['name'] == 'Winterfell')
    if (room['protocol'], room['physics']) != (protocol, physics):
        raise RuntimeError(f'Directory mismatch: {room}')
    subprocess.run([str(root / 'build/boat_bot'), '--connect', room['native'], '--name',
                    'release-check', '--predict', '--seconds', '5'], check=True)
    evidence = dict(tag=args.tag, commit=commit, release=release['url'], directory=directory,
                    health=health, hosting_commit=run('git', 'rev-parse', 'HEAD', cwd=hosting))
    (output / 'verification.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(f'Release and hosted cutover verified: {release["url"]}\n'
          f'Evidence: {output / "verification.json"}\n'
          'Update release/hosting documentation with these results and manual-review limits.')


if __name__ == '__main__':
    main()
