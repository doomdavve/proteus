#!/usr/bin/env python3
"""Publish a checked commit, verify downloads, and upgrade the matched Winterfell pair."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import zipfile

PIN = 'ansible/host_vars/winterfell/main.yml'
HOSTED = 'https://wake.davve.net/'
PAGES = 'https://doomdavve.github.io/proteus/'
CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'


def run(*args, cwd=None, env=None, log=None, timeout=120):
    print('+', *map(str, args), flush=True)
    if log is not None:
        print(f'  log: {log}', flush=True)
        with log.open('w') as stream:
            subprocess.run(list(map(str, args)), cwd=cwd, env=env, stdout=stream,
                           stderr=subprocess.STDOUT, check=True, timeout=timeout)
        return log.read_text().strip()
    return subprocess.check_output(list(map(str, args)), cwd=cwd, env=env,
                                   text=True, timeout=timeout).strip()


def ref(repo, name):
    result = subprocess.run(['git', 'rev-parse', '--verify', name], cwd=repo,
                            capture_output=True, text=True, timeout=30)
    return result.stdout.strip() if result.returncode == 0 else None


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
        fields = dict(line.split(': ', 1) for line in
                      package.read(metadata[0]).decode('utf-8-sig').splitlines() if ': ' in line)
        if fields.get('Version') != tag or fields.get('Commit') != commit:
            raise ValueError(f'Package identity mismatch: {archive}: {fields}')
    return digest


def version_pin(text, tag):
    updated, count = re.subn(r'(?m)^wake_velocity_version:.*$',
                             f'wake_velocity_version: {tag}', text)
    if count != 1:
        raise ValueError('Expected exactly one wake_velocity_version pin')
    return updated


def require_source(root, tag, verify_only=False):
    """A new release uses main; a retry may use its immutable tag after main advances."""
    commit = run('git', 'rev-parse', 'HEAD', cwd=root)
    existing = ref(root, f'refs/tags/{tag}^{{commit}}')
    if existing is not None and existing != commit:
        raise RuntimeError('Existing tag points elsewhere; check out that tag to resume it')
    if existing is None:
        if verify_only:
            raise RuntimeError('Verification requires an existing published tag')
        if commit != ref(root, 'refs/remotes/origin/main'):
            raise RuntimeError('Create new releases from the reviewed origin/main commit')
    else:
        run('git', 'merge-base', '--is-ancestor', commit, 'origin/main', cwd=root)
    return commit, existing is not None


def require_hosting(hosting, branch, tag):
    run('git', 'check-ref-format', '--branch', branch, cwd=hosting)
    if ref(hosting, f'refs/remotes/origin/{branch}') is None:
        raise RuntimeError(f'Hosting origin/{branch} does not exist')
    # The wrapper requires a branch/upstream. The source release worktree can be detached.
    run('git', 'symbolic-ref', '--quiet', '--short', 'HEAD', cwd=hosting)
    run('git', 'rev-parse', '--abbrev-ref', '@{u}', cwd=hosting)
    run('git', 'merge-base', '--is-ancestor', f'origin/{branch}', 'HEAD', cwd=hosting)
    baseline_changes = run('git', 'diff', '--name-only', f'origin/{branch}', 'HEAD',
                           cwd=hosting).splitlines()
    if any(path != PIN for path in baseline_changes):
        raise RuntimeError('Merge unrelated hosting work into its baseline before releasing')
    ahead = run('git', 'rev-list', '--count', '@{u}..HEAD', cwd=hosting)
    if ahead != '0':
        # Permit retrying our own pin commit if its push failed. Never publish unrelated work.
        changes = run('git', 'diff', '--name-only', '@{u}', 'HEAD', cwd=hosting).splitlines()
        text = (hosting / PIN).read_text()
        if changes != [PIN] or text != version_pin(text, tag):
            raise RuntimeError('Push unrelated hosting commits before releasing')
    # Validate the pin before any tag is pushed, including on the first deployment.
    version_pin((hosting / PIN).read_text(), tag)


def preflight(root, args):
    for command in ('git', 'gh', 'cmake', 'ctest', 'curl', 'ssh', 'ansible-playbook'):
        if shutil.which(command) is None:
            raise RuntimeError(f'Missing required command: {command}')
    for repo in (root, args.hosting_repo):
        if run('git', 'status', '--porcelain', cwd=repo):
            raise RuntimeError(f'Commit changes before releasing: {repo}')
        run('git', 'fetch', 'origin', '--tags', cwd=repo)
    commit, exists = require_source(root, args.tag, args.verify_only)
    require_hosting(args.hosting_repo, args.hosting_branch, args.tag)
    if not (args.hosting_repo / 'scripts/ansible-run.sh').is_file():
        raise RuntimeError('Missing hosting scripts/ansible-run.sh')
    if not args.vault_password_file.is_file():
        raise RuntimeError('Set --vault-password-file or ANSIBLE_VAULT_PASSWORD_FILE to the '
                           'existing controller vault file')
    if not os.access(args.chrome, os.X_OK):
        raise RuntimeError('Set --chrome to an installed Google Chrome executable')
    run(args.browser_python, '-c', 'import playwright.sync_api')
    run('gh', 'auth', 'status')
    run('ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', args.ssh_host, 'sudo -n true')
    return commit, exists


def wait_for_publication(root, tag, commit, timeout):
    deadline = time.monotonic() + timeout
    workflows = ('release.yml', 'web-pages.yml')
    reported = set()
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
            elif workflow not in reported:
                print(matching[0]['url'], flush=True)
                reported.add(workflow)
        if complete:
            return
        time.sleep(min(15, max(0, deadline - time.monotonic())))
    raise TimeoutError('Publication deadline exceeded; check Actions, then rerun the same tag')


def build_and_test(root, output):
    run('cmake', '-S', root, '-B', root / 'build', '-DCMAKE_BUILD_TYPE=Debug',
        '-DPROTEUS_GPU_TESTS=OFF', cwd=root, log=output / 'configure.log', timeout=1800)
    run('cmake', '--build', root / 'build', '--parallel', cwd=root,
        log=output / 'build.log', timeout=1800)
    run('ctest', '--preset', 'full', cwd=root, log=output / 'tests.log', timeout=1800)


def publish(root, args, commit, exists):
    if not exists:
        run('git', 'tag', '-a', args.tag, '-m', f'Wake Velocity {args.tag}', cwd=root)
    run('git', 'push', 'origin', f'refs/tags/{args.tag}', cwd=root)
    wait_for_publication(root, args.tag, commit, args.timeout)


def download(root, args, commit):
    release = json.loads(run('gh', 'release', 'view', args.tag, '--json', 'isDraft,url', cwd=root))
    if release['isDraft']:
        raise RuntimeError('Release is still a draft')
    run('gh', 'release', 'download', args.tag, '--dir', args.output, '--clobber', cwd=root)
    checksums = {}
    for platform in ('macos-arm64', 'windows-x64'):
        archive = args.output / f'wake_velocity-{args.tag}-{platform}.zip'
        checksums[archive.name] = verify_archive(archive, args.tag, commit)
    return release['url'], checksums


def deploy(args):
    hosting = args.hosting_repo
    pin = hosting / PIN
    previous = pin.read_text()
    updated = version_pin(previous, args.tag)
    if previous != updated:
        pin.write_text(updated)
        run('git', 'add', PIN, cwd=hosting)
        run('git', 'commit', '-m', f'Upgrade Winterfell Wake Velocity to {args.tag}', cwd=hosting)
    # Keep both the worktree's upstream and the private hosting baseline on the release.
    # These ordinary pushes reject concurrent changes; never force or silently rebase.
    run('git', 'push', '-u', 'origin', 'HEAD', cwd=hosting)
    run('git', 'push', 'origin', f'HEAD:refs/heads/{args.hosting_branch}', cwd=hosting)
    env = dict(os.environ, ANSIBLE_VAULT_PASSWORD_FILE=str(args.vault_password_file))
    run('scripts/ansible-run.sh', '--limit', 'winterfell', '--tags', 'wake_velocity',
        cwd=hosting, env=env, log=args.output / 'deployment.log', timeout=3600)


def verify(root, args, commit):
    # Never log ExecStart: it includes the private TURN credential.
    health = run('ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', args.ssh_host,
                 'systemctl is-active wake-velocity wake-velocity-relay && '
                 'systemctl show wake-velocity --property=Description --value && '
                 f'sudo -n -u wake-velocity git -C /srv/wake-velocity/releases/{args.tag} rev-parse HEAD')
    if (health.splitlines()[:2] != ['active', 'active']
            or f' {args.tag} ' not in health or health.splitlines()[-1] != commit):
        raise RuntimeError(f'Unexpected running release: {health}')
    directory = json.loads(run('curl', '-fsS', '--max-time', '30', HOSTED + 'servers.json'))
    protocol = int(re.search(r'#define BOAT_NET_VERSION (\d+)',
                            (root / 'src/boat_net_protocol.h').read_text())[1])
    physics = int(re.search(r'#define BOAT_PHYSICS_REVISION (\d+)',
                           (root / 'src/boat.h').read_text())[1])
    room = next((s for s in directory['servers'] if s['name'] == 'Winterfell'), None)
    if room is None:
        raise RuntimeError('Winterfell is missing from the public server directory')
    if (room['protocol'], room['physics']) != (protocol, physics):
        raise RuntimeError(f'Directory mismatch: {room}')
    for name, url in (('hosted', HOSTED), ('pages', PAGES)):
        html = run('curl', '-fsS', '--max-time', '30', url)
        if f'Version {args.tag}<' not in html:
            raise RuntimeError(f'Browser release mismatch: {url}')
        run(args.browser_python, root / 'tools/check_web_server_directory.py',
            '--url', url, '--output', args.output / name, '--executable', args.chrome,
            log=args.output / f'{name}.log', timeout=180)
    native = run(root / 'build/boat_bot', '--connect', room['native'], '--name',
                 'release-check', '--predict', '--seconds', '5',
                 log=args.output / 'native.log', timeout=30)
    if not ('boat_bot joined:' in native and 'final=consistent' in native
            and re.search(r'\bdecode_errors=0\b', native)):
        raise RuntimeError('Native join/state check failed; see native.log')
    return dict(directory=directory, health=health,
                hosting_commit=run('git', 'rev-parse', 'HEAD', cwd=args.hosting_repo))


def execute(root, args):
    if args.check:
        commit, _ = preflight(root, args)
        print(f'Release source: {commit}; hosting baseline: origin/{args.hosting_branch}', flush=True)
        print('Preflight passed. No tag, release or hosting changes made.')
        return
    args.output.mkdir(parents=True, exist_ok=True)
    evidence = dict(tag=args.tag, status='running', stage='preflight',
                    mode='verify-only' if args.verify_only else 'release',
                    started_at=datetime.now(timezone.utc).isoformat())
    evidence_file = args.output / 'verification.json'

    def save():
        evidence_file.write_text(json.dumps(evidence, indent=2) + '\n')

    save()  # Replace any previous success before a retry begins.
    try:
        commit, exists = preflight(root, args)
        print(f'Release source: {commit}; hosting baseline: origin/{args.hosting_branch}', flush=True)
        evidence.update(commit=commit, stage='build')
        save()
        build_and_test(root, args.output)
        if not args.verify_only:
            evidence['stage'] = 'publication'
            save()
            publish(root, args, commit, exists)
        evidence['stage'] = 'downloads'
        save()
        evidence['release'], evidence['checksums'] = download(root, args, commit)
        if not args.verify_only:
            evidence['stage'] = 'deployment'
            save()
            deploy(args)
        evidence['stage'] = 'verification'
        save()
        evidence.update(verify(root, args, commit))
        evidence.update(status='passed', stage='complete',
                        completed_at=datetime.now(timezone.utc).isoformat())
        save()
    except BaseException:
        evidence['status'] = 'failed'
        save()
        raise
    print(f'Release and hosted cutover verified: {evidence["release"]}\n'
          f'Evidence: {evidence_file}\n'
          'Update release/hosting documentation with these results and manual-review limits.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag', help='Immutable vMAJOR.MINOR.PATCH tag (optional prerelease suffix)')
    parser.add_argument('--hosting-repo', required=True, type=Path,
                        help='Clean davve-srv controller worktree on a branch with an upstream')
    parser.add_argument('--hosting-branch', default='main', help='Private hosting baseline to update')
    parser.add_argument('--vault-password-file', type=Path,
                        help='Existing controller vault file; defaults to environment or hosting repo')
    parser.add_argument('--output', type=Path, help='Defaults to source build/releases/TAG')
    parser.add_argument('--timeout', type=int, default=3600, help='Publication deadline in seconds')
    parser.add_argument('--browser-python', default=sys.executable, help='Python with Playwright')
    parser.add_argument('--chrome', default=CHROME, help='Installed Google Chrome executable')
    parser.add_argument('--ssh-host', default='winterfell', help='Winterfell SSH alias')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='Check prerequisites without publishing/deploying')
    mode.add_argument('--verify-only', action='store_true', help='Check existing release without tag/pin/service changes')
    args = parser.parse_args(argv)
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', args.tag):
        parser.error('Expected a filename-safe semantic version tag')
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    root = Path(__file__).resolve().parents[1]
    args.hosting_repo = args.hosting_repo.resolve()
    args.output = (args.output or root / 'build/releases' / args.tag).resolve()
    args.vault_password_file = (args.vault_password_file or Path(os.environ.get(
        'ANSIBLE_VAULT_PASSWORD_FILE', args.hosting_repo / 'ansible/.vault_pass.txt'))).resolve()
    try:
        execute(root, args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f'Release stopped: {error}\nSee {args.output} for available stage logs; '
                       'resolve the failure and rerun the same tag.\n')


if __name__ == '__main__':
    main()
