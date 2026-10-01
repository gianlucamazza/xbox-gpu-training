#!/usr/bin/env python3
"""Check this backend against floppylm's contracts at the pinned commit.

1. contracts/floppylm/ is byte-identical to PIN.json (no local edits).
2. The pinned schemas accept every valid golden fixture and reject every invalid one.
3. Every floppylm.e0.result.v1 / floppylm.device.v1 object in docs/evidence matches.

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
PINNED = ROOT / "contracts" / "floppylm"
NATIVE = ("floppylm.e0.result.v1", "floppylm.device.v1")


def load(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def integrity() -> list[str]:
    pin = load(PINNED / "PIN.json")
    present = {
        str(p.relative_to(PINNED))
        for p in PINNED.rglob("*")
        if p.is_file() and p.name != "PIN.json"
    }
    errors = [f"unpinned file {n}" for n in sorted(present - pin["files"].keys())]
    errors += [f"missing file {n}" for n in sorted(pin["files"].keys() - present)]
    for name, digest in sorted(pin["files"].items()):
        path = PINNED / name
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            errors.append(f"modified file {name}")
    return errors


def native_objects(value):
    if isinstance(value, dict):
        if value.get("schema") in NATIVE:
            yield value
        for child in value.values():
            yield from native_objects(child)
    elif isinstance(value, list):
        for child in value:
            yield from native_objects(child)


def main() -> int:
    errors = integrity()
    schemas = {p.stem: load(p) for p in sorted((PINNED / "schemas").glob("*.json"))}
    registry = Registry().with_resources(
        (s["$id"], Resource.from_contents(s)) for s in schemas.values()
    )
    validators = {k: Draft202012Validator(s, registry=registry) for k, s in schemas.items()}
    checked = {"valid": 0, "invalid": 0, "evidence": 0}
    for kind in ("valid", "invalid"):
        for path in sorted((PINNED / "fixtures" / kind).glob("*.json")):
            ok = validators[path.stem.split("--")[0]].is_valid(load(path))
            if ok != (kind == "valid"):
                errors.append(f"{kind} fixture {path.name} was {'accepted' if ok else 'rejected'}")
            checked[kind] += 1
    for path in sorted((ROOT / "docs" / "evidence").rglob("*.json")):
        for report in native_objects(load(path)):
            for error in validators[report["schema"]].iter_errors(report):
                errors.append(f"{path.relative_to(ROOT)}: {error.message[:200]}")
            checked["evidence"] += 1
    pin = load(PINNED / "PIN.json")["commit"][:12]
    if not checked["valid"] or not checked["invalid"] or not checked["evidence"]:
        errors.append(f"nothing to check: {checked}")
    if errors:
        print(f"floppylm contracts @ {pin}: FAILED", *errors, sep="\n  ")
        return 1
    print(f"floppylm contracts @ {pin}: ok {checked}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
