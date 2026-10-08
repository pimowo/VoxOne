"""Host-independent checks for the Web Update browser resolver."""

import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from voxone_update_headless import resolve_chrome


class ChromeResolverTest(unittest.TestCase):
    def test_chrome_bin_takes_priority(self):
        with patch.dict(os.environ, {"CHROME_BIN": sys.executable}):
            with patch("voxone_update_headless.shutil.which") as which:
                which.return_value = sys.executable
                self.assertEqual(resolve_chrome(), Path(sys.executable).resolve())
                which.assert_called_once_with(sys.executable)

    def test_path_uses_first_available_browser(self):
        def which(name):
            return sys.executable if name == "chromium-browser" else None

        with patch.dict(os.environ, {"CHROME_BIN": ""}):
            with patch("voxone_update_headless.shutil.which", side_effect=which):
                self.assertEqual(resolve_chrome(), Path(sys.executable).resolve())

    def test_invalid_chrome_bin_is_reported(self):
        with patch.dict(os.environ, {"CHROME_BIN": "/missing/browser"}):
            with patch("voxone_update_headless.shutil.which", return_value=None):
                with self.assertRaisesRegex(FileNotFoundError, "CHROME_BIN"):
                    resolve_chrome()


if __name__ == "__main__":
    unittest.main()
