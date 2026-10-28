#!/usr/bin/env python3
"""Offline guards for immutable release artifacts and the hosting version pin."""
import hashlib
from pathlib import Path
import tempfile
import subprocess
from types import SimpleNamespace
from unittest import mock
import json

import cut_release as release
import unittest
import zipfile

from cut_release import verify_archive, version_pin


class ReleaseChecks(unittest.TestCase):
    def test_pin_requires_one_exact_key(self):
        self.assertEqual(version_pin('other: keep\nwake_velocity_version: v0.0.20\n', 'v0.0.21'),
                         'other: keep\nwake_velocity_version: v0.0.21\n')
        for text in ('other: value\n', 'wake_velocity_version: a\nwake_velocity_version: b\n'):
            with self.assertRaises(ValueError):
                version_pin(text, 'v0.0.21')

    def test_package_identity_and_integrity(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'wake_velocity-v0.0.21-macos-arm64.zip'
            with zipfile.ZipFile(archive, 'w') as package:
                package.writestr('Wake Velocity.app/Contents/Resources/build.txt',
                                 'Version: v0.0.21\nCommit: abc123\n')
            checksum = archive.with_suffix('.zip.sha256')
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            checksum.write_text(f'{digest}  {archive.name}\n')
            verify_archive(archive, 'v0.0.21', 'abc123')
            for tag, commit in [('v0.0.20', 'abc123'), ('v0.0.21', 'wrong')]:
                with self.assertRaises(ValueError):
                    verify_archive(archive, tag, commit)
            checksum.write_text(f'{digest}  wrong.zip\n')
            with self.assertRaises(ValueError):
                verify_archive(archive, 'v0.0.21', 'abc123')
            checksum.write_text(f'{"0" * 64}  {archive.name}\n')
            with self.assertRaises(ValueError):
                verify_archive(archive, 'v0.0.21', 'abc123')

    def test_windows_bom_and_ambiguous_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'windows.zip'
            for names in [('app/build.txt',), (), ('a/build.txt', 'b/build.txt')]:
                with self.subTest(names=names):
                    with zipfile.ZipFile(archive, 'w') as package:
                        for name in names:
                            package.writestr(name, '\ufeffVersion: v1.0.0\r\nCommit: abc123\r\n')
                    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
                    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n')
                    if len(names) == 1:
                        self.assertEqual(verify_archive(archive, 'v1.0.0', 'abc123'), digest)
                    else:
                        with self.assertRaisesRegex(ValueError, 'one build.txt'):
                            verify_archive(archive, 'v1.0.0', 'abc123')


class GitGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git('init', '-b', 'candidate')
        self.git('config', 'user.name', 'Release test')
        self.git('config', 'user.email', 'release@example.invalid')
        self.git('config', 'commit.gpgsign', 'false')
        self.pin = self.root / release.PIN
        self.pin.parent.mkdir(parents=True)
        self.pin.write_text('wake_velocity_version: v1.0.0\n')
        self.git('add', '.')
        self.git('commit', '-m', 'baseline')
        self.commit = self.git('rev-parse', 'HEAD')
        self.git('remote', 'add', 'origin', str(self.root / 'unused-remote'))
        self.git('update-ref', 'refs/remotes/origin/main', self.commit)
        self.git('branch', '--set-upstream-to', 'origin/main')

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.root, text=True,
                                       stderr=subprocess.PIPE).strip()

    def test_new_release_accepts_detached_main(self):
        self.git('checkout', '--detach')
        self.assertEqual(release.require_source(self.root, 'v1.0.1'), (self.commit, False))

    def test_retry_accepts_tag_after_main_advances(self):
        self.git('tag', 'v1.0.0')
        self.pin.write_text('wake_velocity_version: v1.0.1\n')
        self.git('commit', '-am', 'later main')
        self.git('update-ref', 'refs/remotes/origin/main', self.git('rev-parse', 'HEAD'))
        self.git('checkout', '--detach', 'v1.0.0')
        self.assertEqual(release.require_source(self.root, 'v1.0.0'), (self.commit, True))

    def test_existing_tag_cannot_move(self):
        self.git('tag', 'v1.0.0')
        self.pin.write_text('wake_velocity_version: v1.0.1\n')
        self.git('commit', '-am', 'different source')
        with self.assertRaisesRegex(RuntimeError, 'Existing tag'):
            release.require_source(self.root, 'v1.0.0')

    def test_new_release_requires_main_and_verify_requires_tag(self):
        with self.assertRaisesRegex(RuntimeError, 'existing published tag'):
            release.require_source(self.root, 'v1.0.1', verify_only=True)
        self.pin.write_text('wake_velocity_version: v1.0.1\n')
        self.git('commit', '-am', 'unmerged candidate')
        with self.assertRaisesRegex(RuntimeError, 'origin/main'):
            release.require_source(self.root, 'v1.0.1')

    def test_hosting_can_retry_unpushed_pin(self):
        release.require_hosting(self.root, 'main', 'v1.0.1')
        self.pin.write_text('wake_velocity_version: v1.0.1\n')
        self.git('commit', '-am', 'Upgrade Winterfell Wake Velocity to v1.0.1')
        release.require_hosting(self.root, 'main', 'v1.0.1')
        with self.assertRaisesRegex(RuntimeError, 'unrelated hosting commits'):
            release.require_hosting(self.root, 'main', 'v1.0.2')

    def test_hosting_rejects_unrelated_and_stale_baseline(self):
        (self.root / 'unrelated').write_text('new config')
        self.git('add', '.')
        self.git('commit', '-m', 'unrelated work')
        with self.assertRaisesRegex(RuntimeError, 'unrelated hosting work'):
            release.require_hosting(self.root, 'main', 'v1.0.1')
        self.git('update-ref', 'refs/remotes/origin/main', self.git('rev-parse', 'HEAD'))
        self.git('checkout', '--detach', self.commit)
        self.git('checkout', '-b', 'stale')
        self.git('branch', '--set-upstream-to', 'origin/main')
        with self.assertRaises(subprocess.CalledProcessError):
            release.require_hosting(self.root, 'main', 'v1.0.1')


class Orchestration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.args = SimpleNamespace(tag='v1.0.1', hosting_branch='main',
                                    output=self.root / 'evidence', check=False, verify_only=False)
        self.stages = {}
        for name in ('preflight', 'build_and_test', 'publish', 'download', 'deploy', 'verify'):
            patch = mock.patch.object(release, name)
            self.stages[name] = patch.start()
            self.addCleanup(patch.stop)
        self.stages['preflight'].return_value = ('abc123', False)
        self.stages['download'].return_value = ('https://example.invalid/release', {'zip': 'sha256'})
        self.stages['verify'].return_value = {'health': 'active'}

    def evidence(self):
        return json.loads((self.args.output / 'verification.json').read_text())

    def test_preflight_only_makes_no_output_or_changes(self):
        self.args.check = True
        release.execute(self.root, self.args)
        self.assertFalse(self.args.output.exists())
        for name, stage in self.stages.items():
            if name != 'preflight':
                stage.assert_not_called()

    def test_verify_only_never_publishes_or_deploys(self):
        self.args.verify_only = True
        release.execute(self.root, self.args)
        self.stages['publish'].assert_not_called()
        self.stages['deploy'].assert_not_called()
        self.stages['build_and_test'].assert_called_once()
        self.assertEqual(self.evidence()['status'], 'passed')

    def test_preflight_failure_replaces_stale_success(self):
        release.execute(self.root, self.args)
        self.stages['preflight'].side_effect = RuntimeError('dirty worktree')
        with self.assertRaisesRegex(RuntimeError, 'dirty worktree'):
            release.execute(self.root, self.args)
        self.assertEqual(self.evidence()['status'], 'failed')
        self.assertEqual(self.evidence()['stage'], 'preflight')
        self.assertNotIn('completed_at', self.evidence())

    def test_failure_gates_and_retry_evidence(self):
        names = ['build_and_test', 'publish', 'download', 'deploy', 'verify']
        stages = ['build', 'publication', 'downloads', 'deployment', 'verification']
        for index, name in enumerate(names):
            with self.subTest(stage=name):
                for stage in self.stages.values():
                    stage.reset_mock(side_effect=True)
                self.stages[name].side_effect = RuntimeError('test failure')
                with self.assertRaisesRegex(RuntimeError, 'test failure'):
                    release.execute(self.root, self.args)
                self.assertEqual(self.evidence()['status'], 'failed')
                self.assertEqual(self.evidence()['stage'], stages[index])
                for later in names[index + 1:]:
                    self.stages[later].assert_not_called()
        self.stages['verify'].side_effect = None
        release.execute(self.root, self.args)
        self.assertEqual(self.evidence()['status'], 'passed')
        self.assertIn('completed_at', self.evidence())


class Deployment(unittest.TestCase):
    def test_baseline_push_precedes_deployment_and_same_pin_retry_skips_commit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pin = root / release.PIN
            pin.parent.mkdir(parents=True)
            pin.write_text('wake_velocity_version: v1.0.0\n')
            args = SimpleNamespace(hosting_repo=root, tag='v1.0.1', hosting_branch='main',
                                   vault_password_file=root / 'vault', output=root)
            with mock.patch.object(release, 'run') as run:
                release.deploy(args)
                commands = [call.args for call in run.call_args_list]
                self.assertEqual(commands[-2], ('git', 'push', 'origin', 'HEAD:refs/heads/main'))
                self.assertEqual(commands[-1][0], 'scripts/ansible-run.sh')
                self.assertNotIn('--force', str(commands))
                self.assertEqual(pin.read_text(), 'wake_velocity_version: v1.0.1\n')
                run.reset_mock()
                release.deploy(args)
                self.assertFalse(any('commit' in call.args for call in run.call_args_list))

    def test_rejected_baseline_push_never_deploys(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pin = root / release.PIN
            pin.parent.mkdir(parents=True)
            pin.write_text('wake_velocity_version: v1.0.1\n')
            args = SimpleNamespace(hosting_repo=root, tag='v1.0.1', hosting_branch='main')
            def reject(*command, **kwargs):
                if command[-1] == 'HEAD:refs/heads/main':
                    raise subprocess.CalledProcessError(1, command)
            with mock.patch.object(release, 'run', side_effect=reject) as run:
                with self.assertRaises(subprocess.CalledProcessError):
                    release.deploy(args)
                self.assertFalse(any(call.args[0] == 'scripts/ansible-run.sh'
                                     for call in run.call_args_list))


class PublicationWait(unittest.TestCase):
    def test_missing_runs_then_success_with_virtual_clock(self):
        clock = [0]
        def advance(seconds):
            clock[0] += seconds
        success = dict(headBranch='v1.0.1', status='completed', conclusion='success', url='run')
        unrelated = dict(success, headBranch='main')
        with mock.patch.object(release.time, 'monotonic', side_effect=lambda: clock[0]), \
                mock.patch.object(release.time, 'sleep', side_effect=advance), \
                mock.patch.object(release, 'run', side_effect=[json.dumps([unrelated]), '[]',
                                                              json.dumps([success]), json.dumps([success])]):
            release.wait_for_publication(self, 'v1.0.1', 'abc123', 60)
        self.assertEqual(clock[0], 15)

    def test_failed_workflow_and_timeout(self):
        failed = dict(headBranch='v1.0.1', status='completed', conclusion='failure', url='failed-run')
        with mock.patch.object(release, 'run', return_value=json.dumps([failed])):
            with self.assertRaisesRegex(RuntimeError, 'failed-run'):
                release.wait_for_publication(self, 'v1.0.1', 'abc123', 60)
        with mock.patch.object(release.time, 'monotonic', side_effect=[0, 61]):
            with self.assertRaises(TimeoutError):
                release.wait_for_publication(self, 'v1.0.1', 'abc123', 60)


if __name__ == '__main__':
    unittest.main()
