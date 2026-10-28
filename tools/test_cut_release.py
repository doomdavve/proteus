#!/usr/bin/env python3
"""Offline guards for immutable release artifacts and the hosting version pin."""
import hashlib
from pathlib import Path
import tempfile
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


if __name__ == '__main__':
    unittest.main()
