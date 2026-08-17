"""Fetch the KiCad symbol/footprint/template libraries into an install tree.

KiCad's libraries live in separate repositories from the application source, so
a build of this tree produces an editor with no parts in it: eeschema opens, but
the symbol chooser is empty and the setup wizard reports that the library tables
could not be found. The official KiCad installers bundle these; ours has to do
the same.

Fetched into <stage>/share/kicad/, which is what PATHS::GetStockEDALibraryPath()
resolves to on Windows (GetStockDataPath):

    symbols/      kicad-symbols       schematic symbols
    footprints/   kicad-footprints    .pretty footprint libraries
    template/     kicad-templates     project templates + global lib tables

kicad-packages3D is deliberately NOT fetched: it is several GB of STEP/WRL models
for a feature (3D viewer rendering) that works without them, and users who want
it can install it through the Plugin and Content Manager.

Usage:
    python tools/ci/fetch_kicad_libraries.py <stage_dir> [--version 10.0.5]
"""
from __future__ import annotations

import argparse
import io
import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path

GITLAB = "https://gitlab.com/api/v4/projects"

# repo -> directory name under share/kicad/
REPOS = {
    "kicad-symbols": "symbols",
    "kicad-footprints": "footprints",
    "kicad-templates": "template",
}


def fetch_tarball(repo: str, version: str) -> bytes:
    project = f"kicad%2Flibraries%2F{repo}"
    url = f"{GITLAB}/{project}/repository/archive.tar.gz?sha={version}"
    print(f"    {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "copperai-setup"})
    with urllib.request.urlopen(req, timeout=600) as resp:
        return resp.read()


def extract_into(blob: bytes, dest: Path) -> int:
    """Extract the archive's single top-level dir *contents* into dest."""
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True, exist_ok=True)

    count = 0
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as tf:
        for member in tf.getmembers():
            if not member.isfile():
                continue
            # GitLab archives are wrapped in "<repo>-<sha>/" -- strip that level.
            parts = Path(member.name).parts
            if len(parts) < 2:
                continue
            rel = Path(*parts[1:])
            # Refuse anything that would escape dest.
            target = (dest / rel).resolve()
            if not str(target).startswith(str(dest.resolve())):
                raise RuntimeError(f"unsafe path in archive: {member.name}")
            target.parent.mkdir(parents=True, exist_ok=True)
            src = tf.extractfile(member)
            if src is None:
                continue
            with target.open("wb") as out:
                shutil.copyfileobj(src, out)
            count += 1
    return count


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", type=Path, help="install tree root (contains share/kicad)")
    ap.add_argument("--version", default="10.0.5",
                    help="library tag to fetch (should track the app version)")
    args = ap.parse_args()

    share = args.stage / "share" / "kicad"
    if not share.is_dir():
        print(f"error: {share} does not exist -- run cmake --install first")
        return 1

    total = 0
    for repo, subdir in REPOS.items():
        dest = share / subdir
        print(f"  {repo} -> share/kicad/{subdir}")
        try:
            blob = fetch_tarball(repo, args.version)
        except Exception as exc:                       # noqa: BLE001
            print(f"    FAILED: {exc}")
            return 1
        n = extract_into(blob, dest)
        size = sum(p.stat().st_size for p in dest.rglob("*") if p.is_file())
        print(f"    {n:,} files, {size / 1024 / 1024:.1f} MB")
        total += n

    # The global tables live at the root of the symbol/footprint repos. KiCad
    # reads them from template/, so put them where the first-run wizard looks.
    template = share / "template"
    for name, src_dir in (("sym-lib-table", share / "symbols"),
                          ("fp-lib-table", share / "footprints")):
        src = src_dir / name
        if src.is_file():
            shutil.copy2(src, template / name)
            print(f"  {name} -> template/{name}")
        elif (template / name).is_file():
            print(f"  {name} already present in template/")
        else:
            print(f"  WARNING: {name} not found -- the first-run wizard will "
                  f"report missing library tables")

    print(f"\n{total:,} library files staged into {share}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
