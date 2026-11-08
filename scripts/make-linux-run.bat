@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Linux release from a Windows host: runs scripts/make-linux-run.sh inside WSL (distro Debian, or LVDSIM_WSL_DISTRO).
rem Usage: scripts\make-linux-run.bat [--copy-to-windows DIR] [options of make-linux-run.sh]
rem   --copy-to-windows DIR  copy the artifacts (.run, portable tarball, SHA256SUMS, build-info.txt) into DIR, which must
rem                          not exist yet (default: build\release\linux-<version>-<timestamp> in the repository), and print
rem                          the SHA-256 of the copies. Without the option the files stay in WSL (path printed at the end).
rem   all other options (--portable --nvc-build --out --version --skip-build --compress --jobs --force-nvc) are passed on;
rem   --nvc-build and --out take Linux paths. Try --help for the full list.
setlocal EnableDelayedExpansion

set "DISTRO=%LVDSIM_WSL_DISTRO%"
if "%DISTRO%"=="" set "DISTRO=Debian"

rem repository root as a Linux path
pushd "%~dp0.."
set "WINROOT=%CD%"
popd
set "FWD=%WINROOT:\=/%"
for /f "tokens=*" %%i in ('wsl -d %DISTRO% -- wslpath -a "%FWD%" 2^>nul') do set "LROOT=%%i"
if not defined LROOT (
    echo ERROR: could not run wslpath in WSL distro "%DISTRO%". Is it installed? Set LVDSIM_WSL_DISTRO to another name.
    exit /b 1
)

set "COPYTO="
set "COPY=0"
set "PASS="
:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--copy-to-windows" (
    set "COPY=1"
    if not "%~2"=="" (
        set "NEXT=%~2"
        if not "!NEXT:~0,2!"=="--" (
            set "COPYTO=%~2"
            shift
        )
    )
    shift
    goto parse
)
set PASS=!PASS! "%~1"
shift
goto parse
:parsed

set "OUTFILE=/tmp/lvdsim-release-out.%RANDOM%%RANDOM%.txt"
wsl -d %DISTRO% -- bash "%LROOT%/scripts/make-linux-run.sh" --write-out-path %OUTFILE% !PASS!
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo make-linux-run.sh failed with exit code %RC%
    exit /b %RC%
)

for /f "tokens=*" %%i in ('wsl -d %DISTRO% -- cat %OUTFILE%') do set "LOUT=%%i"
if not defined LOUT (
    echo ERROR: could not read the output folder from WSL
    exit /b 1
)
echo Linux output folder (inside WSL %DISTRO%): %LOUT%
if "%COPY%"=="0" exit /b 0

rem default destination: build\release\linux-<folder name of the Linux output>
if "!COPYTO!"=="" (
    for /f "tokens=*" %%i in ('wsl -d %DISTRO% -- basename "%LOUT%"') do set "BASE=%%i"
    set "COPYTO=%WINROOT%\build\release\linux-!BASE!"
)
if exist "!COPYTO!" (
    echo ERROR: !COPYTO! already exists; this script never overwrites. Pass another --copy-to-windows DIR.
    exit /b 1
)
mkdir "!COPYTO!"
if errorlevel 1 exit /b 1
set "FCOPY=!COPYTO:\=/!"
for /f "tokens=*" %%i in ('wsl -d %DISTRO% -- wslpath -a "!FCOPY!"') do set "LCOPY=%%i"
wsl -d %DISTRO% -- bash -c "cd '%LOUT%' && cp -p *.run *.tar.gz SHA256SUMS build-info.txt '!LCOPY!/' 2>/dev/null; ls '!LCOPY!'"
echo.
echo SHA-256 of the copies in !COPYTO!:
for %%f in ("!COPYTO!\*.run" "!COPYTO!\*.tar.gz") do (
    for /f "skip=1 tokens=*" %%h in ('certutil -hashfile "%%~ff" SHA256 ^| findstr /v /c:"CertUtil"') do echo   %%h  %%~nxf
)
echo The expected values are in !COPYTO!\SHA256SUMS
exit /b 0
