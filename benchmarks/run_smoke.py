#!/usr/bin/env python3
"""Smoke benchmark stub.

Writes benchmarks/results/smoke.json with status=stub.
Does not invent tok/s or quality metrics.
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="xbox-gpu-training smoke benchmark (stub)")
    parser.add_argument(
        "--out",
        default=str(Path(__file__).resolve().parent / "results" / "smoke.json"),
        help="Output JSON path",
    )
    args = parser.parse_args()

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    payload = {
        "status": "stub",
        "note": (
            "Not implemented. No GPU kernel timed. No tok/s. "
            "Fase 1 will record matmul vs CPU ggml when kernels exist. "
            "Xbox Series S|X numbers require Dev Mode hardware (Fase 6)."
        ),
        "generated_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "schema": "xbox-gpu-training.benchmark.smoke.v1",
    }

    out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out_path}")
    print(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
