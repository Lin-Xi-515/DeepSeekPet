@echo off
rem ===========================================================================
rem  DeepSeekPet build script
rem  Requires MSVC + Windows SDK (Visual Studio, or the copy bundled with CLion).
rem  Output: build\DeepSeekPet.exe  (assets\ is copied next to it)
rem
rem  NOTE: this file is intentionally ASCII-only. cmd.exe mis-parses non-ASCII
rem        comments in .bat files, so keep it that way when editing.
rem ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo [1/4] Looking for a Visual Studio toolchain...

set "VSDEVCMD="

rem --- 1) well-known Visual Studio install locations -------------------------
for %%R in (
  "D:\my\DEVELOPER\VisualStudio"
  "C:\Program Files\Microsoft Visual Studio\2022\Community"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
  "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
  "C:\Program Files\Microsoft Visual Studio\18\Community"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
) do (
  if not defined VSDEVCMD if exist "%%~R\Common7\Tools\VsDevCmd.bat" (
    for /d %%V in ("%%~R\VC\Tools\MSVC\*") do (
      if not defined VSDEVCMD if exist "%%V\bin\Hostx64\x64\cl.exe" (
        set "VSDEVCMD=%%~R\Common7\Tools\VsDevCmd.bat"
      )
    )
  )
)

rem --- 2) ask the VS installer (vswhere) -------------------------------------
if not defined VSDEVCMD (
  for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    if exist "%%I\VC\Tools\MSVC" set "VSDEVCMD=%%I\Common7\Tools\VsDevCmd.bat"
  )
)

rem --- 3) the Visual Studio toolchain bundled with CLion ---------------------
if not defined VSDEVCMD (
  for %%R in (
    "C:\Program Files\JetBrains\CLion 2026.1\Microsoft Visual Studio\2022\Professional"
    "C:\Program Files\JetBrains\CLion 2026.1\Microsoft Visual Studio\2022\Community"
    "C:\Program Files\JetBrains\CLion 2025.3\Microsoft Visual Studio\2022\Professional"
    "C:\Program Files\JetBrains\CLion 2025.2\Microsoft Visual Studio\2022\Professional"
  ) do (
    if not defined VSDEVCMD if exist "%%~R\Common7\Tools\VsDevCmd.bat" (
      for /d %%V in ("%%~R\VC\Tools\MSVC\*") do (
        if not defined VSDEVCMD if exist "%%V\bin\Hostx64\x64\cl.exe" (
          set "VSDEVCMD=%%~R\Common7\Tools\VsDevCmd.bat"
        )
      )
    )
  )
)

if not defined VSDEVCMD (
  echo [ERROR] No usable Visual Studio toolchain found - cl.exe is missing.
  echo.
  echo   Options:
  echo     1^) Reinstall the "Desktop development with C++" workload with the
  echo        Visual Studio Installer.
  echo     2^) Open this folder in CLion as a CMake project - see CMakeLists.txt.
  echo     3^) Edit this script and point VSDEVCMD at your own VsDevCmd.bat.
  pause
  exit /b 1
)

echo       using: %VSDEVCMD%
call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
  echo [ERROR] Failed to initialize the MSVC environment.
  pause
  exit /b 1
)

if not exist build mkdir build
if not exist build\tmp mkdir build\tmp
set "TMP=%CD%\build\tmp"
set "TEMP=%CD%\build\tmp"

echo [2/4] Compiling resources and sources...
pushd build
rc /nologo /I "..\src" /fo pet.res "..\src\pet.rc"
if errorlevel 1 (
  echo [ERROR] Resource compilation failed.
  popd
  pause
  exit /b 1
)

cl /nologo /utf-8 /std:c++17 /EHsc /O2 /MT /W3 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
   /Fo"DeepSeekPet.obj" "..\src\main.cpp" pet.res ^
   /link /SUBSYSTEM:WINDOWS /OUT:DeepSeekPet.exe /MANIFEST:NO ^
   user32.lib gdi32.lib gdiplus.lib winhttp.lib shell32.lib ole32.lib advapi32.lib windowscodecs.lib shlwapi.lib
if errorlevel 1 (
  echo [ERROR] Build failed.
  popd
  pause
  exit /b 1
)
popd

echo [3/4] Copying assets...
if exist "build\assets" rmdir /s /q "build\assets"
xcopy /e /i /q /y "assets" "build\assets" >nul

echo [4/4] Done.
echo.
echo   exe     : %CD%\build\DeepSeekPet.exe
echo   config  : %%LOCALAPPDATA%%\DeepSeekPet\config.ini
echo   log     : %%LOCALAPPDATA%%\DeepSeekPet\pet.log
echo.
echo   First run pops up a settings window - fill in your DeepSeek API Key there.
echo   Tip: to change the artwork, run tools\make_assets.py, then run this script again.
echo.
pause
