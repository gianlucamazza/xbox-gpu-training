#!/usr/bin/env python3
"""Lightweight relative-link check for Markdown files."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
SKIP_DIRS = {".git", "build", "out", "__pycache__"}


def is_relative(url: str) -> bool:
    if url.startswith(("#", "http://", "https://", "mailto:")):
        return False
    return True


def main() -> int:
    broken: list[str] = []
    for path in ROOT.rglob("*.md"):
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        text = path.read_text(encoding="utf-8")
        for match in LINK.finditer(text):
            raw = match.group(2).strip()
            url = raw.split()[0].strip("<>")
            url = url.split("#", 1)[0]
            if not url or not is_relative(url):
                continue
            target = (path.parent / url).resolve()
            try:
                target.relative_to(ROOT.resolve())
            except ValueError:
                broken.append(f"{path.relative_to(ROOT)}: escapes repo -> {url}")
                continue
            if not target.exists():
                broken.append(f"{path.relative_to(ROOT)}: missing {url}")

    if broken:
        print("relative markdown links FAILED:")
        for item in broken:
            print(f"  {item}")
        return 1

    print("relative markdown links: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
