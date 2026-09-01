@echo off
setlocal
rem Package a built tree into an MSI.
rem
rem   package-windows-local.bat [version]      (default 1.3.0)
rem
rem Run tools\ci\build-windows-local.bat first. Mirrors the CI packaging steps,
rem so a failure here is a failure CI would hit.
rem
rem NOT safe to run concurrently with itself: the staging tree, CopperAI.wxs and
rem the output directory are all shared, and a second run's rmdir will delete
rem files the first run's wix build is still reading.

set "SRC=C:\src\Kicad_10.0.0"
set "BUILD_DIR=build\copperai-win64"
set "STAGE=build\install\copperai-win64"
set "VERSION=%~1"
if "%VERSION%"=="" set "VERSION=1.3.0"

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCINSTALL=%%i"
if not defined VCINSTALL ( echo FAIL: no VS install with the C++ toolset & exit /b 1 )
call "%VCINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%PATH%;%USERPROFILE%\.dotnet\tools"

cd /d "%SRC%"

echo === INSTALL to the configured prefix ===
rem No --prefix override: it must match CMAKE_INSTALL_PREFIX from configure.
rem KiCad bakes KICAD_DATA from that as an absolute CACHE STRING, so overriding
rem here splits the install across two trees and silently drops ~700 files,
rem including images.tar.gz (every icon) and _pcbnew.pyd.
rmdir /s /q "%STAGE%" 2>nul
cmake --install %BUILD_DIR%
if errorlevel 1 ( echo. & echo INSTALL FAILED & exit /b 1 )

echo.
echo === STAGE third-party runtime DLLs ===
rem Nothing in KiCad's CMake installs vcpkg's runtime DLLs, and vcpkg's bin is
rem on PATH on the build machine -- so a package missing them launches fine here
rem and dies on a client PC with
rem   wxmsw332u_html_vc_x64_custom.dll was not found.
rem That shipped once already, in 1.3.0. The script walks PE import tables and
rem fails packaging if the closure is incomplete, so it cannot recur silently.
python tools/ci/stage_runtime_deps.py "%STAGE%" "%BUILD_DIR%/vcpkg_installed/x64-windows/bin"
if errorlevel 1 ( echo. & echo RUNTIME DEP STAGING FAILED & exit /b 1 )

echo.
echo === FETCH KiCad libraries ===
rem The staging tree is wiped above, which takes these with it. Without them the
rem package installs an editor with an empty symbol chooser.
python tools/ci/fetch_kicad_libraries.py "%STAGE%" --version 10.0.5
if errorlevel 1 ( echo. & echo LIBRARY FETCH FAILED & exit /b 1 )

echo.
echo === PACK libraries into one archive ===
rem ~38,500 individual files are 87%% of the package by file count. MSI keeps a
rem File table row per file and walks it during costing, InstallValidate,
rem InstallFiles and uninstall, so shipping them loose made the installer sit on
rem "Computing space requirements" and "Validating install" for minutes.
python tools/ci/pack_libraries.py "%STAGE%"
if errorlevel 1 ( echo. & echo LIBRARY PACK FAILED & exit /b 1 )

echo.
echo === GENERATE WiX source ===
if not exist build\msi mkdir build\msi
set "COPPERAI_INSTALL_ROOT=%STAGE%"
set "COPPERAI_WIX_OUT=build\msi\CopperAI.wxs"
set "COPPERAI_MSI_VERSION=%VERSION%"
python packaging\windows\gen_copperai_wix.py
if errorlevel 1 ( echo. & echo WXS GENERATION FAILED & exit /b 1 )

echo.
echo === BUILD MSI (x64) ===
rem -arch x64: without it wix emits a 32-bit package, which resolves
rem ProgramFiles64Folder to "Program Files (x86)".
rem -ext UI: the <ui:WixUI> wizard. Without a UI the installer shows no dialogs
rem at all and looks broken.
rem -ext Util: RemoveFolderEx, which cleans up the extracted libraries on
rem uninstall since MSI only removes files it placed itself.
wix build -arch x64 -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext ^
    build\msi\CopperAI.wxs -o "build\msi\CopperAI-%VERSION%-win64.msi"
if errorlevel 1 ( echo. & echo MSI BUILD FAILED & exit /b 1 )
echo === MSI OK ===

powershell -NoProfile -Command "Get-ChildItem 'build\msi\CopperAI-%VERSION%-win64.msi' | ForEach-Object { '{0}  {1:N1} MB' -f $_.Name, ($_.Length/1MB) }"
exit /b 0
