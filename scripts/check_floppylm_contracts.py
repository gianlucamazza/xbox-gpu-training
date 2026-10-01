#!/usr/bin/env python3
"""Check this backend against floppylm's contracts at the pinned commit.

1. contracts/floppylm/ is byte-identical to PIN.json (no local edits).
2. The pinned schemas accept every valid golden fixture and reject every invalid one.
3. Every object in docs/evidence that names a pinned contract in its "schema" field matches it.

Requires jsonschema>=4.18. Refresh the copy with sync_floppylm_contracts.py.
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parents[1]


def load(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def integrity(pinned: Path) -> list[str]:
    pin = load(pinned / "PIN.json")
    present = {
        str(p.relative_to(pinned))
        for p in pinned.rglob("*")
        if p.is_file() and p.name != "PIN.json"
    }
    errors = [f"unpinned file {n}" for n in sorted(present - pin["files"].keys())]
    errors += [f"missing file {n}" for n in sorted(pin["files"].keys() - present)]
    for name, digest in sorted(pin["files"].items()):
        path = pinned / name
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            errors.append(f"modified file {name}")
    return errors


def named_objects(value, names):
    if isinstance(value, dict):
        if value.get("schema") in names:
            yield value
        for child in value.values():
            yield from named_objects(child, names)
    elif isinstance(value, list):
        for child in value:
            yield from named_objects(child, names)


def check(root: Path) -> tuple[list[str], dict[str, int]]:
    """Return (errors, counts) for the pinned copy under root/contracts/floppylm."""
    pinned = root / "contracts" / "floppylm"
    errors = integrity(pinned)
    schemas = {p.stem: load(p) for p in sorted((pinned / "schemas").glob("*.json"))}
    registry = Registry().with_resources(
        (s["$id"], Resource.from_contents(s)) for s in schemas.values()
    )
    validators = {k: Draft202012Validator(s, registry=registry) for k, s in schemas.items()}
    counts = {"valid": 0, "invalid": 0, "evidence": 0}
    for kind in ("valid", "invalid"):
        for path in sorted((pinned / "fixtures" / kind).glob("*.json")):
            ok = validators[path.stem.split("--")[0]].is_valid(load(path))
            if ok != (kind == "valid"):
                errors.append(f"{kind} fixture {path.name} was {'accepted' if ok else 'rejected'}")
            counts[kind] += 1
    for path in sorted((root / "docs" / "evidence").rglob("*.json")):
        for report in named_objects(load(path), validators.keys()):
            for error in validators[report["schema"]].iter_errors(report):
                errors.append(f"{path.relative_to(root)}: {error.message[:200]}")
            counts["evidence"] += 1
    if not all(counts.values()):
        errors.append(f"nothing to check: {counts}")
    return errors, counts


def main() -> int:
    errors, counts = check(ROOT)
    pin = load(ROOT / "contracts" / "floppylm" / "PIN.json")["commit"][:12]
    if errors:
        print(f"floppylm contracts @ {pin}: FAILED", *errors, sep="\n  ")
        return 1
    print(f"floppylm contracts @ {pin}: ok {counts}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
