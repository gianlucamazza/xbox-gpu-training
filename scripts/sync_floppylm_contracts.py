#!/usr/bin/env python3
"""Vendor floppylm's contract schemas and golden fixtures at one pinned commit.

floppylm owns the floppylm.*.v1 contracts (floppylm ADR 0012). This copies
schemas/*.json and tests/fixtures/contracts/** from a floppylm checkout at
--commit into contracts/floppylm/ and records every file hash in PIN.json, so
check_floppylm_contracts.py can prove the copy is exact and unmodified.

    python3 scripts/sync_floppylm_contracts.py --source ../floppylm --commit <sha>
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "contracts" / "floppylm"
PREFIXES = ("schemas/", "tests/fixtures/contracts/")


def git(source: Path, *args: str) -> bytes:
    return subprocess.run(["git", "-C", str(source), *args], check=True, capture_output=True).stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, required=True, help="floppylm checkout")
    parser.add_argument("--commit", required=True, help="floppylm commit to pin")
    args = parser.parse_args()
    commit = git(args.source, "rev-parse", "--verify", args.commit + "^{commit}").decode().strip()
    names = [
        n
        for n in git(args.source, "ls-tree", "-r", "--name-only", commit).decode().splitlines()
        if n.startswith(PREFIXES) and n.endswith(".json")
    ]
    if not any(n.startswith("schemas/") for n in names):
        raise SystemExit(f"{commit} has no schemas/")
    shutil.rmtree(DEST, ignore_errors=True)
    files = {}
    for name in names:
        data = git(args.source, "show", f"{commit}:{name}")
        target = DEST / name.replace("tests/fixtures/contracts/", "fixtures/")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        files[str(target.relative_to(DEST))] = hashlib.sha256(data).hexdigest()
    pin = {"repository": "gianlucamazza/floppylm", "commit": commit, "files": files}
    (DEST / "PIN.json").write_text(json.dumps(pin, indent=1, sort_keys=True) + "\n")
    print(f"pinned floppylm {commit[:12]}: {len(files)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
