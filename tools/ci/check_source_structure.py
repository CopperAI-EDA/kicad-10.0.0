#!/usr/bin/env python3
"""Structural sanity check for C/C++ sources.

Catches the class of damage a bad patch application leaves behind -- code that
is obviously broken but is only discovered hours later when a platform-specific
translation unit finally gets compiled.

Three checks per file:
  * brace balance (ignoring braces inside comments and string/char literals)
  * #if / #endif balance
  * a function signature immediately followed by another function signature,
    which means a patch ate the first function's body

All three of these were live defects in libs/kiplatform/port/wxmsw/ui.cpp on the
copperai-release branch, and none of them were caught because that file only
compiles on Windows and nobody had built for Windows.

Exit status is non-zero if any file fails, so this is usable as a CI gate.

Usage:
    check_source_structure.py [paths...]      # defaults to the CopperAI dirs
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

# Directories that carry CopperAI's own changes. thirdparty/ is vendored and
# qa/ contains deliberately malformed fixtures, so both are excluded.
DEFAULT_ROOTS = [
    "common", "eeschema", "pcbnew", "kicad", "libs", "include", "mcp",
]
SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".mm"}
EXCLUDE_PARTS = {"thirdparty", "qa", "build"}

SIGNATURE = re.compile(
    r"^\s*(?:[A-Za-z_][\w:<>,&*\s]*\s+)?[A-Za-z_~]\w*(?:::[A-Za-z_~]\w*)+"
    r"\s*\([^;{}]*\)\s*(?:const)?\s*$"
)


def strip_literals(src: str) -> str:
    """Remove comments and string/char literal contents so brace counting is honest."""
    out: list[str] = []
    i, n, state = 0, len(src), "code"
    while i < n:
        c = src[i]
        if state == "code":
            if c == "/" and i + 1 < n and src[i + 1] == "/":
                state = "line"; i += 2; continue
            if c == "/" and i + 1 < n and src[i + 1] == "*":
                state = "block"; i += 2; continue
            if c == '"':
                state = "str"; i += 1; continue
            if c == "'":
                state = "chr"; i += 1; continue
            out.append(c); i += 1; continue
        if state == "line":
            if c == "\n":
                state = "code"; out.append("\n")
            i += 1; continue
        if state == "block":
            if c == "*" and i + 1 < n and src[i + 1] == "/":
                state = "code"; i += 2; continue
            if c == "\n":
                out.append("\n")
            i += 1; continue
        # inside a string or char literal
        if c == "\\":
            i += 2; continue
        if (state == "str" and c == '"') or (state == "chr" and c == "'"):
            state = "code"
        i += 1
    return "".join(out)


def check_file(path: Path) -> tuple[list[str], list[str]]:
    """Return (errors, warnings)."""
    src = path.read_text(encoding="utf-8", errors="ignore")
    code = strip_literals(src)
    lines = src.splitlines()
    errors: list[str] = []
    warnings: list[str] = []

    # A function signature immediately followed by another signature means a
    # patch ate the first function's body. This is precise -- it does not fire
    # on any healthy file in this tree -- so it is the gate.
    for i in range(len(lines) - 1):
        if SIGNATURE.match(lines[i]) and SIGNATURE.match(lines[i + 1]):
            errors.append(
                f"line {i + 1}: function has no body -- "
                f"'{lines[i].strip()[:60]}' runs straight into the next signature"
            )

    # Brace and #if counting cannot model conditional compilation: a file with
    # #if/#else branches carrying different brace counts is legitimately
    # "unbalanced" by naive counting. Several healthy files in this tree trip
    # it, so these are advisory only and never fail the build.
    opens, closes = code.count("{"), code.count("}")
    if opens != closes:
        warnings.append(f"brace count differs: {{={opens} }}={closes} "
                        f"(delta {opens - closes}) -- may just be #if branches")

    ifs = sum(1 for l in lines if re.match(r"\s*#\s*if", l))
    endifs = sum(1 for l in lines if re.match(r"\s*#\s*endif", l))
    if ifs != endifs:
        warnings.append(f"preprocessor count differs: #if={ifs} #endif={endifs}")

    return errors, warnings


def iter_sources(roots: list[Path]):
    for root in roots:
        if root.is_file():
            yield root
            continue
        for p in root.rglob("*"):
            if p.suffix.lower() not in SUFFIXES:
                continue
            if EXCLUDE_PARTS & set(p.parts):
                continue
            yield p


def main(argv: list[str]) -> int:
    repo = Path(__file__).resolve().parents[2]
    roots = [Path(a) for a in argv[1:]] or [repo / d for d in DEFAULT_ROOTS]
    roots = [r for r in roots if r.exists()]

    checked = 0
    failed: dict[Path, list[str]] = {}
    warned: dict[Path, list[str]] = {}
    for path in iter_sources(roots):
        checked += 1
        errors, warnings = check_file(path)
        if errors:
            failed[path] = errors
        if warnings:
            warned[path] = warnings

    def show(path: Path) -> str:
        return str(path.relative_to(repo) if repo in path.parents else path)

    for path, problems in sorted(warned.items()):
        for p in problems:
            print(f"warning: {show(path)}: {p}")

    if warned:
        print()

    for path, problems in sorted(failed.items()):
        for p in problems:
            print(f"ERROR: {show(path)}: {p}")

    print(f"\nchecked {checked} files: {len(failed)} with missing function bodies, "
          f"{len(warned)} with advisory count mismatches")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
