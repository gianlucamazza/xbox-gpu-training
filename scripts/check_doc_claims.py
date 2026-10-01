#!/usr/bin/env python3
"""Check that headline numbers in the docs match the committed evidence JSON.

Link checks prove that cited files exist; this proves that cited values are true.
Each claim renders a value from an evidence file and names the docs/status.md
table row (first cell) that must state it. "Unique" values must not be copied into
other pages, because a copy is how a number silently goes stale.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATUS = "docs/status.md"
# Pages allowed to repeat unique values besides STATUS (evidence notes are records).
ALLOWED = ("docs/evidence/", "docs/history.md")
SKIP_DIRS = {".git", "build", "out", "__pycache__"}
CELL_SPLIT = re.compile(r"(?<!\\)\|")


@dataclass(frozen=True)
class Claim:
    evidence: str  # path under docs/evidence/
    key: str  # dotted key; "len:<key>" counts a list
    row: str | None  # status table row that must state the value; None = copy detection only
    fmt: str = "{}"
    unique: bool = True  # forbid copies outside STATUS / ALLOWED


ACTIVE = "e0-20261001-dashboard"
PREVIOUS = "e0-20261001-resident"
PREVIOUS_ROW = "Previous package, same benchmark (`0.1.0.28`)"
CLAIMS = [
    Claim(f"{ACTIVE}/package-lineage.json", "package", "Package"),
    Claim(f"{ACTIVE}/package-lineage.json", "source_commit", "Installed source"),
    Claim(f"{ACTIVE}/package-lineage.json", "ci_run", "CI run", unique=False),
    Claim(f"{ACTIVE}/throughput.json", "tokens_per_second", "Representative throughput", "{:.3f}"),
    Claim(f"{ACTIVE}/throughput.json", "tokens_per_second", None, "{:.0f}"),  # rounded copies
    Claim(f"{ACTIVE}/throughput.json", "peak_memory_bytes", "Peak app memory (same run)"),
    Claim(f"{ACTIVE}/kernel-parity.json", "case_count", "Independent GPU operation cases", unique=False),
    Claim(f"{ACTIVE}/acceptance.json", "len:fixtures", "Held-out model fixtures", unique=False),
    Claim(f"{ACTIVE}/bit-identity.json", "acceptance_cases.compared", "Bit identity with `0.1.0.28`", unique=False),
    Claim(
        f"{ACTIVE}/lifecycle.json",
        "checkpoint_interruption.trunk_step",
        "Real Dev Home suspension and recovery",
        unique=False,
    ),
    Claim(f"{PREVIOUS}/throughput.json", "tokens_per_second", PREVIOUS_ROW, "{:.3f}"),
    Claim(f"{PREVIOUS}/throughput.json", "peak_memory_bytes", PREVIOUS_ROW),
]


def lookup(data: object, key: str) -> object:
    count = key.startswith("len:")
    for part in key.removeprefix("len:").split("."):
        if not isinstance(data, dict) or part not in data:
            raise KeyError(key)
        data = data[part]
    return len(data) if count else data  # type: ignore[arg-type]


def render(root: Path, claim: Claim) -> str:
    data = json.loads((root / "docs/evidence" / claim.evidence).read_text(encoding="utf-8"))
    return claim.fmt.format(lookup(data, claim.key))


def token(value: str) -> re.Pattern[str]:
    """Match value as a whole number/identifier, not as part of a longer one."""
    return re.compile(rf"(?<![\w.]){re.escape(value)}(?![\w]|\.\d)")


def rows(text: str) -> dict[str, str]:
    """Map the first cell of each Markdown table row to the rest of the row."""
    table: dict[str, str] = {}
    for line in text.splitlines():
        if line.lstrip().startswith("|"):
            cells = [c.strip() for c in CELL_SPLIT.split(line.strip().strip("|"))]
            if cells and cells[0]:
                table.setdefault(cells[0], " | ".join(cells[1:]))
    return table


def check(root: Path, claims: list[Claim]) -> list[str]:
    errors: list[str] = []
    status_rows = rows((root / STATUS).read_text(encoding="utf-8"))
    pages = {
        p.relative_to(root).as_posix(): p.read_text(encoding="utf-8")
        for p in root.rglob("*.md")
        if not any(part in SKIP_DIRS for part in p.relative_to(root).parts)
    }
    for claim in claims:
        name = f"{claim.evidence}:{claim.key}"
        try:
            value = render(root, claim)
        except (OSError, KeyError, ValueError) as error:
            errors.append(f"{name}: cannot read evidence ({error})")
            continue
        pattern = token(value)
        if claim.row is not None:
            if claim.row not in status_rows:
                errors.append(f"{STATUS}: missing row '{claim.row}' for {name}")
            elif not pattern.search(status_rows[claim.row]):
                errors.append(f"{STATUS}: row '{claim.row}' does not state {name} = {value}")
        if not claim.unique:
            continue
        for rel, text in pages.items():
            if rel == STATUS or rel.startswith(ALLOWED):
                continue
            if pattern.search(text):
                errors.append(f"{rel}: copies {name} = {value}; link to {STATUS} instead")
    return errors


def main() -> int:
    errors = check(ROOT, CLAIMS)
    if errors:
        print("doc claims FAILED:")
        for item in errors:
            print(f"  {item}")
        return 1
    print(f"doc claims: ok ({len(CLAIMS)} values match evidence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
