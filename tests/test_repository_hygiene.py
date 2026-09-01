#!/usr/bin/env python3

import pathlib
import re
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SESSION_PATHS = (
    ".superpowers/brainstorm/.last-token",
    ".superpowers/brainstorm/.last-port",
)


class RepositoryHygieneTest(unittest.TestCase):
    def git(self, *args):
        return subprocess.run(
            ("git",) + args,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_superpowers_session_tree_is_ignored(self):
        for path in SESSION_PATHS:
            with self.subTest(path=path):
                self.assertEqual(
                    0,
                    self.git("check-ignore", "--quiet", "--no-index", path).returncode,
                )

    def test_no_superpowers_session_file_is_tracked(self):
        tracked = self.git("ls-files", "--", ".superpowers").stdout.strip()
        self.assertEqual("", tracked)

    def test_third_party_notices_ship_in_the_package(self):
        notices = ROOT / "LICENSE-third-party.txt"
        self.assertTrue(notices.is_file(), "third-party notices file is missing")
        text = notices.read_text()
        self.assertIn("MIT License", text)
        self.assertIn("Copyright (c) 2025 Yuriy Ivantsov", text)

        # The Makefile packages top-level files matching LICENSE*; the notices
        # file must match that wildcard or it silently drops from the package.
        makefile = (ROOT / "Makefile").read_text()
        self.assertIn("$(wildcard LICENSE*)", makefile)
        self.assertIn(
            "LICENSE-third-party.txt", [p.name for p in ROOT.glob("LICENSE*")]
        )

    def test_read_broadcast_sanitizes_non_finite_voltages(self):
        module_sources = (
            "src/Four/Four.cpp",
            "src/FourV2/FourV2.cpp",
            "src/Vortex/Vortex.cpp",
            "src/VortexV2/VortexV2.cpp",
            "src/Brink/Brink.cpp",
            "src/BrinkV2/BrinkV2.cpp",
        )
        for rel in module_sources:
            with self.subTest(module=rel):
                source = (ROOT / rel).read_text()
                match = re.search(
                    r"static float readBroadcast\(.*?\n    \}",
                    source,
                    re.DOTALL,
                )
                self.assertIsNotNone(match, "readBroadcast not found")
                self.assertIn(
                    "finite_or",
                    match.group(0),
                    "readBroadcast must guard against non-finite voltages",
                )


if __name__ == "__main__":
    unittest.main()
