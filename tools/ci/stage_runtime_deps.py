"""Stage third-party runtime DLLs into an install tree, and prove the closure.

Why this exists
---------------
Nothing in KiCad's CMake installs vcpkg's runtime DLLs. `cmake --install` emits
our own binaries and the UCRT stubs; the ~150 DLLs we link against (wxWidgets,
OCCT, curl, ...) are left in vcpkg_installed. A package built without this step
installs cleanly, passes every MSI check, and then dies on launch with

    The code execution cannot proceed because
    wxmsw332u_html_vc_x64_custom.dll was not found.

That failure is invisible on the build machine, because vcpkg's bin is on PATH
there. It only shows up on a client PC -- which is exactly how it was found,
after shipping a package that had silently dropped all 16 wx DLLs.

Rather than blind-copying vcpkg's whole bin, this walks the PE import tables
from our own binaries outward and copies only what is genuinely reachable, then
fails if anything is left unresolved. Blind copying would also "work", but it
cannot tell you when it stops working: an import we never satisfy looks
identical to one we over-satisfied.

A DLL counts as a system DLL only if it is absent from the donor directory and
present in System32. Checking System32 alone would wrongly classify msvcp140 and
friends as system on this machine (the VC redist is installed here) and drop
them from the package, breaking client PCs that lack the redist.

Usage:  python tools/ci/stage_runtime_deps.py <stage_dir> <donor_bin_dir>
"""
from __future__ import annotations

import argparse
import shutil
import struct
import sys
from pathlib import Path

SYSTEM32 = Path(r"C:\Windows\System32")
# Always system, never shipped: the API-set stubs and the kernel surface.
SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-win-")


def pe_imports(path: Path) -> list[str]:
    """Return the DLL names in a PE file's import directory."""
    try:
        data = path.read_bytes()
    except OSError:
        return []
    if len(data) < 0x40 or data[:2] != b"MZ":
        return []

    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew + 24 > len(data) or data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return []

    coff = e_lfanew + 4
    n_sections, = struct.unpack_from("<H", data, coff + 2)
    opt_size, = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    magic, = struct.unpack_from("<H", data, opt)
    # PE32+ puts the data directories 16 bytes further in than PE32.
    dir_off = opt + (112 if magic == 0x20B else 96)
    n_dirs, = struct.unpack_from("<I", data, opt + (108 if magic == 0x20B else 92))
    if n_dirs < 2:
        return []
    import_rva, import_size = struct.unpack_from("<II", data, dir_off + 8)
    if not import_rva:
        return []

    sections = []
    sec_off = opt + opt_size
    for i in range(n_sections):
        s = sec_off + i * 40
        if s + 40 > len(data):
            break
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, s + 8)
        sections.append((vaddr, max(vsize, rawsize), rawptr))

    def rva_to_off(rva: int) -> int | None:
        for vaddr, size, rawptr in sections:
            if vaddr <= rva < vaddr + size:
                return rawptr + (rva - vaddr)
        return None

    names, i = [], 0
    while True:
        desc = rva_to_off(import_rva + i * 20)
        if desc is None or desc + 20 > len(data):
            break
        fields = struct.unpack_from("<IIIII", data, desc)
        if not any(fields):          # null terminator ends the array
            break
        name_off = rva_to_off(fields[3])
        if name_off is not None:
            end = data.find(b"\0", name_off)
            if end > name_off:
                names.append(data[name_off:end].decode("ascii", "ignore"))
        i += 1
    return names


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", type=Path)
    ap.add_argument("donor", type=Path, help="vcpkg_installed/<triplet>/bin")
    args = ap.parse_args()

    bindir = args.stage / "bin"
    if not bindir.is_dir():
        print(f"error: {bindir} does not exist")
        return 1
    if not args.donor.is_dir():
        print(f"error: donor {args.donor} does not exist")
        return 1

    donor = {p.name.lower(): p for p in args.donor.glob("*.dll")}

    # Everything already in the tree can satisfy an import, wherever it sits --
    # the 3D plugins live in bin/plugins/3d, not bin.
    have = {p.name.lower(): p for p in args.stage.rglob("*.dll")}
    roots = [p for p in args.stage.rglob("*") if p.suffix.lower() in (".exe", ".dll", ".kiface", ".pyd")]
    print(f"scanning {len(roots)} binaries against {len(donor)} donor DLLs")

    queue = list(roots)
    seen: set[str] = set()
    copied: list[str] = []
    missing: dict[str, set[str]] = {}

    while queue:
        cur = queue.pop()
        key = str(cur).lower()
        if key in seen:
            continue
        seen.add(key)

        for imp in pe_imports(cur):
            low = imp.lower()
            if low.startswith(SYSTEM_PREFIXES) or low in have:
                continue
            if low in donor:
                dest = bindir / donor[low].name
                if not dest.exists():
                    shutil.copy2(donor[low], dest)
                    copied.append(donor[low].name)
                have[low] = dest
                queue.append(dest)          # walk its imports too
            elif (SYSTEM32 / imp).is_file():
                continue                     # genuine OS DLL
            else:
                missing.setdefault(low, set()).add(cur.name)

    print(f"  copied {len(copied)} runtime DLLs into bin/")
    for n in sorted(copied)[:12]:
        print(f"    {n}")
    if len(copied) > 12:
        print(f"    ... and {len(copied) - 12} more")

    if missing:
        print(f"\nERROR: {len(missing)} import(s) cannot be resolved -- "
              f"this package would fail to launch:")
        for name in sorted(missing):
            users = ", ".join(sorted(missing[name])[:3])
            print(f"    {name}   needed by {users}")
        return 1

    print("  dependency closure complete: every import resolves")
    return 0


if __name__ == "__main__":
    sys.exit(main())
