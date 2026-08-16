# Setting up a self-hosted Windows build runner

What `windows-build.yml` expects from a runner, and the traps found while
setting up the first one (`copper-pdf`, 2026-08-16).

The workflow deliberately does **not** hardcode disk layout. It reads
`VCPKG_ROOT`, `VCPKG_BINARY_SOURCES`, `SWIG_DIR` and `SWIG_EXECUTABLE` from the
machine environment and resolves MSVC through `vswhere`. Set those up once per
box; the workflow adapts.

## Requirements

| Need | Notes |
|---|---|
| VS 2022 Build Tools | **with the VCTools workload** — Build Tools alone has no `cl.exe` |
| CMake | 3.28–3.31.x. **Not 4.x** — see below |
| Ninja | any recent |
| SWIG | must be the **swigwin** distribution, for its `Lib/` directory |
| Python, Git | any recent |
| vcpkg | must be recent — see below |
| Disk | ~100 GB. Source ~2 GB, vcpkg_installed ~3 GB, build tree ~12–15 GB, plus cache |
| RAM | budget 1–2 GB per concurrent `cl.exe` |

## Machine environment

Set at **machine** scope so the runner *service* inherits them:

```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "E:\build\vcpkg", "Machine")
[Environment]::SetEnvironmentVariable("VCPKG_BINARY_SOURCES", "clear;files,E:\build\vcpkg-cache,readwrite", "Machine")
[Environment]::SetEnvironmentVariable("SWIG_EXECUTABLE", "E:\build\tools\swigwin-4.4.1\swig.exe", "Machine")
[Environment]::SetEnvironmentVariable("SWIG_DIR", "E:\build\tools\swigwin-4.4.1\Lib", "Machine")
```

## Traps

- **CMake 4.x breaks the dependency build.** It removed support for
  `cmake_minimum_required(VERSION < 3.5)`, which many vcpkg ports still declare.
  Pin the 3.31.x line. Installing "latest" gets you 4.x.

- **vcpkg must not lag its `vcpkg-make` helper port.** An older tool silently
  fails to pass `--prefix` to autotools configure, so ports install to
  `<triplet>/usr/local/...` and vcpkg records the broken layout as a success. It
  surfaces later as unrelated-looking failures in gperf, gettext and icu. Keep
  vcpkg current; `tools/ci/fix_vcpkg_usrlocal_prefix.py` repairs a damaged tree.

- **SWIG on PATH is not sufficient.** `find_package(SWIG)` wants the `Lib/`
  directory beside the resolved `swig.exe`. A winget shim has none, so
  `swig --version` succeeds while CMake fails. Point `SWIG_DIR` at the real
  install.

- **Do not hardcode the VS edition path.** Community and Build Tools install to
  different directories. Resolve with `vswhere -requires
  Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`.

- **`vcvars64.bat` overwrites `VCPKG_ROOT`** with the VS-bundled vcpkg. Stash and
  restore around the call.

- **Ninja's default `-j` (cores+2) can exhaust memory.** eeschema's large
  translation units with precompiled headers fail as `C3859: Failed to create
  virtual memory for PCH` / `C1076: internal heap limit reached`. `BUILD_JOBS`
  caps it.

- **winget may be unusable.** On `copper-pdf` every install failed with
  `0x8a15000f : Data required by the source is missing` (corrupt source index).
  Installing tools straight from upstream release archives is more deterministic
  for a build box anyway.

- **PowerShell 5.1 `Invoke-WebRequest` cannot fetch from SourceForge.** It lands
  on the mirror-selection HTML page and silently writes that instead of the
  archive, which then fails to extract. Use `curl.exe` (shipped with Windows 10
  1803+) and verify the `PK` zip magic before trusting the download.

- **Upstream tarballs move.** ngspice 45.2 was relocated to an `old-releases`
  path. Fetch from the new location, verify SHA512 against the portfile, and
  drop it in `<vcpkg>\downloads\<expected-filename>`. Always verify the hash.

## Binary cache

`VCPKG_BINARY_SOURCES` is what makes this sustainable — without it, every run
rebuilds all 133 dependencies. The cache key includes compiler version and
triplet settings, so an MSVC upgrade or a triplet edit is a legitimate miss.

For a cache shared across machines, back it with GitHub Packages instead of a
local folder:

```
VCPKG_BINARY_SOURCES=clear;nuget,https://nuget.pkg.github.com/CopperAI-EDA/index.json,readwrite
```

Note that toolchains must match to share a cache: a box on MSVC 14.43 and one on
14.44 compute different ABI hashes and will not reuse each other's artifacts.

## Registering the runner

Repo → Settings → Actions → Runners → New self-hosted runner. The token is
short-lived, so generate it immediately before use.

```powershell
cd E:\actions-runner
.\config.cmd --url https://github.com/CopperAI-EDA/kicad-10.0.0 `
  --token <TOKEN> --name copper-pdf-win64 --labels self-hosted,windows,x64 `
  --work E:\actions-runner\_work --unattended --replace
.\svc.cmd install
.\svc.cmd start
```

Install it as a service so builds survive logout and reboot.

## Verifying

Before trusting a new box, compile something rather than checking that
`cl.exe` exists — a partial toolset install can satisfy the latter. Configure a
trivial CMake + Ninja C++ project, build it, and run the result.
