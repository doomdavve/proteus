"""Exercise release publication with a fake GitHub CLI; no network or token needed."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class PublishReleaseTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.assets = self.root / "release assets"
        self.assets.mkdir()
        self.make_assets()
        self.log = self.root / "calls.jsonl"
        cli = self.root / "gh"
        cli.write_text("""#!/usr/bin/env python3
import json, os, sys
with open(os.environ['TEST_GH_LOG'], 'a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\\n')
command = sys.argv[2]
if command == 'view' and os.environ.get('TEST_GH_EXISTS') != '1':
    sys.exit(1)
if command == os.environ.get('TEST_GH_FAIL'):
    sys.exit(1)
""")
        cli.chmod(0o755)

    def make_assets(self, tag="v0.1.0"):
        for platform in ("macos-arm64", "windows-x64"):
            for suffix in (".zip", ".zip.sha256"):
                (self.assets / f"wake_velocity-{tag}-{platform}{suffix}").write_text("fixture")

    def publish(self, tag="v0.1.0", **options):
        env = os.environ.copy()
        env.update({"PATH": str(self.root) + os.pathsep + env["PATH"],
                    "TEST_GH_LOG": str(self.log), "TEST_GH_EXISTS": "0", "TEST_GH_FAIL": ""})
        env.update(options)
        result = subprocess.run(
            ["bash", str(Path(__file__).resolve().parents[2] / "tools" / "publish_release.sh"), tag, str(self.assets)],
            env=env, capture_output=True, text=True)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
        return result, calls

    def test_new_release_is_drafted_uploaded_then_published(self):
        result, calls = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([call[1] for call in calls], ["view", "create", "upload", "edit"])
        self.assertEqual(calls[1], ["release", "create", "v0.1.0", "--verify-tag", "--generate-notes", "--draft"])
        self.assertEqual(calls[2][-1], "--clobber")
        self.assertEqual(len(calls[2][3:-1]), 4)
        self.assertTrue(all(Path(asset).is_file() for asset in calls[2][3:-1]))
        self.assertEqual(calls[3], ["release", "edit", "v0.1.0", "--draft=false"])

    def test_prerelease_tag(self):
        self.make_assets("v0.1.0-rc.1")
        result, calls = self.publish("v0.1.0-rc.1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--prerelease", calls[1])
        self.assertTrue(all("wake_velocity-v0.1.0-rc.1-" in asset for asset in calls[2][3:-1]))

    def test_rerun_reuses_release_without_changing_notes(self):
        result, calls = self.publish(TEST_GH_EXISTS="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([call[1] for call in calls], ["view", "upload", "edit"])

    def test_failed_upload_does_not_publish(self):
        result, calls = self.publish(TEST_GH_FAIL="upload")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual([call[1] for call in calls], ["view", "create", "upload"])

    def test_failed_creation_does_not_upload(self):
        result, calls = self.publish(TEST_GH_FAIL="create")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual([call[1] for call in calls], ["view", "create"])

    def test_missing_asset_does_not_contact_github(self):
        (self.assets / "wake_velocity-v0.1.0-windows-x64.zip").unlink()
        result, calls = self.publish()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_other_version_assets_do_not_contact_github(self):
        result, calls = self.publish("v0.2.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_branch_name_does_not_contact_github(self):
        result, calls = self.publish("main")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main()
