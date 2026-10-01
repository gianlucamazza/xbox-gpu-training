"""Unit tests for heading anchors in scripts/check_relative_links.py."""

from __future__ import annotations

import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest.mock import patch

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

    def test_main_validates_fragments(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "p.md").write_text("# Notes\n[valid](#notes)\n[missing](#absent)\n")
            (root / "q.md").write_text("[cross](p.md#notes)\n[bad](p.md#nope)\n")
            output = StringIO()
            with patch.object(cl, "ROOT", root), redirect_stdout(output):
                self.assertEqual(cl.main(), 1)
            report = output.getvalue()
            self.assertIn("p.md: missing anchor #absent in p.md", report)
            self.assertIn("q.md: missing anchor #nope in p.md", report)
            self.assertNotIn("#notes", report)


if __name__ == "__main__":
    unittest.main()
