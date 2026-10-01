"""Unit tests for scripts/sync_floppylm_contracts.py and check_floppylm_contracts.py."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_floppylm_contracts as check  # noqa: E402
import sync_floppylm_contracts as sync  # noqa: E402

SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "$id": "https://example.test/floppylm.device.v1.json",
    "type": "object",
    "required": ["schema", "state"],
    "properties": {"schema": {"const": "floppylm.device.v1"}, "state": {"const": "ready"}},
    "additionalProperties": False,
}
GOOD = {"schema": "floppylm.device.v1", "state": "ready"}
BAD = {"schema": "floppylm.device.v1", "state": "broken"}


def write(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value))


class Contracts(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.source, self.root = base / "floppylm", base / "backend"
        write(self.source / "schemas/floppylm.device.v1.json", SCHEMA)
        write(self.source / "tests/fixtures/contracts/valid/floppylm.device.v1--ready.json", GOOD)
        write(self.source / "tests/fixtures/contracts/invalid/floppylm.device.v1--bad.json", BAD)
        (self.source / "schemas/README.md").write_text("not vendored")
        self.commit = self.commit_all("contracts")
        write(self.root / "docs/evidence/run/acceptance.json", {"device": GOOD})
        self.pin = sync.sync(self.source, self.commit, self.root / "contracts/floppylm")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def commit_all(self, message: str) -> str:
        git = ["git", "-C", str(self.source), "-c", "user.name=t", "-c", "user.email=t@t"]
        if not (self.source / ".git").exists():
            subprocess.run([*git, "init", "-q"], check=True)
        subprocess.run([*git, "add", "-A"], check=True)
        subprocess.run([*git, "commit", "-q", "-m", message], check=True)
        return subprocess.run(
            [*git, "rev-parse", "HEAD"], check=True, capture_output=True, text=True
        ).stdout.strip()

    def errors(self) -> list[str]:
        return check.check(self.root)[0]

    def test_sync_pins_commit_and_json_only(self) -> None:
        self.assertEqual(self.pin["commit"], self.commit)
        self.assertEqual(
            sorted(self.pin["files"]),
            [
                "fixtures/invalid/floppylm.device.v1--bad.json",
                "fixtures/valid/floppylm.device.v1--ready.json",
                "schemas/floppylm.device.v1.json",
            ],
        )

    def test_clean_copy_passes(self) -> None:
        errors, counts = check.check(self.root)
        self.assertEqual(errors, [])
        self.assertEqual(counts, {"valid": 1, "invalid": 1, "evidence": 1})

    def test_local_edit_is_detected(self) -> None:
        write(self.root / "contracts/floppylm/schemas/floppylm.device.v1.json", {**SCHEMA, "x": 1})
        self.assertIn("modified file schemas/floppylm.device.v1.json", self.errors())

    def test_unpinned_and_missing_files_are_detected(self) -> None:
        pinned = self.root / "contracts/floppylm"
        write(pinned / "fixtures/valid/floppylm.device.v1--extra.json", GOOD)
        (pinned / "fixtures/invalid/floppylm.device.v1--bad.json").unlink()
        errors = self.errors()
        self.assertIn("unpinned file fixtures/valid/floppylm.device.v1--extra.json", errors)
        self.assertIn("missing file fixtures/invalid/floppylm.device.v1--bad.json", errors)

    def test_invalid_fixture_that_validates_fails(self) -> None:
        write(self.source / "tests/fixtures/contracts/invalid/floppylm.device.v1--bad.json", GOOD)
        sync.sync(self.source, self.commit_all("weak fixture"), self.root / "contracts/floppylm")
        self.assertIn("invalid fixture floppylm.device.v1--bad.json was accepted", self.errors())

    def test_nonconforming_native_evidence_fails(self) -> None:
        write(self.root / "docs/evidence/run/acceptance.json", {"device": BAD})
        self.assertTrue(
            any(e.startswith("docs/evidence/run/acceptance.json") for e in self.errors())
        )


if __name__ == "__main__":
    unittest.main()
