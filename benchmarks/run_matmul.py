#!/usr/bin/env python3
"""Matmul bench runner.

Invokes xbox_gpu_host --bench matmul so the C++ host writes the CSV.
If the host binary is missing, writes a blocked CSV itself.

Never invents tok/s or GPU dispatch logs.
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

SCHEMA = "xbox-gpu-training.benchmark.matmul.v1"
REPO = Path(__file__).resolve().parents[1]


def find_host() -> Path | None:
    candidates = [
        REPO / "build" / "xbox_gpu_host",
        REPO / "build" / "Release" / "xbox_gpu_host.exe",
        REPO / "build" / "xbox_gpu_host.exe",
        REPO / "build" / "src" / "cpp" / "Release" / "xbox_gpu_host.exe",
        REPO / "dist" / "bin" / "xbox_gpu_host.exe",
        REPO / "dist" / "bin" / "xbox_gpu_host",
    ]
    for path in candidates:
        if path.is_file():
            return path
    return None


def write_blocked_csv(out_path: Path, reason: str) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "schema",
        "status",
        "precision",
        "M",
        "N",
        "K",
        "max_abs_error",
        "max_rel_error",
        "tol_abs",
        "tol_rel",
        "parity",
        "cpu_ms",
        "gpu_dispatch_ms",
        "device",
        "adapter",
        "note",
    ]
    note = (
        f"{reason} GPU matmul was not dispatched. Dispatch log not invented. "
        f"Not a tok/s result. Not a console result. generated_at_utc="
        f"{datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')}"
    )
    with out_path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerow(
            {
                "schema": SCHEMA,
                "status": "blocked",
                "precision": "fp32",
                "M": "",
                "N": "",
                "K": "",
                "max_abs_error": "",
                "max_rel_error": "",
                "tol_abs": "1.000000e-04",
                "tol_rel": "1.000000e-03",
                "parity": "n/a",
                "cpu_ms": "",
                "gpu_dispatch_ms": "",
                "device": "none",
                "adapter": "",
                "note": note,
            }
        )
    print(f"wrote {out_path} status=blocked")
    print(note)


def main() -> int:
    parser = argparse.ArgumentParser(description="xbox-gpu-training matmul bench (no invented tok/s)")
    parser.add_argument(
        "--out",
        default=str(REPO / "benchmarks" / "results" / "matmul.csv"),
        help="Output CSV path",
    )
    parser.add_argument("--host", default="", help="Path to xbox_gpu_host (optional)")
    args = parser.parse_args()

    out_path = Path(args.out)
    host = Path(args.host) if args.host else find_host()
    if host is None or not host.is_file():
        write_blocked_csv(
            out_path,
            "BLOCKED: xbox_gpu_host not found. Build the C++ host, then re-run. ",
        )
        return 0

    out_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(host), "--bench", "matmul", "--out", str(out_path)]
    print("running:", " ".join(cmd))
    completed = subprocess.run(cmd, check=False)
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
