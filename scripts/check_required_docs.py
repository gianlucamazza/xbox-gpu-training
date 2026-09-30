#!/usr/bin/env python3
"""Fail if required research-repo files are missing or empty."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REQUIRED = [
    "README.md",
    "ROADMAP.md",
    "LICENSE",
    "CONTRIBUTING.md",
    "docs/execution-plan.md",
    "docs/adr/0001-architecture.md",
    "docs/architecture.md",
    "docs/platform/dev-mode.md",
    "docs/platform/uwp-resources.md",
    "docs/platform/dx12-hlsl-compute.md",
    "docs/platform/directml-scope.md",
    "docs/platform/gdk-vs-gdkx.md",
    "docs/platform/series-s-vs-x.md",
    "docs/platform/blockers-fase6-validation.md",
    ".github/workflows/ci.yml",
    ".github/workflows/benchmark.yml",
]


def main() -> int:
    missing: list[str] = []
    empty: list[str] = []
    for rel in REQUIRED:
        path = ROOT / rel
        if not path.is_file():
            missing.append(rel)
            continue
        if path.stat().st_size == 0:
            empty.append(rel)

    if missing or empty:
        if missing:
            print("MISSING required files:")
            for item in missing:
                print(f"  - {item}")
        if empty:
            print("EMPTY required files:")
            for item in empty:
                print(f"  - {item}")
        return 1

    print("required files: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
