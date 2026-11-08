# SPDX-License-Identifier: GPL-3.0-or-later
<#
 make-windows-release.ps1: one command that builds lvdsim and produces the Windows release files in a NEW folder.

 Usage (from anywhere):
   scripts\make-windows-release.bat [options]
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\make-windows-release.ps1 [options]

 Options:
   -Version x.y.z   default: the version in project(lvdsim VERSION ...) of the top-level CMakeLists.txt
   -OutDir DIR      default: build\release\<version>-<yyyyMMdd-HHmmss>. The folder must not exist: nothing is
                    ever deleted or overwritten.
   -Preset NAME     CMake preset to build (default mingw-release)
   -SkipBuild       reuse the existing build tree of the preset (the nvc build in kernel\nvc\build is always reused)
   -NoInstaller     do not make the Inno Setup installer
   -NoPortable      do not make the portable zip
   -Help            this text

 Produces in OutDir:
   lvdsim-<ver>-win64-setup.exe      installer (Inno Setup, per user by default, unsigned)
   lvdsim-<ver>-win64-portable.zip   same content plus portable.txt and README-portable.txt, top folder
                                     lvdsim-<ver>-win64-portable (settings and libraries stay in its data folder)
   SHA256SUMS.txt                    "<hash>  <file>" per artifact, UTF-8 without BOM
   build-info.txt                    version, date, git commit (if any), nvc commit (kernel/nvc), Qt, compiler
   stage\lvdsim-<ver>-win64\         the assembled folder (installer payload; no portable.txt); may be deleted afterwards

 Before it prints OK the script checks: installer size, zip content (lvdsim-gui.exe, nvc\bin\nvc.exe, tcl\compat\lvdcompat.tcl,
 scripts\libs, licenses\NVC-GPL-3.0.txt, portable.txt), no portable.txt in the installer payload, every DLL import of every
 PE file resolves inside the package or Windows (needs objdump from C:\msys64\mingw64\bin; a missing objdump is a warning),
 and a smoke run of the packaged vlib, vcom and vsim on a tiny VHDL file with a minimal environment.

 Exit codes: 0 ok, 1 any failure (the message says which check failed), 2 bad usage.
#>
param(
    [string]$Version = '',
    [string]$OutDir = '',
    [string]$Preset = 'mingw-release',
    [switch]$SkipBuild,
    [switch]$NoInstaller,
    [switch]$NoPortable,
    [switch]$Help
)

function Show-Usage {
    $text = Get-Content -LiteralPath $PSCommandPath -Raw
    $m = [regex]::Match($text, '(?s)<#(.*?)#>')
    Write-Host $m.Groups[1].Value.TrimEnd()
}
if ($Help) { Show-Usage; exit 0 }

$ErrorActionPreference = 'Stop'
$warnings = New-Object System.Collections.Generic.List[string]
function Step([string]$m) { Write-Host ("== " + $m) }
function Warn([string]$m) { $warnings.Add($m); Write-Host ("WARNING: " + $m) }
function Fail([string]$m) { throw $m }

try {
    $root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    $cmakeLists = Join-Path $root 'CMakeLists.txt'

    # ---- version: single source of truth is CMakeLists.txt ----
    if (-not $Version) {
        $m = [regex]::Match((Get-Content $cmakeLists -Raw), 'project\(lvdsim\s+VERSION\s+([0-9][0-9.]*)')
        if (-not $m.Success) { Fail "cannot read the version: no project(lvdsim VERSION x.y.z ...) line in $cmakeLists (or pass -Version)" }
        $Version = $m.Groups[1].Value
    }
    if ($Version -notmatch '^[0-9][0-9A-Za-z.+-]*$') { Fail "bad version '$Version'" }

    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    if (-not $OutDir) { $OutDir = Join-Path $root "build/release/$Version-$stamp" }
    $OutDir = [System.IO.Path]::GetFullPath($OutDir)
    if (Test-Path -LiteralPath $OutDir) { Fail "$OutDir already exists. This script never deletes or overwrites: pass another -OutDir or wait a second." }

    $name       = "lvdsim-$Version-win64"
    $portName   = "$name-portable"
    $stageRoot  = Join-Path $OutDir 'stage'
    $stage      = Join-Path $stageRoot $name
    $setupName  = "$name-setup.exe"
    $setupFile  = Join-Path $OutDir $setupName
    $zipFile    = Join-Path $OutDir "$portName.zip"
    $msysBin    = 'C:/msys64/mingw64/bin'
    $mingwBin   = 'C:/Qt/Tools/mingw1310_64/bin'
    $kMinInstallerBytes = 20MB

    Step "lvdsim $Version -> $OutDir"
    New-Item -ItemType Directory -Path $OutDir | Out-Null

    # ---- build + assemble the folder (and the installer) through package-windows.ps1 ----
    $pkgArgs = @{ OutDir = $stageRoot; Preset = $Preset; Version = $Version; NoZip = $true }
    if ($SkipBuild)   { $pkgArgs.SkipBuild = $true }
    if ($NoInstaller) { $pkgArgs.NoInstaller = $true }
    Step 'build and assemble (scripts\package-windows.ps1)'
    & (Join-Path $PSScriptRoot 'package-windows.ps1') @pkgArgs
    if (-not (Test-Path -LiteralPath $stage)) { Fail "package folder $stage was not created" }

    if (-not $NoInstaller) {
        $made = Join-Path $stageRoot $setupName
        if (-not (Test-Path -LiteralPath $made)) { Fail 'the installer was not produced: is Inno Setup installed (C:\Program Files\Inno Setup 7\ISCC.exe)?' }
        Move-Item -LiteralPath $made -Destination $setupFile
    }

    # ---- installer payload must not be portable ----
    if (Test-Path -LiteralPath (Join-Path $stage 'portable.txt')) { Fail "portable.txt found in the installer payload folder $stage" }

    # ---- portable zip: the same files under lvdsim-<ver>-win64-portable/, plus the marker and a readme ----
    $portableReadme = @"
lvdsim $Version - portable edition (Windows x64)

No installer, no registry. Unzip this folder anywhere (a USB stick works), then start lvdsim-gui.exe.
Command line tools: open a terminal in this folder and run vlib, vcom, vlog, vsim (or lvdsim-cli).
They find nvc and the Tcl package relative to their own location; nothing is added to PATH.

Portable mode is switched on by the file portable.txt next to lvdsim-gui.exe (delete it to use the normal per-user
settings in the registry). Everything the program saves stays in the data folder of this directory:
  data\lvdsim-gui.ini   window layout and options
  data\libraries        default output folder of Tools > Compile libraries
Moving or copying the whole folder moves the settings and the compiled libraries with it.
Do not run it from a read-only location (a DVD, a locked network share): the GUI then falls back to the normal
per-user settings and prints a note in the transcript.
Temporary files go to the system temp folder. An explicit "lvdsim-gui --settings FILE" still wins.
The environment variable LVDSIM_PORTABLE=1 turns portable mode on for a copy without portable.txt.

Licences: licenses\ (GPL-3.0-or-later for lvdsim and NVC). The files are not code signed.
"@
    $portableMarker = @"
lvdsim portable mode marker. While this file sits next to lvdsim-gui.exe the program keeps its settings in
data\lvdsim-gui.ini and compiled libraries in data\libraries (nothing in the registry). Delete it to turn that off.
"@
    if (-not $NoPortable) {
        Step 'portable zip'
        Add-Type -AssemblyName System.IO.Compression
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $utf8 = New-Object System.Text.UTF8Encoding($false)
        $stageFull = (Resolve-Path -LiteralPath $stage).Path.TrimEnd('\')
        $zip = [System.IO.Compression.ZipFile]::Open($zipFile, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($f in (Get-ChildItem -LiteralPath $stageFull -Recurse -File)) {
                $rel = $f.FullName.Substring($stageFull.Length + 1).Replace('\', '/')
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, "$portName/$rel", [System.IO.Compression.CompressionLevel]::Optimal)
            }
            foreach ($extra in @(@('portable.txt', $portableMarker), @('README-portable.txt', $portableReadme))) {
                $e = $zip.CreateEntry("$portName/$($extra[0])", [System.IO.Compression.CompressionLevel]::Optimal)
                $w = New-Object System.IO.StreamWriter($e.Open(), $utf8)
                $w.Write(($extra[1] -replace "`r?`n", "`r`n"))
                $w.Dispose()
            }
        } finally { $zip.Dispose() }
    }

    # ---- checksums ----
    Step 'checksums'
    $artifacts = @()
    if (-not $NoInstaller) { $artifacts += $setupFile }
    if (-not $NoPortable)  { $artifacts += $zipFile }
    $sumLines = @()
    foreach ($a in $artifacts) {
        $h = (Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash.ToLower()
        $sumLines += "$h  $(Split-Path $a -Leaf)"
    }
    $noBom = New-Object System.Text.UTF8Encoding($false)
    if ($sumLines.Count) { [System.IO.File]::WriteAllText((Join-Path $OutDir 'SHA256SUMS.txt'), (($sumLines -join "`n") + "`n"), $noBom) }

    # ---- build-info.txt ----
    Step 'build-info.txt'
    $gitCommit = 'none (no git commit available)'
    try {
        $g = (& git -C $root rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and $g) { $gitCommit = ($g | Select-Object -First 1).Trim() }
    } catch { }
    $nvcDir = Join-Path $root 'kernel/nvc'
    $nvcCommit = 'unknown'; $nvcRemote = 'unknown'; $nvcDirty = 'unknown'
    try {
        $c = (& git -C $nvcDir rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and $c) { $nvcCommit = ($c | Select-Object -First 1).Trim() }
        $u = (& git -C $nvcDir remote get-url origin 2>$null)
        if ($LASTEXITCODE -eq 0 -and $u) { $nvcRemote = ($u | Select-Object -First 1).Trim() }
        $nvcDirty = @(& git -C $nvcDir status --porcelain 2>$null).Count
    } catch { }
    $cache = Join-Path $root "build/$Preset/CMakeCache.txt"
    $qtVer = 'unknown'; $compiler = 'unknown'
    if (Test-Path $cache) {
        $c = Get-Content $cache -Raw
        $qm = [regex]::Match($c, 'Qt6_DIR:PATH=.*?/Qt/([0-9][0-9.]*)/')
        if ($qm.Success) { $qtVer = $qm.Groups[1].Value }
        $cm = [regex]::Match($c, '(?m)^CMAKE_CXX_COMPILER:[A-Z]+=(.+)$')
        $cxx = $cm.Groups[1].Value.Trim()
        if ($cm.Success -and (Test-Path -LiteralPath $cxx)) {
            $compiler = (& $cxx --version | Select-Object -First 1).ToString().Trim()
        }
    }
    $nvcExe = Join-Path $root 'kernel/nvc/build/bin/nvc.exe'
    $nvcVer = 'unknown'
    $oldPath = $env:PATH
    try {
        $env:PATH = "$msysBin;$env:PATH"
        $nvcVer = ((& $nvcExe --version 2>$null) | Select-Object -First 1).ToString().Trim()
    } catch { } finally { $env:PATH = $oldPath }
    if ($nvcCommit -ne 'unknown' -and $nvcVer -ne 'unknown' -and -not $nvcVer.Contains($nvcCommit.Substring(0, 7))) {
        Warn "nvc.exe reports '$nvcVer', which does not contain the commit $($nvcCommit.Substring(0, 9)) of kernel/nvc: it may not be built from the current kernel (rebuild with kernel/build/build-nvc-msys2.sh)"
    }
    $info = @(
        "lvdsim $Version (win64)",
        "built:        $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')",
        "preset:       $Preset",
        "git commit:   $gitCommit",
        "nvc commit:   $nvcCommit (kernel/nvc, branch lvdsim, $nvcRemote)",
        "nvc tree:     $nvcDirty uncommitted change(s) in kernel/nvc",
        "nvc version:  $nvcVer",
        "Qt:           $qtVer",
        "compiler:     $compiler (our programs); nvc built with MSYS2 MINGW64",
        "files:        " + (($artifacts | ForEach-Object { Split-Path $_ -Leaf }) -join ', ')
    )
    [System.IO.File]::WriteAllText((Join-Path $OutDir 'build-info.txt'), (($info -join "`n") + "`n"), $noBom)

    # ================= verification =================
    Step 'verify'
    if (-not $NoInstaller) {
        $len = (Get-Item -LiteralPath $setupFile).Length
        if ($len -lt $kMinInstallerBytes) { Fail "installer is only $len bytes (expected more than $kMinInstallerBytes)" }
        Write-Host ("  installer {0:N1} MB" -f ($len / 1MB))
    }

    if (-not $NoPortable) {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $za = [System.IO.Compression.ZipFile]::OpenRead($zipFile)
        try {
            $names = @($za.Entries | ForEach-Object { $_.FullName })
        } finally { $za.Dispose() }
        $required = 'lvdsim-gui.exe', 'lvdsim-cli.exe', 'vsim.exe', 'nvc/bin/nvc.exe', 'tcl/compat/lvdcompat.tcl',
                    'scripts/libs/compile_libs.bat', 'scripts/libs/compile_libs.sh', 'licenses/NVC-GPL-3.0.txt',
                    'portable.txt', 'README-portable.txt', 'platforms/qwindows.dll'
        foreach ($r in $required) {
            if ($names -notcontains "$portName/$r") { Fail "portable zip lacks $portName/$r" }
        }
        $stageCount = @(Get-ChildItem -LiteralPath $stage -Recurse -File).Count
        if ($names.Count -ne $stageCount + 2) { Fail "portable zip has $($names.Count) entries, expected $($stageCount + 2)" }
        if ($names | Where-Object { $_ -match '\\' }) { Fail 'portable zip has backslashes in entry names' }
        Write-Host ("  portable zip {0:N1} MB, {1} entries, required files present" -f ((Get-Item -LiteralPath $zipFile).Length / 1MB), $names.Count)
    }
    foreach ($r in 'lvdsim-gui.exe', 'nvc/bin/nvc.exe', 'tcl/compat/lvdcompat.tcl', 'scripts/libs/compile_libs.bat', 'licenses/NVC-GPL-3.0.txt') {
        if (-not (Test-Path -LiteralPath (Join-Path $stage $r))) { Fail "package folder lacks $r" }
    }
    if (Test-Path -LiteralPath (Join-Path $stage 'portable.txt')) { Fail 'portable.txt in the installer payload' }
    Write-Host '  installer payload has no portable.txt'

    # ---- dependency scan: every import resolves in the same folder or in Windows ----
    $objdump = Join-Path $msysBin 'objdump.exe'
    if (-not (Test-Path $objdump)) {
        Warn "objdump not found at ${objdump}: dependency scan skipped"
    } else {
        $stageFull = (Resolve-Path -LiteralPath $stage).Path.TrimEnd('\')
        $pe = @(Get-ChildItem -LiteralPath $stageFull -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll' })
        $have = @{}
        foreach ($p in $pe) { $have[$p.Name.ToLower()] = $true }
        $sysDirs = @("$env:SystemRoot\System32", $env:SystemRoot)
        $missing = @{}; $elsewhere = 0; $cache2 = @{}
        foreach ($p in $pe) {
            $out = & $objdump -p $p.FullName 2>$null | Select-String 'DLL Name:'
            foreach ($line in $out) {
                $dll = ($line.ToString() -split 'DLL Name:')[1].Trim()
                if (Test-Path -LiteralPath (Join-Path $p.DirectoryName $dll)) { continue }
                $k = $dll.ToLower()
                if ($k -like 'api-ms-win-*' -or $k -like 'ext-ms-win-*') { continue }
                if (-not $cache2.ContainsKey($k)) {
                    $cache2[$k] = [bool]($sysDirs | Where-Object { Test-Path -LiteralPath (Join-Path $_ $dll) })
                }
                if ($cache2[$k]) { continue }
                if ($have.ContainsKey($k)) { $elsewhere++; continue }   # Qt plugins in subfolders, siginit.dll -> nvc.exe
                if (-not $missing.ContainsKey($dll)) { $missing[$dll] = $p.FullName.Substring($stageFull.Length + 1) }
            }
        }
        if ($missing.Count) {
            $list = ($missing.GetEnumerator() | ForEach-Object { "$($_.Key) (needed by $($_.Value))" }) -join '; '
            Fail "dependency scan: imports that resolve nowhere in the package or Windows: $list"
        }
        Write-Host ("  dependency scan: {0} PE files, all imports resolve ({1} cross-folder references of plugins, expected)" -f $pe.Count, $elsewhere)
    }

    # ---- smoke run: packaged vlib/vcom/vsim, minimal environment, temp folder ----
    $smoke = Join-Path ([System.IO.Path]::GetTempPath()) ("lvdsim-smoke-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $smoke | Out-Null
    $vhd = @"
entity smoke is end entity;
architecture a of smoke is
  signal s : bit := '0';
begin
  process begin
    wait for 1 ns; s <= '1';
    wait for 1 ns;
    report "lvdsim-smoke-ok";
    wait;
  end process;
end architecture;
"@
    [System.IO.File]::WriteAllText((Join-Path $smoke 'smoke.vhd'), $vhd, $noBom)
    function Invoke-Packaged([string]$exe, [string[]]$arguments) {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = Join-Path $stage $exe
        $psi.Arguments = ($arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
        $psi.WorkingDirectory = $smoke
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.CreateNoWindow = $true
        $psi.EnvironmentVariables.Clear()
        $psi.EnvironmentVariables['SystemRoot'] = $env:SystemRoot
        $psi.EnvironmentVariables['TEMP'] = $smoke
        $psi.EnvironmentVariables['TMP'] = $smoke
        $psi.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
        $p = [System.Diagnostics.Process]::Start($psi)
        $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
        if (-not $p.WaitForExit(120000)) { try { $p.Kill() } catch { }; Fail "smoke run: $exe timed out" }
        [pscustomobject]@{ Code = $p.ExitCode; Text = ($o.Result + $e.Result) }
    }
    $r = Invoke-Packaged 'vlib.exe' @('work')
    if ($r.Code) { Fail "smoke run: vlib exit $($r.Code): $($r.Text)" }
    $r = Invoke-Packaged 'vcom.exe' @('-2008', 'smoke.vhd')
    if ($r.Code) { Fail "smoke run: vcom exit $($r.Code): $($r.Text)" }
    $r = Invoke-Packaged 'vsim.exe' @('-c', '-do', 'run 10 ns; quit', 'work.smoke')
    if ($r.Code -or $r.Text -notmatch 'lvdsim-smoke-ok') { Fail "smoke run: vsim exit $($r.Code), expected output missing: $($r.Text)" }
    Write-Host '  smoke run: vlib, vcom, vsim -c ok (minimal environment)'
    Remove-Item -LiteralPath $smoke -Recurse -Force   # temp folder created by this script

    Step 'done'
    Write-Host "Output folder: $OutDir"
    Get-ChildItem -LiteralPath $OutDir -File | ForEach-Object { Write-Host ("  {0,-44} {1,14:N0} bytes" -f $_.Name, $_.Length) }
    if ($sumLines.Count) { $sumLines | ForEach-Object { Write-Host "  sha256 $_" } }
    if ($warnings.Count) { Write-Host ("OK with {0} warning(s)" -f $warnings.Count) } else { Write-Host 'OK' }
    exit 0
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
