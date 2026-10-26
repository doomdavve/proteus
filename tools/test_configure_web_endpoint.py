#!/usr/bin/env python3
"""Deployment configuration must preserve the selected release's compiled artifacts."""
from pathlib import Path
import tempfile
import unittest
from configure_web_endpoint import configure


class EndpointTests(unittest.TestCase):
    def test_only_launcher_configuration_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            index = root / 'index.html'
            index.write_text("Version v0.0.18\n  boatServerListUrl: 'https://old.example/servers.json',\n")
            wasm = root / 'game.wasm'
            wasm.write_bytes(b'compiled-release-fixture')
            configure(index, 'https://wake.davve.net/servers.json')
            self.assertIn('Version v0.0.18', index.read_text())
            self.assertIn('"https://wake.davve.net/servers.json"', index.read_text())
            self.assertEqual(wasm.read_bytes(), b'compiled-release-fixture')
            first = index.read_text()
            configure(index, 'https://wake.davve.net/servers.json')
            self.assertEqual(index.read_text(), first)

    def test_invalid_urls_leave_launcher_unchanged(self):
        for url in ['http://wake.davve.net/servers.json', 'https://user:secret@host/servers.json',
                    'https://wake.davve.net/</script>', 'https://wake.davve.net/servers.json?q=1']:
            with self.subTest(url=url), tempfile.TemporaryDirectory() as directory:
                index = Path(directory) / 'index.html'
                index.write_text("boatServerListUrl: 'https://old.example/servers.json',\n")
                before = index.read_text()
                with self.assertRaises(ValueError):
                    configure(index, url)
                self.assertEqual(index.read_text(), before)

    def test_older_client_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            index = Path(directory) / 'index.html'
            index.write_text('Version v0.0.17')
            with self.assertRaises(ValueError):
                configure(index, 'https://wake.davve.net/servers.json')


if __name__ == '__main__':
    unittest.main()
