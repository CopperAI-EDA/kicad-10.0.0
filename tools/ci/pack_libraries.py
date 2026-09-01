"""Collapse the KiCad libraries into one archive before MSI packaging.

Why this exists
---------------
The symbol and footprint libraries are ~38,500 small files -- 87% of everything
in the package, but a minority of its bytes. Windows Installer keeps a File
table row per file and walks it during costing, InstallValidate, InstallFiles
and uninstall, so shipping them individually makes every install, upgrade and
*removal* crawl. With them inlined the installer sat on "Computing space
requirements" and then "Validating install" for many minutes.

KiCad's own Windows installer gets away with it because NSIS has no per-file
bookkeeping. MSI charges for every row, so the fix is to hand MSI one file
instead of 38,500 and unpack it at install time.

This script replaces the library directories in a staged install tree with a
single .tar.gz. `tools/ci/gen_copperai_wix.py` then emits ~5,500 files instead
of 44,000, and a deferred custom action in the package extracts the archive.

tar.gz rather than .zip: Windows ships bsdtar at System32\\tar.exe (1803+), so
extraction needs no bundled tool and no PowerShell execution-policy dance.

Usage:  python tools/ci/pack_libraries.py <stage_dir>
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# Directories collapsed into the archive, relative to share/kicad.
LIBRARY_DIRS = ("symbols", "footprints", "template")
ARCHIVE_NAME = "copperai-libraries.tar.gz"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", type=Path, help="install tree root (contains share/kicad)")
    args = ap.parse_args()

    share = args.stage / "share" / "kicad"
    if not share.is_dir():
        print(f"error: {share} does not exist")
        return 1

    present = [d for d in LIBRARY_DIRS if (share / d).is_dir()]
    if not present:
        print("no library directories to pack (already packed, or never fetched)")
        return 0

    before = sum(1 for d in present for _ in (share / d).rglob("*") if _.is_file())
    archive = share / ARCHIVE_NAME
    archive.unlink(missing_ok=True)

    # System32 tar explicitly: a tar.exe from Git for Windows would treat the
    # "C:" of an absolute path as a remote host, the same trap that broke the
    # image-archive build step.
    tar = Path(r"C:\Windows\System32\tar.exe")
    if not tar.is_file():
        print(f"error: {tar} not found (needs Windows 10 1803+)")
        return 1

    print(f"packing {before:,} library files from {', '.join(present)}")
    # -C share so paths inside the archive are "symbols/...", which is what the
    # extract-time custom action expects.
    cmd = [str(tar), "-czf", str(archive), "-C", str(share), *present]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"error: tar failed ({result.returncode})\n{result.stderr[:800]}")
        return 1

    if not archive.is_file() or archive.stat().st_size == 0:
        print("error: archive was not produced")
        return 1

    # Verify the archive lists what we expect before deleting the originals --
    # otherwise a silent tar failure would ship a package with no libraries at
    # all, which is exactly the failure mode this whole exercise started from.
    listing = subprocess.run([str(tar), "-tzf", str(archive)], capture_output=True, text=True)
    if listing.returncode != 0:
        print(f"error: archive is unreadable ({listing.returncode})")
        archive.unlink(missing_ok=True)
        return 1

    entries = [ln for ln in listing.stdout.splitlines() if ln.strip()]
    files_in_archive = [ln for ln in entries if not ln.endswith("/")]
    if len(files_in_archive) < before:
        print(f"error: archive holds {len(files_in_archive):,} files, expected {before:,}")
        archive.unlink(missing_ok=True)
        return 1

    for d in present:
        shutil.rmtree(share / d)

    size_mb = archive.stat().st_size / 1024 / 1024
    print(f"  {archive.name}: {size_mb:,.1f} MB, {len(files_in_archive):,} files verified")
    print(f"  removed {', '.join(present)} from the staged tree")
    print(f"  MSI File rows saved: {before:,}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
