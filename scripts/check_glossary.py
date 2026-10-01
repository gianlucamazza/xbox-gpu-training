#!/usr/bin/env python3
"""Reject claim-shaped forbidden phrases in tracked prose.

Allowed: documenting that Xbox has no CUDA, and that DirectML on console
is inference/forward-focused. Forbidden: phrasing those stacks as trainers
we use, or inventing ID@Xbox / GDKX access.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SKIP_DIRS = {
    ".git",
    "build",
    "out",
    "CMakeFiles",
    "__pycache__",
    ".venv",
    "vcpkg_installed",
}

TEXT_SUFFIXES = {".md", ".yml", ".yaml", ".hlsl", ".cpp", ".h", ".hpp", ".txt", ".py", ".ps1", ".xml", ".idl"}

# Exact claim-shaped phrases. Do not write these strings in the repo.
FORBIDDEN = [
    (re.compile(r"DirectML training", re.IGNORECASE), "DirectML training (implies ORT/DML trains on Xbox)"),
    (re.compile(r"CUDA training", re.IGNORECASE), "CUDA training (Xbox has no CUDA)"),
    (re.compile(r"we (have|got|hold|possess) (ID@Xbox|GDKX)", re.IGNORECASE), "claimed ID@Xbox/GDKX access"),
    (re.compile(r"using CUDA", re.IGNORECASE), "using CUDA (Xbox has no CUDA)"),
]


def iter_files() -> list[Path]:
    files: list[Path] = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        if path.suffix.lower() not in TEXT_SUFFIXES and path.name not in {"LICENSE", "CODEOWNERS"}:
            continue
        if path.name == "check_glossary.py":
            continue
        files.append(path)
    return files


def main() -> int:
    hits: list[str] = []
    for path in iter_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT).as_posix()
        for pattern, label in FORBIDDEN:
            for match in pattern.finditer(text):
                line_no = text.count("\n", 0, match.start()) + 1
                hits.append(f"{rel}:{line_no}: {label} -> {match.group(0)!r}")

    if hits:
        print("glossary claim check FAILED:")
        for hit in hits:
            print(f"  {hit}")
        return 1

    print("glossary claim check: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
