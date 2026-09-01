@echo off
setlocal
rem Local Windows build for CopperAI (KiCad 10).
rem
rem   build-windows-local.bat [configure|build|all]
rem
rem Set JOBS to override link/compile parallelism (default 6). Do NOT raise this
rem much: ninja's default of cores+2 exhausts commit charge while eeschema's
rem PCH-heavy translation units compile, and MSVC dies with
rem   c1xx: error C3859: Failed to create virtual memory for PCH
rem   c1xx: fatal error C1076: compiler limit: internal heap limit reached
rem Budget 1-2 GB per concurrent cl.exe. Close any running eeschema/kicad first;
rem leftover instances have twice starved the compiler.

set "SRC=C:\src\Kicad_10.0.0"
set "BUILD_DIR=build\copperai-win64"
set "STAGE=build\install\copperai-win64"
if "%JOBS%"=="" set "JOBS=6"

set "STAGE_ARG=%~1"
if "%STAGE_ARG%"=="" set "STAGE_ARG=build"

rem Resolve MSVC rather than assuming an edition: Community and Build Tools
rem install to different directories.
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCINSTALL=%%i"
if not defined VCINSTALL ( echo FAIL: no VS install with the C++ toolset & exit /b 1 )

rem vcvars64 clobbers VCPKG_ROOT with the VS-bundled vcpkg; stash and restore.
if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=C:\vcpkg"
set "REAL_VCPKG_ROOT=%VCPKG_ROOT%"
call "%VCINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo FAIL: vcvars64 & exit /b 1 )
set "VCPKG_ROOT=%REAL_VCPKG_ROOT%"

rem SWIG is on PATH only as a winget shim, which has no adjacent Lib/ directory.
rem CMake's FindSWIG looks for Lib/ next to the resolved swig.exe, so point it at
rem the real install or find_package(SWIG) fails while `swig --version` works.
if "%SWIG_HOME%"=="" set "SWIG_HOME=%LOCALAPPDATA%\Microsoft\WinGet\Packages\SWIG.SWIG_Microsoft.Winget.Source_8wekyb3d8bbwe\swigwin-4.4.1"

cd /d "%SRC%"

if /i "%STAGE_ARG%"=="build" goto :build

echo === CONFIGURE ===
rem Explicit -D flags rather than --preset: CMakePresets.json is gitignored
rem (.gitignore:122) so it never exists in a clean checkout.
rem CMAKE_INSTALL_PREFIX must be set HERE, not via `cmake --install --prefix`:
rem KiCad bakes KICAD_DATA from it as an absolute CACHE STRING, and a later
rem --prefix override silently splits the install across two trees.
cmake -S . -B %BUILD_DIR% -G Ninja ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_INSTALL_PREFIX=%CD%\%STAGE% ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DVCPKG_OVERLAY_TRIPLETS=tools/custom_vcpkg_triplets ^
    -DKICAD_BUILD_QA_TESTS=OFF ^
    -DKICAD_SCRIPTING_WXPYTHON=ON ^
    -DKICAD_WIN32_DPI_AWARE=ON ^
    -DKICAD_IPC_API=ON ^
    -DSWIG_EXECUTABLE="%SWIG_HOME%\swig.exe" ^
    -DSWIG_DIR="%SWIG_HOME%\Lib"
if errorlevel 1 ( echo. & echo CONFIGURE FAILED & exit /b 1 )
echo === CONFIGURE OK ===

if /i "%STAGE_ARG%"=="configure" exit /b 0

:build
echo.
echo === BUILD (-j %JOBS%) ===
cmake --build %BUILD_DIR% -- -j %JOBS%
if errorlevel 1 ( echo. & echo BUILD FAILED & exit /b 1 )
echo === BUILD OK ===
exit /b 0
