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
    dc.Claim("run/throughput.json", "tokens_per_second", "Throughput", "{:.3f}"),
    dc.Claim("run/throughput.json", "tokens_per_second", None, "{:.0f}"),
    dc.Claim("run/acceptance.json", "len:fixtures", "Fixtures", unique=False),
]
GOOD_STATUS = "| Item | Value |\n| --- | --- |\n| Throughput | 1234.568 token/s |\n| Fixtures | 3 passed |\n"


class CheckDocClaims(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        run = self.root / "docs/evidence/run"
        run.mkdir(parents=True)
        # 1234.56789 rounds up to 1235: the rounded variant must not be required.
        (run / "throughput.json").write_text(json.dumps({"tokens_per_second": 1234.56789}))
        (run / "acceptance.json").write_text(json.dumps({"fixtures": [1, 2, 3]}))
        (run / "notes.md").write_text("Measured 1234.568 token/s (about 1235).\n")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def write(self, rel: str, text: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def test_matching_status_passes_without_rounded_copy(self) -> None:
        self.write("docs/status.md", GOOD_STATUS)
        self.assertEqual(dc.check(self.root, CLAIMS), [])

    def test_stale_status_fails(self) -> None:
        self.write("docs/status.md", GOOD_STATUS.replace("1234.568", "1200.000"))
        errors = dc.check(self.root, CLAIMS)
        self.assertEqual(errors, ["docs/status.md: row 'Throughput' does not state run/throughput.json:tokens_per_second = 1234.568"])

    def test_value_elsewhere_does_not_satisfy_row(self) -> None:
        # "3" appears in another row, but the Fixtures row is wrong.
        self.write("docs/status.md", GOOD_STATUS.replace("| 3 passed", "| 30 passed").replace("token/s", "token/s, 3 runs"))
        errors = dc.check(self.root, CLAIMS)
        self.assertEqual(errors, ["docs/status.md: row 'Fixtures' does not state run/acceptance.json:len:fixtures = 3"])

    def test_missing_row_fails(self) -> None:
        self.write("docs/status.md", GOOD_STATUS.replace("| Fixtures | 3 passed |\n", "3 fixtures passed.\n"))
        errors = dc.check(self.root, CLAIMS)
        self.assertEqual(errors, ["docs/status.md: missing row 'Fixtures' for run/acceptance.json:len:fixtures"])

    def test_copies_outside_status_fail(self) -> None:
        self.write("docs/status.md", GOOD_STATUS)
        self.write("README.md", "We reach 1234.568 token/s, roughly 1235 token/s.\n")
        errors = dc.check(self.root, CLAIMS)
        self.assertEqual(
            errors,
            [
                "README.md: copies run/throughput.json:tokens_per_second = 1234.568; link to docs/status.md instead",
                "README.md: copies run/throughput.json:tokens_per_second = 1235; link to docs/status.md instead",
            ],
        )

    def test_missing_evidence_key_fails(self) -> None:
        self.write("docs/status.md", GOOD_STATUS)
        errors = dc.check(self.root, CLAIMS + [dc.Claim("run/throughput.json", "missing", None)])
        self.assertTrue(any("cannot read evidence" in e for e in errors))


class RepositoryClaims(unittest.TestCase):
    def test_repository_docs_match_evidence(self) -> None:
        self.assertEqual(dc.check(dc.ROOT, dc.CLAIMS), [])


if __name__ == "__main__":
    unittest.main()
