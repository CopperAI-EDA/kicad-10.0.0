"""Pre-stage upstream source archives that have moved out from under vcpkg.

Upstream projects relocate release files. When they do, a vcpkg port's recorded
URL 404s and every clean build fails -- on every machine, until someone
diagnoses it. ngspice 45.2 is the live example: SourceForge moved it into an
`old-releases` path, so the port's URL returns 404 from every mirror.

vcpkg checks its downloads directory before fetching, so dropping a correctly
named file there makes the port skip the dead URL entirely.

Each entry's SHA512 is the one recorded in the port. It is verified before the
file is staged, so a wrong or tampered artifact is rejected rather than silently
compiled -- that verification is what makes this safe rather than a blind
substitution.

Usage:  python tools/ci/prefetch_distfiles.py [vcpkg_downloads_dir]

Exits non-zero only if a download that was attempted failed to verify. A file
that is already staged, or a URL that is reachable again, is not an error.
"""
import hashlib
import os
import sys
import urllib.request
from pathlib import Path

# filename -> (sha512 from the vcpkg portfile, [candidate urls, best first])
DISTFILES = {
    "ngspice-45.2.tar.gz": (
        "4090e9433457c0b49dc1e7561bc630a5c6a340391f26be8142c6bd514ae13b72"
        "137589964fe9ae5a01069c9de7d8457bd41b0811a335b2c93a3c6e07044b35b1",
        [
            # SourceForge moved 45.2 here when a newer release shipped; the
            # port's original .../ng-spice-rework/45.2/... path now 404s.
            "https://downloads.sourceforge.net/project/ngspice/ng-spice-rework/"
            "old-releases/45.2/ngspice-45.2.tar.gz",
            "https://downloads.sourceforge.net/project/ngspice/ng-spice-rework/"
            "45.2/ngspice-45.2.tar.gz",
        ],
    ),
}

CHUNK = 1 << 20


def sha512_of(path: Path) -> str:
    h = hashlib.sha512()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(CHUNK), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(url: str, dest: Path) -> bool:
    try:
        # A plain urlopen follows SourceForge's redirect to a mirror; a browser
        # UA avoids being handed the mirror-selection HTML page instead.
        req = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
        with urllib.request.urlopen(req, timeout=300) as resp, dest.open("wb") as out:
            while True:
                chunk = resp.read(CHUNK)
                if not chunk:
                    break
                out.write(chunk)
        return True
    except Exception as exc:                      # noqa: BLE001 - report and try the next url
        print(f"    failed: {exc}")
        dest.unlink(missing_ok=True)
        return False


def main() -> int:
    if len(sys.argv) > 1:
        downloads = Path(sys.argv[1])
    else:
        root = os.environ.get("VCPKG_ROOT")
        if not root:
            print("VCPKG_ROOT is not set and no downloads dir was given")
            return 1
        downloads = Path(root) / "downloads"

    downloads.mkdir(parents=True, exist_ok=True)
    print(f"staging into {downloads}")

    failed = []
    for name, (expected, urls) in DISTFILES.items():
        target = downloads / name
        if target.is_file() and sha512_of(target) == expected:
            print(f"  {name}: already staged and verified")
            continue

        print(f"  {name}: staging")
        staged = False
        for url in urls:
            tmp = downloads / (name + ".prefetch.tmp")
            print(f"    trying {url}")
            if not fetch(url, tmp):
                continue
            actual = sha512_of(tmp)
            if actual != expected:
                print("    SHA512 mismatch -- discarding")
                print(f"      expected {expected}")
                print(f"      actual   {actual}")
                tmp.unlink(missing_ok=True)
                continue
            tmp.replace(target)
            print(f"    staged, sha512 verified ({target.stat().st_size:,} bytes)")
            staged = True
            break

        if not staged:
            # Not fatal on its own: the port's own URL may simply work, in which
            # case vcpkg downloads it normally and this was unnecessary.
            print(f"  {name}: could not stage from any known location")
            failed.append(name)

    if failed:
        print(f"\nnot staged: {', '.join(failed)}")
        print("vcpkg will try the port's own URL; if that 404s the build fails there.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
