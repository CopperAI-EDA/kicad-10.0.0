#!/usr/bin/env python3
"""Verify that Windows resource scripts reference files that actually exist.

resources/msw/*.rc pull in icons and bitmaps by relative path. The resource
compiler only runs during a Windows build, so a dangling reference sits unnoticed
until someone tries to build for Windows -- which for this tree meant a reference
to icon_copperai.ico that was not in the repository at all.

Exit status is non-zero if any referenced file is missing.

Usage:
    check_resource_refs.py [rc-files-or-dirs...]     # defaults to resources/msw
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

# e.g.   IDI_APP_KICAD_ICON ICON "../resources/bitmaps_png/icons/icon_copperai.ico"
RESOURCE_REF = re.compile(
    r'^\s*\w+\s+(ICON|BITMAP|CURSOR|RCDATA)\s+"([^"]+)"', re.IGNORECASE
)


def read_text(path: Path) -> str:
    raw = path.read_bytes()
    enc = "utf-16" if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8"
    return raw.decode(enc, errors="ignore")


def check_rc(rc: Path, repo: Path) -> list[str]:
    """Report references that resolve against none of the RC include roots.

    Paths in these .rc files are not relative to the .rc itself -- the resource
    compiler is invoked with include paths set by CMake, so e.g.
    "../resources/bitmaps_png/icons/x.ico" resolves from <repo>/resources.
    Try every plausible root and only complain if none of them has the file.
    """
    roots = [repo / "resources", repo, rc.parent]
    problems: list[str] = []
    for n, line in enumerate(read_text(rc).splitlines(), 1):
        m = RESOURCE_REF.match(line)
        if not m:
            continue
        kind, ref = m.group(1), m.group(2)
        # wx/... comes from wxWidgets' own include path at build time, not from
        # this repository, so it is never resolvable here.
        if ref.replace("\\", "/").startswith("wx/"):
            continue
        if any((root / ref).resolve().exists() for root in roots):
            continue
        problems.append(
            f"line {n}: {kind} references '{ref}' -- not found relative to "
            f"resources/, repo root, or {rc.parent.name}/"
        )
    return problems


def main(argv: list[str]) -> int:
    repo = Path(__file__).resolve().parents[2]
    args = [Path(a) for a in argv[1:]] or [repo / "resources" / "msw"]

    rc_files: list[Path] = []
    for a in args:
        if a.is_dir():
            rc_files.extend(sorted(a.rglob("*.rc")))
        elif a.is_file():
            rc_files.append(a)

    if not rc_files:
        print("no .rc files found", file=sys.stderr)
        return 1

    failed = 0
    for rc in rc_files:
        problems = check_rc(rc, repo)
        if problems:
            failed += 1
            rel = rc.relative_to(repo) if repo in rc.parents else rc
            for p in problems:
                print(f"{rel}: {p}")

    print(f"\nchecked {len(rc_files)} .rc files, {failed} with dangling references")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
