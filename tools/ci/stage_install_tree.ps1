<#
.SYNOPSIS
    Complete an installed tree so it can actually run, then prove that it does.

.DESCRIPTION
    `cmake --install` produces a tree that cannot start. Two things are missing:

    1. The vcpkg runtime DLLs. In the build tree these are present because
       vcpkg's applocal post-build step copies them beside each executable; that
       does not carry across to the install prefix. Without them every binary
       dies with STATUS_DLL_NOT_FOUND (0xC0000135) before reaching main().

    2. The JSON schemas. api/, common/ and kicad/pcm/ each copy their schemas
       into ${CMAKE_BINARY_DIR}/schemas at build time, but nothing installs them,
       so kicad-cli reports
           Error: schema file '...\share\kicad\schemas\api.v1.schema.json' not found

    The whole vcpkg runtime is copied rather than a computed subset. A transitive
    import-table walk was tried first and produced a tree that still failed to
    start -- dumpbin does not surface everything that is loaded (delay imports,
    and whatever the plugins pull in at runtime). 147 MB of DLLs is a small price
    for a package that works, and it is what KiCad's own Windows installer does.

.PARAMETER StageDir
    The installed tree (e.g. build\stage). Modified in place.

.PARAMETER BuildDir
    The build tree (e.g. build\copperai-win64), source of the vcpkg runtime and
    the generated schemas.

.PARAMETER SkipVerify
    Skip the run check. The verification is the point; only skip it when staging
    for a different machine.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $StageDir,
    [Parameter(Mandatory)] [string] $BuildDir,
    [switch] $SkipVerify
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $StageDir)) { throw "stage dir not found: $StageDir" }
if (-not (Test-Path $BuildDir)) { throw "build dir not found: $BuildDir" }

$vcpkgBin = Join-Path $BuildDir "vcpkg_installed\x64-windows\bin"
if (-not (Test-Path $vcpkgBin)) { throw "vcpkg runtime not found: $vcpkgBin" }

# ---------------------------------------------------------------- runtime DLLs
$destBin = Join-Path $StageDir "bin"
if (-not (Test-Path $destBin)) { throw "no bin/ in the staged tree: $destBin" }

$dlls = Get-ChildItem $vcpkgBin -Filter *.dll -File
$copied = 0
foreach ($d in $dlls) {
    $dest = Join-Path $destBin $d.Name
    # Skip identical files so re-runs are cheap and do not churn timestamps.
    if ((Test-Path $dest) -and (Get-Item $dest).Length -eq $d.Length) { continue }
    Copy-Item $d.FullName $dest -Force
    $copied++
}
"runtime DLLs : {0} copied, {1} already present ({2:N0} MB total)" -f `
    $copied, ($dlls.Count - $copied), (($dlls | Measure-Object Length -Sum).Sum / 1MB)

# ---------------------------------------------------------------- schemas
$srcSchemas = Join-Path $BuildDir "schemas"
$dstSchemas = Join-Path $StageDir "share\kicad\schemas"
if (Test-Path $srcSchemas) {
    New-Item -ItemType Directory -Force $dstSchemas | Out-Null
    $n = 0
    foreach ($f in Get-ChildItem $srcSchemas -Filter *.json -File) {
        Copy-Item $f.FullName (Join-Path $dstSchemas $f.Name) -Force
        $n++
    }
    "schemas      : $n installed to share\kicad\schemas"
} else {
    "schemas      : WARNING - $srcSchemas does not exist"
}

# ---------------------------------------------------------------- verify
if ($SkipVerify) {
    "`nstaging complete (verification skipped)"
    return
}

"`n=== verifying the staged tree actually runs ==="
$cli = Join-Path $destBin "kicad-cli.exe"
if (-not (Test-Path $cli)) { throw "kicad-cli.exe not in the staged tree" }

$out = Join-Path $env:TEMP "stage_verify_out.txt"
$err = Join-Path $env:TEMP "stage_verify_err.txt"
$p = Start-Process -FilePath $cli -ArgumentList "--version" -NoNewWindow -Wait -PassThru `
     -RedirectStandardOutput $out -RedirectStandardError $err

$stdout = (Get-Content $out -EA SilentlyContinue) -join "`n"
$stderr = (Get-Content $err -EA SilentlyContinue) -join "`n"

if ($p.ExitCode -ne 0) {
    "exit code : $($p.ExitCode)"
    if ($p.ExitCode -eq -1073741515) {
        "  0xC0000135 STATUS_DLL_NOT_FOUND -- a dependency is still missing"
    }
    if ($stderr) { "stderr:`n$stderr" }
    throw "staged tree does not run"
}

"kicad-cli --version -> $($stdout.Trim())"
if ($stderr) {
    # A warning here still means something is not packaged, even though the
    # process started. Surface it rather than letting it ship silently.
    "WARNING - stderr was not empty:"
    $stderr -split "`n" | Select-Object -First 5 | ForEach-Object { "  $_" }
    throw "staged tree runs but reports errors -- something is still missing"
}
"`nSTAGED TREE OK"
