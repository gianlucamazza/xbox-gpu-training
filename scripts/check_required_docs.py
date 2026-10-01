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
    "docs/README.md",
    "docs/status.md",
    "docs/claims-policy.md",
    "docs/glossary.md",
    "docs/architecture.md",
    "docs/results.md",
    "docs/history.md",
    "docs/e0/overview.md",
    "docs/e0/engine.md",
    "docs/e0/job-protocol.md",
    "docs/e0/runbook.md",
    "docs/e0/acceptance.md",
    "docs/adr/README.md",
    "docs/adr/0001-architecture.md",
    "docs/adr/0002-ste-qat-mapping.md",
    "docs/adr/0003-floppylm-e0.md",
    "docs/adr/0004-independent-e0-gates.md",
    "docs/evidence/README.md",
    "docs/figures/README.md",
    "docs/diagnostic/README.md",
    "docs/diagnostic/setup.md",
    "docs/diagnostic/ggml-baseline.md",
    "docs/diagnostic/flp2-forward.md",
    "docs/diagnostic/ste-adamw.md",
    "docs/diagnostic/memory-budget.md",
    "docs/diagnostic/qat-wsd.md",
    "docs/platform/README.md",
    "docs/platform/dev-mode.md",
    "docs/platform/uwp-resources.md",
    "docs/platform/dx12-hlsl-compute.md",
    "docs/platform/directml-scope.md",
    "docs/platform/gdk-vs-gdkx.md",
    "docs/platform/series-s-vs-x.md",
    "docs/platform/console-constraints.md",
    ".github/workflows/ci.yml",
    ".github/workflows/benchmark.yml",
    ".github/workflows/e0-uwp.yml",
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
