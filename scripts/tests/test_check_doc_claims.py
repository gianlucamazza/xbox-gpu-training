"""Unit tests for scripts/check_doc_claims.py."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_doc_claims as dc  # noqa: E402

CLAIMS = [
    dc.Claim("run/throughput.json", "tokens_per_second", "{:.3f}"),
    dc.Claim("run/acceptance.json", "len:fixtures", unique=False),
]


class CheckDocClaims(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        run = self.root / "docs/evidence/run"
        run.mkdir(parents=True)
        (run / "throughput.json").write_text(json.dumps({"tokens_per_second": 1234.56789}))
        (run / "acceptance.json").write_text(json.dumps({"fixtures": [1, 2, 3]}))
        (run / "notes.md").write_text("Measured 1234.568 token/s.\n")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def write(self, rel: str, text: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def test_matching_status_passes(self) -> None:
        self.write("docs/status.md", "| tok/s | 1234.568 |\n| fixtures | 3 |\n")
        self.assertEqual(dc.check(self.root, CLAIMS), [])

    def test_stale_status_fails(self) -> None:
        self.write("docs/status.md", "| tok/s | 1200.000 |\n| fixtures | 3 |\n")
        errors = dc.check(self.root, CLAIMS)
        self.assertEqual(len(errors), 1)
        self.assertIn("does not state run/throughput.json:tokens_per_second = 1234.568", errors[0])

    def test_copy_outside_status_fails(self) -> None:
        self.write("docs/status.md", "1234.568 and 3\n")
        self.write("README.md", "We reach 1234.568 token/s.\n")
        errors = dc.check(self.root, CLAIMS)
        self.assertEqual(errors, ["README.md: copies run/throughput.json:tokens_per_second = 1234.568; link to docs/status.md instead"])

    def test_missing_evidence_key_fails(self) -> None:
        self.write("docs/status.md", "1234.568 3\n")
        errors = dc.check(self.root, CLAIMS + [dc.Claim("run/throughput.json", "missing")])
        self.assertTrue(any("cannot read evidence" in e for e in errors))


class RepositoryClaims(unittest.TestCase):
    def test_repository_docs_match_evidence(self) -> None:
        self.assertEqual(dc.check(dc.ROOT, dc.CLAIMS), [])


if __name__ == "__main__":
    unittest.main()
