"""Unit tests for heading anchors in scripts/check_relative_links.py."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_relative_links as cl  # noqa: E402


class Slug(unittest.TestCase):
    def test_github_slugs(self) -> None:
        self.assertEqual(cl.slug("BitNet / peer comparison"), "bitnet--peer-comparison")
        self.assertEqual(cl.slug("F1 — Matmul parity vs tile"), "f1--matmul-parity-vs-tile")
        self.assertEqual(cl.slug("`dxc` (DirectX Shader Compiler)"), "dxc-directx-shader-compiler")
        self.assertEqual(cl.slug("4. Accept"), "4-accept")

    def test_duplicates_and_fences(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            page = Path(tmp) / "p.md"
            page.write_text("# Notes\n## Notes\n```\n# not a heading\n```\n")
            self.assertEqual(cl.anchors(page, {}), {"notes", "notes-1"})


if __name__ == "__main__":
    unittest.main()
