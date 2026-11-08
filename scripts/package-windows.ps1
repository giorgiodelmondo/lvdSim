# SPDX-License-Identifier: GPL-3.0-or-later
<#
 Builds the mingw-release preset and assembles a self-contained Windows package:
   build/package/lvdsim-<version>-win64/        the folder
   build/package/lvdsim-<version>-win64.zip     the same folder as a zip
   build/package/lvdsim-<version>-win64-setup.exe   (only if Inno Setup is installed)
 Usage: powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 [-SkipBuild] [-NoInstaller] [-NoZip] [-OutDir path]
                                                                   [-Preset mingw-release] [-Version x.y.z]
 For a complete release (installer, portable zip, checksums, checks) use scripts/make-windows-release.ps1, which calls this script.
 The kernel must be built first (kernel/build/build-nvc-msys2.sh). See docs/packaging.md.
#>
param(
    [switch]$SkipBuild,
    [switch]$NoInstaller,
    [switch]$NoZip,
    [string]$OutDir = '',
    [string]$Preset = 'mingw-release',
    [string]$Version = ''
)
$ErrorActionPreference = 'Stop'

$root     = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$qtBin    = 'C:/Qt/6.11.1/mingw_64/bin'
$mingwBin = 'C:/Qt/Tools/mingw1310_64/bin'
$msysBin  = 'C:/msys64/mingw64/bin'
$msysLic  = 'C:/msys64/mingw64/share/licenses'
$cmake    = 'C:/Program Files/CMake/bin/cmake.exe'
$nvcBuild = Join-Path $root 'kernel/nvc/build'
if (-not $OutDir) { $OutDir = Join-Path $root 'build/package' }

$version = $Version
if (-not $version) {
    $version = ([regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(lvdsim\s+VERSION\s+([0-9.]+)')).Groups[1].Value
}
if (-not $version) { throw 'version not found: pass -Version or fix project(lvdsim VERSION ...) in CMakeLists.txt' }
$name    = "lvdsim-$version-win64"
$stage   = Join-Path $OutDir $name
$zip     = Join-Path $OutDir "$name.zip"

if (Test-Path $stage) { throw "$stage already exists. Remove it yourself or pass another -OutDir (this script never deletes)." }
if ((Test-Path $zip) -and -not $NoZip) { throw "$zip already exists. Remove it yourself or pass another -OutDir." }

$env:PATH = "$mingwBin;C:/Qt/Tools/Ninja;C:/Program Files/CMake/bin;$env:PATH"

Set-Location $root   # cmake presets are read from the current directory
if (-not $SkipBuild) {
    & $cmake --preset $Preset;           if ($LASTEXITCODE) { throw 'configure failed' }
    & $cmake --build --preset $Preset;   if ($LASTEXITCODE) { throw 'build failed' }
}
$bin = Join-Path $root "build/$Preset/bin"
foreach ($f in 'lvdsim-gui.exe','lvdsim-cli.exe','vlib.exe','vmap.exe','vcom.exe','vlog.exe','vsim.exe') {
    if (-not (Test-Path (Join-Path $bin $f))) { throw "missing $f in $bin" }
}
$nvcExe = Join-Path $nvcBuild 'bin/nvc.exe'
if (-not (Test-Path $nvcExe)) { throw "nvc.exe not built: run kernel/build/build-nvc-msys2.sh" }

New-Item -ItemType Directory -Force -Path $stage | Out-Null

# 1. Our programs. The five command names are copies of the multi-call lvdsim-cli.
foreach ($f in 'lvdsim-gui.exe','lvdsim-cli.exe','vlib.exe','vmap.exe','vcom.exe','vlog.exe','vsim.exe') {
    Copy-Item (Join-Path $bin $f) $stage
}

# 2. Qt runtime (dynamic linking, LGPL) and the MinGW runtime.
& "$qtBin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    --compiler-runtime (Join-Path $stage 'lvdsim-gui.exe')
if ($LASTEXITCODE) { throw 'windeployqt failed' }
# Headless platform plugin: lets the packaged GUI run with QT_QPA_PLATFORM=offscreen (screenshots, CI).
Copy-Item "$qtBin/../plugins/platforms/qoffscreen.dll" (Join-Path $stage 'platforms')
foreach ($d in 'libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll') {
    if (-not (Test-Path (Join-Path $stage $d))) { Copy-Item "$mingwBin/$d" $stage }
}

# 3. nvc with its own DLL closure (MSYS2 runtime), kept apart from the Qt MinGW runtime.
$nvcDir = Join-Path $stage 'nvc'
New-Item -ItemType Directory -Force -Path (Join-Path $nvcDir 'bin'), (Join-Path $nvcDir 'lib') | Out-Null
Copy-Item $nvcExe (Join-Path $nvcDir 'bin')
$objdump = "$msysBin/objdump.exe"
$seen = @{}; $queue = New-Object System.Collections.Queue
$queue.Enqueue($nvcExe)
while ($queue.Count) {
    $item = $queue.Dequeue()
    foreach ($line in (& $objdump -p $item | Select-String 'DLL Name:')) {
        $dll = ($line.ToString() -split 'DLL Name:')[1].Trim()
        $key = $dll.ToLower()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        $src = Join-Path $msysBin $dll
        if (Test-Path $src) {
            Copy-Item $src (Join-Path $nvcDir 'bin')
            $queue.Enqueue($src)
        }   # otherwise a Windows system DLL
    }
}
# Tcl needs its script library when nvc starts the shell (found as <exe dir>/../lib/tcl8.6).
foreach ($t in (Get-ChildItem "$msysBin/../lib" -Directory -Filter 'tcl8*')) {
    Copy-Item $t.FullName (Join-Path $nvcDir 'lib') -Recurse
}
# nvc's own libraries: no test plugins, no static archives.
foreach ($l in 'std','std.08','std.19','ieee','ieee.08','ieee.19','nvc','nvc.08','nvc.19','vital','vital.08','vital.19',
               'synopsys','synopsys.08','synopsys.19') {
    $p = Join-Path $nvcBuild "lib/$l"
    if (Test-Path $p) { Copy-Item $p (Join-Path $nvcDir 'lib') -Recurse }
}
if (Test-Path (Join-Path $nvcBuild 'lib/siginit.dll')) { Copy-Item (Join-Path $nvcBuild 'lib/siginit.dll') (Join-Path $nvcDir 'lib') }

# 4. Command compatibility package.
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'tcl/compat') | Out-Null
Copy-Item (Join-Path $root 'tcl/compat/lvdcompat.tcl') (Join-Path $stage 'tcl/compat')

# 4b. Library compile scripts (Tools > Compile libraries in the GUI runs them; also usable by hand). No vendor sources inside.
$libsDir = Join-Path $stage 'scripts/libs'
New-Item -ItemType Directory -Force -Path $libsDir | Out-Null
foreach ($f in 'compile_libs.bat','compile_libs.sh') {
    Copy-Item (Join-Path $root "scripts/libs/$f") $libsDir
}

# 5. Licences.
$lic = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force -Path $lic | Out-Null
Copy-Item (Join-Path $root 'packaging/licenses/*') $lic -Recurse
Copy-Item (Join-Path $root 'kernel/nvc/COPYING') (Join-Path $lic 'NVC-GPL-3.0.txt')
Copy-Item (Join-Path $root 'third_party/fstapi/LICENSE') (Join-Path $lic 'fstapi-MIT.txt')
$thirdParty = Join-Path $lic 'third-party'
New-Item -ItemType Directory -Force -Path $thirdParty | Out-Null
foreach ($pkg in 'libffi','llvm-libs','readline','zlib','zstd','gcc-libs','winpthreads','ncurses','termcap','libiconv','expat','libxml2') {
    if (Test-Path "$msysLic/$pkg") { Copy-Item "$msysLic/$pkg" $thirdParty -Recurse }
}
Copy-Item (Join-Path $root 'packaging/README-package.txt') (Join-Path $stage 'README.txt')

# 6. Zip (cmake writes forward-slash entries, unlike Compress-Archive in Windows PowerShell 5.1).
if (-not $NoZip) {
    Push-Location $OutDir
    & $cmake -E tar cf "$name.zip" --format=zip $name
    $rc = $LASTEXITCODE
    Pop-Location
    if ($rc) { throw 'zip failed' }
}

# 7. Installer if Inno Setup is installed.
$iscc = @('C:/Program Files/Inno Setup 7/ISCC.exe','C:/Program Files (x86)/Inno Setup 6/ISCC.exe') | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc -and -not $NoInstaller) {
    & $iscc "/DAppVersion=$version" "/DSourceDir=$stage" "/DOutDir=$OutDir" "/DOutName=$name-setup" (Join-Path $root 'packaging/lvdsim.iss')
    if ($LASTEXITCODE) { throw 'ISCC failed' }
}

$size = '{0:N1} MB' -f ((Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "folder: $stage ($size)"
if (-not $NoZip) { Write-Host ("zip:    $zip ({0:N1} MB)" -f ((Get-Item $zip).Length / 1MB)) }
