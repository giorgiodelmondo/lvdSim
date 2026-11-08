@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem Windows release in one command: installer, portable zip, checksums. Options are passed to
rem make-windows-release.ps1 (-Version -OutDir -Preset -SkipBuild -NoInstaller -NoPortable -Help).
rem The execution policy is bypassed for this one process only (our own script, no system change).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make-windows-release.ps1" %*
exit /b %ERRORLEVEL%
