#!/usr/bin/env python3
"""Relative-link check for Markdown files: target paths and #anchors must exist."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
FENCE = re.compile(r"^\s*(```|~~~)")
SKIP_DIRS = {".git", "build", "out", "__pycache__"}


def is_relative(url: str) -> bool:
    return not url.startswith(("http://", "https://", "mailto:"))


def slug(heading: str) -> str:
    """GitHub-style heading anchor."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", heading)  # links -> text
    text = text.strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


def anchors(path: Path, cache: dict[Path, set[str]]) -> set[str]:
    if path not in cache:
        found: set[str] = set()
        counts: dict[str, int] = {}
        in_fence = False
        for line in path.read_text(encoding="utf-8").splitlines():
            if FENCE.match(line):
                in_fence = not in_fence
                continue
            match = None if in_fence else HEADING.match(line)
            if not match:
                continue
            base = slug(match.group(2))
            n = counts.get(base, 0)
            counts[base] = n + 1
            found.add(base if n == 0 else f"{base}-{n}")
        cache[path] = found
    return cache[path]


def main() -> int:
    broken: list[str] = []
    cache: dict[Path, set[str]] = {}
    for path in sorted(ROOT.rglob("*.md")):
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        text = path.read_text(encoding="utf-8")
        for match in LINK.finditer(text):
            raw = match.group(2).strip()
            url = raw.split()[0].strip("<>")
            if not url or not is_relative(url):
                continue
            target_part, _, fragment = url.partition("#")
            target = (path.parent / target_part).resolve() if target_part else path
            where = path.relative_to(ROOT)
            try:
                target.relative_to(ROOT.resolve())
            except ValueError:
                broken.append(f"{where}: escapes repo -> {url}")
                continue
            if not target.exists():
                broken.append(f"{where}: missing {target_part}")
                continue
            if (
                fragment
                and target.suffix == ".md"
                and fragment not in anchors(target, cache)
            ):
                broken.append(
                    f"{where}: missing anchor #{fragment} in {target.relative_to(ROOT)}"
                )

    if broken:
        print("relative markdown links FAILED:")
        for item in broken:
            print(f"  {item}")
        return 1

    print("relative markdown links: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
