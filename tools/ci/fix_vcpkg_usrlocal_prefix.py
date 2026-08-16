#!/usr/bin/env python3
"""Relocate vcpkg ports that installed under a bogus usr/local/ prefix.

Some autotools-based ports let configure fall back to --prefix=/usr/local, so
their files land in <triplet>/usr/local/... instead of <triplet>/...  vcpkg
records that layout in the port's .list manifest and marks the port installed,
so it will not rebuild -- meanwhile consumers looking in the canonical location
fail with errors that point nowhere near the real cause.

Observed on this tree with:
  * gperf          -> fontconfig fails, having logged "Program gperf found: YES"
                      immediately after "[Errno 2] No such file or directory"
  * gettext-libintl -> gettext fails with "Cannot open include file: 'libintl.h'"

This recurs whenever the package ABI hash changes (e.g. editing a triplet), so
it belongs in the pipeline rather than in somebody's shell history.

Rule: strip the leading "usr/local/" from each path. Handles both
<triplet>/usr/local/... and <triplet>/debug/usr/local/...

Usage:
    fix_vcpkg_usrlocal_prefix.py <vcpkg_installed_dir>
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path


def canonical(entry: str) -> str:
    """x64-windows/usr/local/include/x.h     -> x64-windows/include/x.h
       x64-windows/debug/usr/local/lib/x.lib -> x64-windows/debug/lib/x.lib"""
    return entry.replace("/debug/usr/local/", "/debug/").replace("/usr/local/", "/")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2

    installed = Path(argv[1])
    info = installed / "vcpkg" / "info"
    if not info.is_dir():
        print(f"no vcpkg info dir at {info}", file=sys.stderr)
        return 1

    total = 0
    for listfile in sorted(info.glob("*.list")):
        entries = [l.strip() for l in
                   listfile.read_text(encoding="utf-8").splitlines() if l.strip()]
        if not any("usr/local" in e for e in entries):
            continue

        print(f"--- {listfile.stem}")
        moved = 0
        for entry in entries:
            if "usr/local" not in entry or entry.endswith("/"):
                continue
            src = installed / entry
            if not src.is_file():
                continue
            dst = installed / canonical(entry)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(src), str(dst))
            print(f"    {entry}  ->  {canonical(entry)}")
            moved += 1
        total += moved

        # Rewrite the manifest so vcpkg's bookkeeping (notably uninstall) stays
        # consistent: remap file paths, drop dead usr/ directory entries, and
        # re-emit parent directories so the manifest stays well-formed.
        out: set[str] = set()
        for entry in entries:
            if "usr/local" in entry and entry.endswith("/"):
                continue
            new = canonical(entry)
            parts = Path(new).parts
            for i in range(1, len(parts)):
                out.add("/".join(parts[:i]) + "/")
            out.add(new)
        listfile.write_text("\n".join(sorted(out)) + "\n", encoding="utf-8")
        print(f"    manifest rewritten ({moved} files moved)")

    # prune the now-empty usr/ trees
    for pattern in ("*/usr", "*/debug/usr"):
        for leftover in installed.glob(pattern):
            shutil.rmtree(leftover, ignore_errors=True)

    print(f"\ntotal files relocated: {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
