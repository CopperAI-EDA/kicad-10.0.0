from __future__ import annotations

import hashlib
import os
import uuid
from pathlib import Path
from xml.sax.saxutils import escape


ROOT = Path(os.environ.get("COPPERAI_INSTALL_ROOT", r"E:\ca\install"))
OUT = Path(os.environ.get("COPPERAI_WIX_OUT", r"E:\ca\msi\CopperAI.wxs"))
VERSION = os.environ.get("COPPERAI_MSI_VERSION", "0.1.12")
NS = uuid.UUID("6b0f06f9-7551-48ac-9b71-16d1189095d8")


def xml(value: str) -> str:
    return escape(value, {'"': "&quot;"})


def ident(prefix: str, value: str) -> str:
    digest = hashlib.sha1(value.encode("utf-8")).hexdigest()[:24].upper()
    return f"{prefix}_{digest}"


def guid(value: str) -> str:
    return "{" + str(uuid.uuid5(NS, value)).upper() + "}"


dirs = sorted(
    [p for p in ROOT.rglob("*") if p.is_dir()],
    key=lambda p: p.relative_to(ROOT).as_posix().lower(),
)
files = sorted(
    [p for p in ROOT.rglob("*") if p.is_file()],
    key=lambda p: p.relative_to(ROOT).as_posix().lower(),
)

dir_ids: dict[Path, str] = {ROOT: "INSTALLFOLDER"}

for directory in dirs:
    dir_ids[directory] = ident("D", directory.relative_to(ROOT).as_posix())

kicad_file_id = ""
component_lines: list[str] = []

# One component per DIRECTORY, not per file.
#
# Windows Installer's CostFinalize action -- the "Computing space requirements"
# step -- scales very badly with component count. Emitting a component per file
# gave 44,057 components once the symbol and footprint libraries were bundled
# (they are ~38,500 small files), and the installer appeared to hang there for
# many minutes. Grouping by directory drops that to a few hundred.
#
# The trade-off is that a component is the unit of install/repair, so files in
# one directory are no longer independently repairable. That is irrelevant here:
# nothing installs a subset, and the library directories are read-only data.
#
# The first file in each directory is the KeyPath. Component GUIDs are derived
# from the directory path, so they stay stable across rebuilds and upgrades
# continue to match up correctly.
files_by_dir: dict[Path, list[Path]] = {}

for file in files:
    files_by_dir.setdefault(file.parent, []).append(file)

for directory in sorted(files_by_dir, key=lambda d: d.relative_to(ROOT).as_posix().lower()):
    dir_rel = directory.relative_to(ROOT).as_posix() or "."
    comp_id = ident("C", dir_rel)

    component_lines.append(
        f'      <Component Id="{comp_id}" Directory="{dir_ids[directory]}" Guid="{guid(dir_rel)}">'
    )

    for index, file in enumerate(files_by_dir[directory]):
        rel = file.relative_to(ROOT).as_posix()
        file_id = ident("F", rel)

        if rel.lower() == "bin/kicad.exe":
            kicad_file_id = file_id

        # Exactly one KeyPath per component.
        keypath = ' KeyPath="yes"' if index == 0 else ""
        component_lines.append(
            f'        <File Id="{file_id}" Source="{xml(str(file))}"{keypath} />'
        )

    component_lines.append("      </Component>")

if not kicad_file_id:
    raise SystemExit("Could not find bin/kicad.exe in install tree")

children: dict[Path, list[Path]] = {}

for directory in dirs:
    children.setdefault(directory.parent, []).append(directory)


def emit_dir(directory: Path, indent: int = 4) -> list[str]:
    out: list[str] = []
    pad = " " * indent

    for child in children.get(directory, []):
        out.append(f'{pad}<Directory Id="{dir_ids[child]}" Name="{xml(child.name)}">')
        out.extend(emit_dir(child, indent + 2))
        out.append(f"{pad}</Directory>")

    return out


lines = [
    '<?xml version="1.0" encoding="utf-8"?>',
    '<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs" xmlns:ui="http://wixtoolset.org/schemas/v4/wxs/ui" xmlns:util="http://wixtoolset.org/schemas/v4/wxs/util">',
    f'  <Package Name="CopperAI" Manufacturer="Stropical" Version="{VERSION}" UpgradeCode="{{6B0F06F9-7551-48AC-9B71-16D1189095D8}}" Scope="perMachine">',
    '    <!-- AllowSameVersionUpgrades=yes: installing 1.2.0 over an existing 1.2.0 is otherwise not an upgrade at all, so Windows Installer reports success and leaves the old files in place. MSI compares only the first three version fields. -->',
    '    <MajorUpgrade AllowSameVersionUpgrades="yes" DowngradeErrorMessage="A newer version of CopperAI is already installed." />',
    '    <MediaTemplate EmbedCab="yes" />',
    '    <!-- Without a UI reference the package installs with no wizard at all: double-clicking it churns for minutes behind a bare progress bar, with no Next button and no completion dialog, which reads as a broken installer. WixUI_InstallDir gives the expected welcome / license / install-location / progress / finish flow. Requires building with -ext WixToolset.UI.wixext. -->',
    '    <ui:WixUI Id="WixUI_InstallDir" InstallDirectory="INSTALLFOLDER" />',
    '    <WixVariable Id="WixUILicenseRtf" Value="packaging/windows/license.rtf" />',
    f'    <CustomAction Id="LaunchCopperAI" FileRef="{kicad_file_id}" ExeCommand="" Execute="immediate" Return="asyncNoWait" />',
    '    <InstallExecuteSequence>',
    '      <Custom Action="ExtractLibraries" After="InstallFiles" Condition="NOT REMOVE" />',
    '      <Custom Action="LaunchCopperAI" After="InstallFinalize" Condition="NOT Installed" />',
    '    </InstallExecuteSequence>',
    '    <Feature Id="MainFeature" Title="CopperAI" Level="1">',
    '      <ComponentGroupRef Id="ProductComponents" />',
    '      <ComponentRef Id="ApplicationShortcut" />',
    '      <ComponentRef Id="CleanupLibraries" />',
    "    </Feature>",
    # ---- library archive -------------------------------------------------
    # The symbol/footprint libraries ship as one .tar.gz (tools/ci/pack_libraries.py)
    # because 38,500 individual File rows made every install, upgrade and
    # uninstall crawl. Unpack them here.
    #
    # Deferred + Impersonate=no: this writes into Program Files, so it needs the
    # elevated service context. Sequenced after InstallFiles so the archive is
    # on disk, and only when actually installing (not on uninstall/repair).
    '    <SetProperty Id="ExtractLibraries"',
    r'                 Value="&quot;[System64Folder]tar.exe&quot; -xzf &quot;[INSTALLFOLDER]share\kicad\copperai-libraries.tar.gz&quot; -C &quot;[INSTALLFOLDER]share\kicad&quot;"',
    '                 Before="ExtractLibraries" Sequence="execute" />',
    '    <CustomAction Id="ExtractLibraries" DllEntry="WixQuietExec64" Execute="deferred"',
    '                  Impersonate="no" Return="check" BinaryRef="Wix4UtilCA_X64" />',
    '',
    '    <!-- MSI only removes files it installed. The extracted libraries are not in',
    '         the File table, so without this they would be orphaned on uninstall:',
    '         roughly 380 MB left in Program Files. RemoveFolderEx reads the path',
    '         from the registry value written below. -->',
    r'    <SetProperty Id="LIBDIR_SYMBOLS" Value="[INSTALLFOLDER]share\kicad\symbols" Before="CostFinalize" Sequence="both" />',
    r'    <SetProperty Id="LIBDIR_FOOTPRINTS" Value="[INSTALLFOLDER]share\kicad\footprints" Before="CostFinalize" Sequence="both" />',
    r'    <SetProperty Id="LIBDIR_TEMPLATE" Value="[INSTALLFOLDER]share\kicad\template" Before="CostFinalize" Sequence="both" />',
    '',
    "  </Package>",
    "  <Fragment>",
    '    <StandardDirectory Id="ProgramFiles64Folder">',
    '      <Directory Id="INSTALLFOLDER" Name="CopperAI">',
    *emit_dir(ROOT, 8),
    "      </Directory>",
    "    </StandardDirectory>",
    '    <StandardDirectory Id="ProgramMenuFolder">',
    '      <Directory Id="ApplicationProgramsFolder" Name="CopperAI" />',
    "    </StandardDirectory>",
    "  </Fragment>",
    "  <Fragment>",
    '    <ComponentGroup Id="ProductComponents">',
    *component_lines,
    "    </ComponentGroup>",
    '    <!-- RemoveFolderEx must live inside a Component. The extracted library directories are not in the File table, so MSI would otherwise leave ~380 MB behind on uninstall. The registry value is the KeyPath and the paths come from the LIBDIR_* properties. -->',
    '    <Component Id=\"CleanupLibraries\" Directory=\"INSTALLFOLDER\" Guid=\"{4C1A7E90-3B62-4E55-9E1B-7A2D6F0C8B14}\">',
    '      <RegistryValue Root=\"HKLM\" Key=\"Software\CopperAI\" Name=\"LibraryDir\" Type=\"string\" Value=\"[INSTALLFOLDER]share\kicad\" KeyPath=\"yes\" />',
    '      <util:RemoveFolderEx On=\"uninstall\" Property=\"LIBDIR_SYMBOLS\" />',
    '      <util:RemoveFolderEx On=\"uninstall\" Property=\"LIBDIR_FOOTPRINTS\" />',
    '      <util:RemoveFolderEx On=\"uninstall\" Property=\"LIBDIR_TEMPLATE\" />',
    '    </Component>',
    '    <Component Id="ApplicationShortcut" Directory="ApplicationProgramsFolder" Guid="{912B78AC-1E87-4C22-B25C-E0C2B3E62957}">',
    f'      <Shortcut Id="StartMenuCopperAI" Name="CopperAI" Description="CopperAI KiCad" Target="[#{kicad_file_id}]" WorkingDirectory="D_BIN" />',
    '      <RemoveFolder Id="RemoveApplicationProgramsFolder" On="uninstall" />',
    '      <RegistryValue Root="HKLM" Key="Software\\CopperAI" Name="installed" Type="integer" Value="1" KeyPath="yes" />',
    "    </Component>",
    "  </Fragment>",
    "</Wix>",
]

text = "\n".join(lines) + "\n"
text = text.replace('WorkingDirectory="D_BIN"', f'WorkingDirectory="{dir_ids[ROOT / "bin"]}"')
OUT.write_text(text, encoding="utf-8")
print(f"Wrote {OUT} with {len(files)} files")
