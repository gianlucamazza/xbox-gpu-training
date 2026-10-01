#!/usr/bin/env python3
"""Check that headline numbers in the docs match the committed evidence JSON.

Link checks prove that cited files exist; this proves that cited values are true.
Each claim renders a value from an evidence file. The value must appear in
docs/status.md, and "unique" values must not be copied into other pages (a copy
is how a number silently goes stale).
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATUS = "docs/status.md"
# Pages allowed to repeat unique values besides STATUS (evidence notes are records).
ALLOWED = ("docs/evidence/", "docs/history.md")
SKIP_DIRS = {".git", "build", "out", "__pycache__"}


@dataclass(frozen=True)
class Claim:
    evidence: str  # path under docs/evidence/
    key: str  # dotted key; "len:<key>" counts a list
    fmt: str = "{}"
    unique: bool = True  # forbid copies outside STATUS / ALLOWED


ACTIVE = "e0-20261001-resident"
PREVIOUS = "e0-20261001"
CLAIMS = [
    Claim(f"{ACTIVE}/package-lineage.json", "package"),
    Claim(f"{ACTIVE}/package-lineage.json", "source_commit"),
    Claim(f"{ACTIVE}/package-lineage.json", "ci_run", unique=False),
    Claim(f"{ACTIVE}/throughput.json", "tokens_per_second", "{:.3f}"),
    Claim(f"{ACTIVE}/throughput.json", "tokens_per_second", "{:.0f}"),  # rounded copies
    Claim(f"{ACTIVE}/throughput.json", "peak_memory_bytes"),
    Claim(f"{ACTIVE}/kernel-parity.json", "case_count", unique=False),
    Claim(f"{ACTIVE}/acceptance.json", "len:fixtures", unique=False),
    Claim(f"{ACTIVE}/bit-identity.json", "acceptance_cases.compared", unique=False),
    Claim(
        f"{ACTIVE}/lifecycle.json", "checkpoint_interruption.trunk_step", unique=False
    ),
    Claim(f"{PREVIOUS}/throughput.json", "tokens_per_second", "{:.3f}"),
    Claim(f"{PREVIOUS}/throughput.json", "peak_memory_bytes"),
]


def lookup(data: object, key: str) -> object:
    count = key.startswith("len:")
    for part in key.removeprefix("len:").split("."):
        if not isinstance(data, dict) or part not in data:
            raise KeyError(key)
        data = data[part]
    return len(data) if count else data  # type: ignore[arg-type]


def render(root: Path, claim: Claim) -> str:
    data = json.loads(
        (root / "docs/evidence" / claim.evidence).read_text(encoding="utf-8")
    )
    return claim.fmt.format(lookup(data, claim.key))


def check(root: Path, claims: list[Claim]) -> list[str]:
    errors: list[str] = []
    status = (root / STATUS).read_text(encoding="utf-8")
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
        if value not in status:
            errors.append(f"{STATUS} does not state {name} = {value}")
        if not claim.unique:
            continue
        for rel, text in pages.items():
            if rel == STATUS or rel.startswith(ALLOWED):
                continue
            if value in text:
                errors.append(
                    f"{rel}: copies {name} = {value}; link to {STATUS} instead"
                )
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
