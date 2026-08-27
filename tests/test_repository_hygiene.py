#!/usr/bin/env python3

import pathlib
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


if __name__ == "__main__":
    unittest.main()
