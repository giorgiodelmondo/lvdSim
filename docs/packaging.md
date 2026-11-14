# Packaging

Two reproducible packages, both self contained: no environment variables are needed. The tools find nvc,
its libraries and the Tcl package relative to their own executable.

First steps with a package (unzip, PATH, first design, GUI tour): [../README.md](../README.md) and [tutorial.md](tutorial.md).

## Layout

```
lvdsim-<ver>-win64/                 (Linux: lvdsim-<ver>-linux-x86_64/, programs in bin/)
  lvdsim-gui.exe lvdsim-cli.exe vlib.exe vmap.exe vcom.exe vlog.exe vsim.exe
  Qt6*.dll platforms/ styles/ imageformats/ ...   windeployqt output (+ platforms/qoffscreen.dll for headless runs)
  libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll   MinGW 13.1 runtime of our programs
  nvc/bin/nvc.exe + MSYS2 DLLs                    kept apart from the Qt MinGW runtime, the two do not mix
  nvc/lib/{std,ieee,nvc}[.08|.19] siginit.dll tcl8.6 tcl8   VHDL libraries and the Tcl script library nvc needs
  tcl/compat/lvdcompat.tcl
  scripts/libs/compile_libs.bat + compile_libs.sh   Tools > Compile libraries runs these (Linux: next to bin/, found one level up)
  licenses/  README.txt
  portable.txt                                    only in the portable zip (see Portable mode below)
```

Discovery (`findBundledNvc`, `tools/src/environment.cpp`, unit tests in `tools/tests/test_environment.cpp`):
`LVDSIM_NVC`, then `<program dir>/nvc/bin/nvc` (or `<program dir>/../nvc/...`, the Linux layout), then next to the program,
then `PATH`. With a bundled nvc, `-L <nvc>/lib` and the DLL directory `<nvc>/bin` are added unless `LVDSIM_NVC_LIBDIR` and
`LVDSIM_NVC_DLLDIR` say otherwise. `lvdcompat.tcl` was already found as `<dir>/tcl/compat` or `<dir>/../tcl/compat`.
nvc 1.24 builds no `vital` or `synopsys` library directories (VITAL is inside `ieee`); the scripts copy them if they appear.

The GUI finds the library scripts as `<program dir>/scripts/libs`, `<program dir>/../scripts/libs`, then in every parent directory up to
eight levels (the source tree during development). No Xilinx or Intel sources are in the package; the scripts read the user's own
installation. `package-windows.ps1` copies `compile_libs.bat` and `.sh`, `package-linux.sh` the same (the installer file list is the
package folder, so `scripts/libs` is included without a change of `packaging/lvdsim.iss`).

## Windows

Prerequisite: the kernel built with `kernel/build/build-nvc-msys2.sh` (needs `kernel/nvc/build/bin/nvc.exe` and `lib/`).

    powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1

Options: `-SkipBuild` (reuse `build/<preset>`), `-NoInstaller`, `-NoZip`, `-OutDir <dir>`, `-Preset <name>` (default `mingw-release`), `-Version <x.y.z>`
(default from `CMakeLists.txt`). For a complete release (installer, portable zip, checksums, checks) use [`scripts/make-windows-release.ps1`](#making-a-release), which calls this script. The script configures and builds the
`mingw-release` preset, runs `windeployqt --release --no-translations`, finds the DLLs of `nvc.exe` by walking
`objdump -p` through `C:/msys64/mingw64/bin`, assembles the folder and writes the zip with `cmake -E tar --format=zip`
(forward slash entries). It never deletes: it stops if the output names exist, so move `build/package` aside first.

Outputs (0.1.0): `build/package/lvdsim-0.1.0-win64/` 215 MB; `lvdsim-0.1.0-win64.zip` 73 MB; `lvdsim-0.1.0-win64-setup.exe`
52 MB. 146 MB of the folder is `libLLVM-23.dll` (nvc's JIT).

### Installer (`packaging/lvdsim.iss`)

Inno Setup 7 is installed (`C:/Program Files/Inno Setup 7/ISCC.exe`; Inno Setup 6 also works). `scripts/package-windows.ps1`
compiles the script after building the folder, passing `/DAppVersion`, `/DSourceDir` (the package folder), `/DOutDir` and
`/DOutName`. By hand, from PowerShell:

    & 'C:\Program Files\Inno Setup 7\ISCC.exe' /DAppVersion=0.1.0 /DSourceDir=D:\D\lvdsim\build\package-final\lvdsim-0.1.0-win64 `
        /DOutDir=D:/tmp/some-new-dir /DOutName=lvdsim-0.1.0-win64-setup packaging\lvdsim.iss

What the installer changes on the machine:

- Files: the whole package folder is copied to the install directory, default `%LOCALAPPDATA%\Programs\lvdsim` (per user, no
  administrator rights) or `C:\Program Files\lvdsim` when the user picks "all users" in the privileges dialog
  (`PrivilegesRequired=lowest`, `PrivilegesRequiredOverridesAllowed=dialog`). The GPL text is the licence page.
- Start Menu group `lvdsim`: lvdsim (the GUI), lvdsim README, Licenses, Uninstall lvdsim.
- Task "Add lvdsim to the PATH (vlib, vmap, vcom, vlog, vsim and lvdsim-cli)", **checked by default** and can be unticked on the
  Select Additional Tasks page. It appends the install directory (not `nvc\bin`) to PATH: `HKCU\Environment\Path` for a per-user
  install, `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment\Path` for an all-users install. Details:
  the check is case-insensitive and ignores trailing backslashes, slashes and quotes, so reinstalling never adds a second entry
  and `C:\Program Files\lvdsimX` is not mistaken for `C:\Program Files\lvdsim`; the old value is read raw and written back
  unchanged (`%SystemRoot%` style references are neither expanded nor shortened), as REG_EXPAND_SZ; a missing or empty `Path`
  becomes just the install directory. A marker value `PathEntryAdded` under `Software\lvdsim` (same hive) records that the
  installer added the entry. `ChangesEnvironment=yes` makes Setup broadcast the change.
- Task "Create a desktop icon for lvdsim", unchecked by default.
- Uninstall entry: Apps and features / Programs and Features, `lvdsim 0.1.0` (per user under HKCU, all users under HKLM).
- Uninstall removes the files and shortcuts and, only if the marker exists, every entry equal to the install directory from PATH
  in the same hive (all other entries are kept byte for byte), deletes the marker, and broadcasts the change again.
- Silent installs: `/SILENT /TASKS="addtopath"` or `/MERGETASKS="!addtopath"` to opt out, `/ALLUSERS` or `/CURRENTUSER` for the mode.

The PATH change takes effect only in terminals (and programs) started after the installer finished; windows that were already
open keep the old environment. Only the install directory is on PATH. `nvc\bin` must not be: nvc uses the MSYS2 runtime
DLLs, which would clash with the Qt MinGW runtime of our programs. `vlib`, `vcom`, `vlog`, `vsim` and `lvdsim-cli` find their
nvc relative to their own executable (`findBundledNvc`, see Discovery above), so PATH is only needed to type their names.

Tested: compiled with ISCC 7 without warnings or errors (output `D:/tmp/lvdsim_inst_chk1/lvdsim-0.1.0-win64-setup.exe`,
54.6 MB). The pure string functions (`PathEntryKey`, `PathContains`, `PathAppend`, `PathRemove`) were checked with a line by line
Python mirror (unittest: empty value, other case, trailing backslash, quoted entry, entry in the middle, first, last and only,
similar prefix, child directory, trailing semicolon, duplicates, `%VAR%` entries untouched, append-remove round trip). The
installer itself was never run or uninstalled on the development machine (it would change the user's PATH), so the registry
writes, the broadcast and the uninstall step are untested.

### Verification done (Windows)

The zip was unpacked into a fresh directory under `D:/tmp`, with `LVDSIM_*` and `LVDSIM_INI` cleared and `PATH` reduced to
`C:\Windows\System32;C:\Windows` (no MinGW, MSYS2 or Qt directory):

- `vlib`, `vlog`, `vlog -sv`, `vcom -2008` on `examples/mixed_language/src`, then
  `vsim -c -do "run 2 us; puts [list events [examine /events] avg [examine /avg]]; quit" -lvdwave mixed.fst work.tb_mixed`
  printed `events {"00000010"} avg {"00101110"}`; the FST was written.
- `lvdsim-gui work.tb_mixed --send "run 2 us" --screenshot gui.png` with `QT_QPA_PLATFORM=offscreen`
  and `QT_QPA_FONTDIR=C:\Windows\Fonts` produced a correct screenshot (Sim, Library, Objects, Transcript, Processes, Source panes).
- Real `windows` platform start: after 6 seconds the process was alive with a main window
  (title `lvdsim - work.tb_mixed`, valid window handle).

Found while verifying: the offscreen plugin is not deployed by windeployqt, so the first package could not run headless
(exit code 143, no output); the script now copies `qoffscreen.dll`.

### Final rebuild (kernel patches 0014 to 0016)

`scripts/package-windows.ps1 -OutDir D:/D/lvdsim/build/package-final` (no `-SkipBuild`, with installer; run with `-ExecutionPolicy Bypass` for that process only) rebuilt the `mingw-release` preset and packaged
nvc with patches 0001 to 0016, the transcript timestamp links, the plot event arrows, `tcl/compat/lvdcompat.tcl` and the `lvdsim.ini` names. Sizes: folder 215.1 MB,
`lvdsim-0.1.0-win64.zip` 73.2 MB (76744993 bytes), `lvdsim-0.1.0-win64-setup.exe` 52.0 MB (54567970 bytes; compiled, not run). Linux: see the Linux section.

Verified from a fresh unzip (`D:/tmp/pkgfinal_u1`) with `env -i` (only TEMP, TMP, SystemRoot and `PATH=/usr/bin:/c/Windows/System32`):

- `vlib`, `vlog`, `vlog -sv`, `vcom -2008` on `examples/mixed_language/src`, then `vsim -c -do "run 2 us; puts done; quit" work.tb_mixed` 10 times: all exit status 0, 99 to 123 ms each, no hang.
  The result line `events {"00000010"} avg {"00101110"}` appeared in all 10 runs of a variant that prints it.
- `lvdsim-gui --settings fresh.ini -do "add wave -r /*; run 3 us" work.tb_reports --cursor 100000000 --cursor 500000000 --screenshot ...` on `examples/reports/src`
  (offscreen platform): the screenshot shows the coloured event arrows in the band under the ruler (cyan and magenta triangles) and underlined, coloured time links in the transcript
  (`1500ns+0`, `2us+0`). Use `--settings` with a fresh file: without it the window size saved by an earlier run in the user settings was used and the wave pane was squeezed to nothing.
- Real `windows` platform start (PATH reduced to the system directories): after 6 seconds the process was alive with a main window titled `lvdsim - work.tb_reports`; stopped by PID, no nvc left behind.
- Linux tarball (built with `NVC_BUILD=~/build/nvc-linux16`, `OUT_DIR=~/build/lvdsim/package-final`, no new apt packages): unpacked into `~/tmp/pkgfinal-lin1`, run with `env -i PATH=/usr/bin:/bin`, the mixed_language flow
  printed `events {"00000010"} avg {"00101110"}` 10 times, exit status 0.

### Fixed: nvc did not exit after `quit` (kernel patch 0014)

`quit` or `exit` after a longer `run` (mixed_language: 2 us and above) hung when the environment was clean (PATH with only the system
directories). Cause, from a gdb attach: the main thread was in `join_worker_threads` (`src/thread.c`), called from the atexit table
while msvcrt `exit()` holds its exit lock. A JIT worker was still inside an LLVM compile (tier-up of a hot function started by `run`)
and called `atexit` (blocked on a critical section in msvcrt `.data`, owner = the main thread). The join therefore never returned.
Whether a compile is still in flight at `quit` depends on timing, which is why other runs seemed fine. `NVC_MAX_THREADS=1` hides it
because there are no workers. Fix: `shell_cmd_exit` calls `async_barrier()` before `Tcl_Exit` so pending compiles finish first
(patch 0014 in `kernel/PATCHES.md`). Verified: 20 of 20 clean-environment runs exit, with and without `NVC_MAX_THREADS`; stress example
(118 lanes, 20 ms) 90.0 s against 91.3 s before. Linux does not show it (POSIX `exit` does not hold a lock across atexit handlers).

## Linux

Two ways to package, both from the `linux-release` preset (WSL Debian 13, GCC 14, Qt 6.8.2 from apt):

1. `scripts/package-linux.sh`: a small tarball that uses the distribution's Qt, LLVM and Tcl (apt line below). Kept for CI-style checks.
2. `scripts/package-linux-appdir.sh`: **a self-contained bundle and a shell installer**. This is the one to give to users.

### Bundle and installer (`scripts/package-linux-appdir.sh`)

    wsl -d Debian -- bash -lc "cd /mnt/d/D/lvdsim && bash scripts/package-linux-appdir.sh --skip-build"

Options: `--skip-build` (reuse `~/build/lvdsim/linux-release`), `--no-installer` (bundle only). For a complete release (checks, checksums, portable tarball) use [`scripts/make-linux-run.sh`](#making-a-release), which calls this script. Environment: `NVC_BUILD` (default
`~/build/nvc-linux16`; **must be a build of the branch `lvdsim` of the NVC fork**: an older build without the shell `exit` fix crashes in LLVM at `quit`, about
one run in three), `OUT_DIR` (default `~/build/lvdsim/appdir`), `COMPRESS=gz` (default xz), `SOURCE_DATE_EPOCH`. Nothing is
downloaded (no appimagetool, no makeself) and nothing is deleted: it stops if the output names exist. Needs `patchelf`, `strip`,
`ldd`, `objdump`, `dpkg`, `xz`.

Outputs (0.1.0): `lvdsim-0.1.0-linux-x86_64/` (276 MB, 113 shared libraries; 124 MB of it is `libLLVM.so.19.1`, nvc's JIT) and
`lvdsim-0.1.0-linux-x86_64.run` (68 MiB, tar.xz payload).

Layout of the bundle:

```
AppRun                       launcher (see below)         lvdsim.desktop  lvdsim.png (icon, generated by packaging/make-icon.py)
bin/                         lvdsim-gui lvdsim-cli vlib vmap vcom vlog vsim   qt.conf (Plugins = ../plugins)
nvc/bin/nvc  nvc/lib/        std ieee nvc (+ .08 .19) siginit.so tcl8.6 (the Tcl script library Tcl_Init finds as <exe>/../lib/tcl8.6)
tcl/compat/lvdcompat.tcl     scripts/libs/compile_libs.{sh,bat}
lib/                         every shared library the programs need (below)
plugins/                     Qt plugins: platforms (xcb offscreen minimal wayland-egl wayland-generic), platforminputcontexts, imageformats,
                             iconengines, xcbglintegrations, tls, and the wayland shell/graphics/decoration plugins
licenses/                    lvdsim GPL, NVC GPL, fstapi MIT, Qt LGPL, NOTICE-bundled-libraries.txt, MIN-GLIBC.txt, third-party/<package>-copyright
README.txt
```

What is bundled: the closure of `ldd` over `lvdsim-gui`, `lvdsim-cli`, `nvc` and every bundled Qt plugin (collected until no new
library appears; an unresolved dependency stops the script), minus the exclude list. That is Qt 6 Core, Gui, Widgets, Network, Svg,
DBus, OpenGL, Wayland client, XcbQpa and their dependencies (ICU 76, pcre2, double-conversion, md4c, harfbuzz, freetype, fontconfig,
libpng, libjpeg, glib, dbus, libxcb family, libX11, libxkbcommon, wayland client, openssl, gnutls and curl pulled in by Qt Network),
LLVM 19, z3, libxml2, libffi, libtcl8.6, readline, libedit, libtinfo, zlib, zstd, libstdc++ and libgcc_s. **Not bundled, taken from
the host**: glibc and its helpers (libc, libm, libdl, libpthread, librt, libutil, libresolv, ld-linux), libGL, libGLX, libGLdispatch,
libEGL, libOpenGL, libGLESv2, libgbm, libdrm*, libvulkan (they must match the GPU driver), libudev and libsystemd (host services; also
keeps the glibc requirement at 2.38), and the GTK3 platform theme plugin (it would drag in GTK). Fonts and the fontconfig
configuration come from the host.

RPATH: every ELF file gets a `DT_RPATH` relative to itself (`$ORIGIN/../lib` for `bin/*`, `$ORIGIN/../../lib` for `nvc/bin/nvc` and the
plugins, `$ORIGIN` for `lib/*`), set with `patchelf --force-rpath`, and all files are stripped. The programs therefore also run without
AppRun, and a `LD_LIBRARY_PATH` of the user cannot replace a bundled library (RPATH is searched first).

`AppRun`: no arguments starts `lvdsim-gui`; `AppRun --run <tool> args` runs one of `lvdsim-gui lvdsim-cli vlib vmap vcom vlog vsim`; a
symlink with a tool name (`vcom -> .../AppRun`) dispatches on its own name. For the GUI it prepends `lib/` to `LD_LIBRARY_PATH`, sets
`QT_PLUGIN_PATH` and unsets `QT_QPA_PLATFORMTHEME`; the command line tools are started without any environment change.
`XDG_DATA_DIRS` is not touched (the bundle needs no data files from the host).

**Minimum glibc: 2.38** (Ubuntu 24.04, Debian 13, Fedora 39 or newer). The script lists it in `licenses/MIN-GLIBC.txt` from
`objdump -T` over every ELF file: the highest `GLIBC_` symbol version needed is 2.38 (libstdc++ and libLLVM from Debian 13, built with
glibc 2.41). Older systems (Debian 12, Ubuntu 22.04, RHEL 9) will not run it. The bundle is x86_64 only.

Licence notices: `licenses/NOTICE-bundled-libraries.txt` has one row per bundled file with the Debian package (`dpkg -S`) and a licence
summary taken from the `License:` fields of `/usr/share/doc/<package>/copyright` (this lists every licence of the package, including
build scripts, so it is a superset); the full copyright file of each package is copied to `licenses/third-party/`. Source of the Debian
packages: `apt source <package>`. Qt is LGPL-3.0, linked dynamically, unmodified and replaceable.

### Installer (`lvdsim-<ver>-linux-x86_64.run`)

A POSIX sh stub (`packaging/linux/installer-stub.sh`) followed by the line `__PAYLOAD_BELOW__` and a tar.xz of the bundle (tar.gz when
the build machine has no xz, or with `COMPRESS=gz`; the installing machine needs `xz` for an xz payload). The stub carries the payload
size and SHA-256; the checksum is verified before anything is extracted or installed.

    sh lvdsim-0.1.0-linux-x86_64.run [options]

| Option | Effect |
| --- | --- |
| `--prefix DIR` | install directory; default `~/.local/opt/lvdsim`, `/opt/lvdsim` as root |
| `--yes` | do not ask (without a terminal on stdin it never asks) |
| `--no-symlinks` | no command symlinks |
| `--no-desktop` | no `.desktop` entry and icon |
| `--no-path` | accepted, and the default: shell startup files are not touched |
| `--add-path` | only on request: append a block marked `# >>> lvdsim >>>` ... `# <<< lvdsim <<<` to `~/.profile` and `~/.bashrc` |
| `--uninstall` | remove exactly what the installer created, read from `<prefix>/.lvdsim-install-manifest` (needs `--prefix` if not the default) |
| `--extract DIR` | unpack into an empty or new directory and stop |
| `--list`, `--check`, `--license`, `--version`, `--help` | file list, checksum only, GPL text, version, usage |

Default install (user): the bundle in the prefix; symlinks `lvdsim-gui lvdsim-cli vlib vmap vcom vlog vsim` in `~/.local/bin` (as root
`/usr/local/bin`), all pointing to `<prefix>/AppRun`; `lvdsim.desktop` (Exec generated for the prefix) in
`~/.local/share/applications` and the 256x256 icon in `~/.local/share/icons/hicolor/256x256/apps` (as root under `/usr/share`). `PATH` is
not edited: when `~/.local/bin` is not on it the installer prints the `export PATH=...` line to add (or use `--add-path`). The licence
summary (GPL-3.0-or-later) is printed at the start of every install. An existing lvdsim install in the prefix is removed through its
manifest and replaced; an existing directory that is not empty and has no manifest is refused (nothing is changed); an existing file
at a symlink, `.desktop` or icon path is never overwritten (skipped with a warning). The manifest records every file, symlink, PATH
block and directory the installer created (parents of the prefix and `~/.local/...` directories included, removed again when empty
and created by the installer); a failed install rolls back through the same manifest.

### Linux GUI build

`cmake --preset linux-release -DLVDSIM_BUILD_GUI=ON` (Debian 13: `qt6-base-dev qt6-base-private-dev qt6-shadertools-dev qt6-svg-dev
qt6-tools-dev`). Two changes were needed for Qt 6.8: `gui/rhi/rhi.cmake` no longer requires a separate `Qt6GuiPrivate` package (in 6.8 on
Debian `Qt6::GuiPrivate` comes with `Qt6Gui`; the separate package is looked up only if the target is missing), and
`gui/rhi/gpu_wave_renderer.cpp` uses `QImage::mirrored(false, true)` instead of `flipped(Qt::Vertical)` (6.9 or newer). The Linux code
paths of `gui/app/compile_libs_dialog.cpp` (`setsid`, process-group `SIGTERM`) compiled without change. `ctest --preset linux-release`
(with `QT_QPA_PLATFORM=offscreen`, `LVDSIM_NVC` and `LVDSIM_NVC_LIBDIR` set): 141 of 141 pass. `test_gpu_renderer` skips its cases itself
("headless GPU rendering is only set up for Direct3D 11 and Metal"), which counts as passed.

### Verification done (Linux bundle and installer)

All in WSL Debian 13 (the only Linux available), installer built with `--skip-build` from the final GUI build:

- Isolation: `env -i PATH=/usr/bin:/bin LD_LIBRARY_PATH= ldd` on all 149 ELF files of the bundle: nothing is "not found", and the only
  libraries that resolve outside the bundle are `ld-linux-x86-64.so.2`, `libc.so.6`, `libm.so.6`, `libresolv.so.2` (glibc), `libEGL.so.1`,
  `libGLX.so.0`, `libGLdispatch.so.0`, `libOpenGL.so.0` (GL stack, needed by Qt Gui and the RHI plugins) and `libsystemd.so.0` (needed by the
  bundled libdbus). All are on the exclude list.
- Mixed-language flow with the installed symlinks from `env -i PATH=<bindir>:/usr/bin:/bin` (vlib, vlog, vlog -sv, vcom -2008,
  `vsim -c ... quit`): the last line was `events {"00000010"} avg {"00101110"}`, exit status 0, in 10 of 10 runs (extracted bundle) and 5 of 5
  (installed), no hang. With an nvc build lacking patch 0014 the same flow crashed in LLVM at `quit` in 3 of 8 runs; see the
  `NVC_BUILD` note above.
- GUI headless (`AppRun work.tb_mixed -do "source examples/mixed_language/sim.do; run 2 us" --screenshot ...`, `QT_QPA_PLATFORM=offscreen`,
  with and without `QT_QPA_FONTDIR`): the screenshot shows the Sim, Library, Objects, Transcript, Processes and Source panes, text in the
  host's fonts, the analog trace, buses and the event rows. Real start in WSLg: `QT_QPA_PLATFORM=xcb` and `wayland` both stayed alive for 6 s
  (stopped by PID).
- Installer in a throwaway `HOME` with a fresh prefix: symlinks, desktop file and icon created and working, re-install over the existing
  prefix (replace), a non-empty foreign directory refused, `--uninstall` removed everything including created directories (a listing of the
  whole `HOME` before and after is identical). `--add-path` appended the block to an existing `.profile` and created `.bashrc`; `--uninstall` restored
  `.profile` byte for byte and removed the new `.bashrc`. `--no-symlinks --no-desktop` touched nothing in `HOME`.
- Corruption: one flipped byte in the payload: `--check` and the install both stop with "checksum mismatch" and create nothing; a truncated
  file stops with "payload marker not found".
- Not tested: running as root (`/opt`, `/usr/local/bin`, `/usr/share`), other distributions or older glibc, a real GPU (the QRhi renderer
  and its fallback were not exercised on Linux), `update-desktop-database` (not called), a real desktop launching the `.desktop` entry.

### Tarball (`scripts/package-linux.sh`)

    wsl -d Debian -- bash -lc "cd /mnt/d/D/lvdsim && bash scripts/package-linux.sh"      (add --skip-build to reuse the build)

The script builds preset `linux-release` in `~/build/lvdsim/linux-release`, copies `~/build/nvc-linux` (override with `NVC_BUILD`),
and writes `~/build/lvdsim/package/lvdsim-0.1.0-linux-x86_64{,.tar.gz}` (8 MB folder, 5.3 MB tarball; fixed mtime and owner,
override the time with `SOURCE_DATE_EPOCH`). Programs are stripped. nvc uses the distribution's libraries:

    sudo apt install tcl8.6 libllvm19 libffi8 libzstd1 libreadline8

The GUI is built when the Qt dev packages (`qt6-base-dev qt6-base-private-dev qt6-shadertools-dev qt6-svg-dev`) are installed, and then
uses the system Qt 6 (run-time packages `libqt6widgets6 libqt6gui6 libqt6core6 libqt6svg6 qt6-qpa-plugins`). The tarball of 2026-11-07
(before the dev packages were installed) has no `lvdsim-gui`; the bundle above always has it.

Verified: the tarball was unpacked into `~/tmp/pkgtest-lin1`, run with `env -i PATH=/usr/bin:/bin`, and the mixed_language batch flow
(vlib, vlog, vlog -sv, vcom, vsim -c) printed `events {"00000010"} avg {"00101110"}`, exit status 0.

Fixed on the way: `libwave/bench/bench_wave.cpp` had a stray `std::max<...>(...)` statement (ignored `nodiscard`) that GCC 14
rejects with `-Werror` in the release preset; it is removed.

### Rebuild r2 (2026-11-07: GPU renderer as default, example renamed to mixed_language)

All packages were regenerated from the current tree into fresh directories.

| Package | Location (not in the repository tree beyond `build/`) | Size |
| --- | --- | --- |
| Windows folder | `build/package-r2/lvdsim-0.1.0-win64/` | 215.5 MB |
| Windows zip | `build/package-r2/lvdsim-0.1.0-win64.zip` | 73.3 MB (76 873 864 bytes) |
| Windows installer | `build/package-r2/lvdsim-0.1.0-win64-setup.exe` | 54.7 MB (compiled with Inno Setup 7.1.0 without warnings, not run) |
| Linux bundle | WSL `~/build/lvdsim/appdir-r2/lvdsim-0.1.0-linux-x86_64/` | 276 MB, 149 ELF files |
| Linux installer | WSL `~/build/lvdsim/appdir-r2/lvdsim-0.1.0-linux-x86_64.run` | 69 MB, SHA-256 `1f6a1864801d49934de0fbf86382b19a9bdde74db2b8c1304fd59756c9dfb8d5` |
| Linux tarball (older script) | WSL `~/build/lvdsim/package-r2b/lvdsim-0.1.0-linux-x86_64.tar.gz` | 5.9 MB (9.2 MB folder, uses system libraries) |

Windows, verified: dependency scan (`depcheck.py`, 41 PE files): every import resolves inside the package folder or Windows (Qt plugins in
subfolders and `nvc\lib\siginit.dll` importing `nvc.exe` are expected). The zip was unpacked into a fresh folder; with `env -i` (only
`TEMP`, `TMP`, `SystemRoot` and `PATH=/usr/bin:/c/Windows/System32`) the package's `vlib`, `vlog`, `vlog -sv`, `vcom -2008` and
`vsim -c ... quit` ran the mixed_language flow 10 times: all printed `events {"00000010"} avg {"00101110"}`, no hang, 330 to 380 ms per
flow (first run 0.9 s). `lvdsim-gui` headless (offscreen, fresh settings) shows the full window with `Renderer: QPainter (GPU not
available: platform offscreen)`; started on the real windows platform it stays alive and its own screenshot shows `Renderer: GPU`. The
package holds `scripts/libs`, `tcl/compat/lvdcompat.tcl`, `licenses/` (including `NVC-GPL-3.0.txt` and `NOTICE-third-party.txt`) and
`README.txt`.

Linux, verified in WSL Debian 13 (build with `NVC_BUILD=~/build/nvc-linux16`, patches 0001 to 0016): isolation (`env -i`, empty
`LD_LIBRARY_PATH`, `ldd` on all 149 ELF files): nothing not found, only glibc and the GL stack (`libEGL`, `libGLX`, `libGLdispatch`,
`libOpenGL`) plus `libsystemd` come from the system, all on the exclude list; the mixed_language flow 5 of 5 from the bundle and 5 of 5
with the installed symlinks, exit status 0; GUI headless screenshot (offscreen, with and without `QT_QPA_FONTDIR`) shows the full window;
`xcb` and `wayland` under WSLg both stayed alive for 6 s; install and uninstall in a throwaway `HOME` left the listing identical, replace,
foreign directory refusal, `--add-path` (profile restored byte for byte), `--no-symlinks --no-desktop` and `--extract` behave as before; a
flipped payload byte gives "checksum mismatch" and creates nothing, a truncated file gives "payload marker not found". The tarball
unpacked and ran the flow 3 of 3 from `env -i PATH=/usr/bin:/bin`.

`scripts/package-linux.sh` (the tarball) is now mostly redundant: the bundle and `.run` installer include everything and need nothing from
the host except glibc and the graphics stack. It stays as a small alternative for machines that have the system Qt and nvc libraries
installed (5.9 MB instead of 69 MB). Fixed in this round: its check for the GUI dev files looked for a `Qt6GuiPrivate` cmake directory
that Debian 13 does not have, so a build without `--skip-build` would have configured the GUI off.

### Rebuild r3 (2026-11-07: verification from a clean state, installer really run)

Everything was rebuilt and re-verified. Tests: `ctest --preset mingw-debug` 142 of 142; Linux, a fresh configure of `linux-release` with
`-DLVDSIM_BUILD_GUI=ON` in `~/build/lvdsim-r3` (295 build steps, no warnings as errors), `ctest` with `QT_QPA_PLATFORM=offscreen`, `LVDSIM_NVC`
and `LVDSIM_NVC_LIBDIR` at `~/build/nvc-linux16`: 142 of 142. One case skips itself on Linux
(`TestWaveWidget::gpuFrameMatchesQPainterWaveArea`: "headless GPU rendering is only set up for Direct3D 11 and Metal"); none on Windows.
`scripts_libs_compile` (`scripts/libs/test_libs.sh`) ran and passed on both. The example `examples/mixed_language/run.sh --batch` (a copy outside the
repository) printed `events {"00000010"} avg {"00101110"}` on Windows (build tree) and Linux.

| Package | Location | Size | SHA-256 |
| --- | --- | --- | --- |
| Windows folder | `build/package-r3/lvdsim-0.1.0-win64/` | 215.5 MB | |
| Windows zip | `build/package-r3/lvdsim-0.1.0-win64.zip` | 76 873 864 bytes | `d12de3794b3e595fe4a3dd83ebb8024fe78a35d7786524848c80e4ea63a5a120` |
| Windows installer | `build/package-r3/lvdsim-0.1.0-win64-setup.exe` | 54 651 879 bytes | `467e8953f8f7ce4200adcb359fe906947e33de15394f5e7ae29abfbf8cbbdf6c` |
| Linux installer | WSL `~/build/lvdsim/appdir-r3/lvdsim-0.1.0-linux-x86_64.run` | 71 538 123 bytes | `57eb7b3986685b44439f3f6038a1cebd41a254f05221c0cd94966222b8deb131` |
| Linux bundle | WSL `~/build/lvdsim/appdir-r3/lvdsim-0.1.0-linux-x86_64/` | 276 MB, 149 ELF files | |
| Linux tarball | WSL `~/build/lvdsim/package-r3/lvdsim-0.1.0-linux-x86_64.tar.gz` | 6 099 639 bytes | `c82b54180132d27a79464b02fcf0b0b02f80b2986298823ba1cce14f9caa272e` |

(The 64-hex string printed by the Linux installer, `1f6a1864...`, is the SHA-256 of its inner payload, identical to r2: the bundle is reproducible.
The Windows zip has the same size as in r2.)

Windows package: dependency scan 41 PE files, only the expected cross references. From a fresh unzip with `env -i` (TEMP, TMP, SystemRoot,
`PATH=/usr/bin:/c/Windows/System32`) the mixed_language flow gave the expected values 10 of 10 times (96 to 181 ms per `vsim`). Headless GUI screenshot
complete; a start on the real windows platform with the default renderer policy stayed alive, its own screenshot shows `Renderer: GPU`; stopped by PID,
its nvc child exited; `--show-dialog compile-libs` (offscreen) shows the dialog.
Installer, per user, throwaway directories, user PATH saved first (type REG_EXPAND_SZ, 427 characters):

* Run 1 (`/TASKS="!addtopath,!desktopicon"`, `D:/tmp/inst_r3a`): 1074 files, programs run from the install directory (3 of 3 flows), PATH unchanged,
  Uninstall key `lvdsim_is1` in HKCU only (not in HKLM), Start Menu group with 4 shortcuts. Uninstall: directory gone, key gone, Start Menu group gone, PATH identical.
* Run 2 (`/TASKS="addtopath,!desktopicon"`, `D:/tmp/inst_r3b`): the user PATH ends with `;D:/tmp/inst_r3b` exactly once, `nvc/bin` is not on it, the
  marker `PathEntryAdded` was written, still REG_EXPAND_SZ. Second install on top: log line "PATH already contains ..., left unchanged", still once.
  A new process reading the user PATH from the registry resolves `where vcom` to the install directory (as the last of two hits: another `vcom.exe` of
  the user's own HDL tools sits earlier on their PATH, so the installer's append-at-the-end choice means the older tool wins; documented behaviour).
  Uninstall: PATH byte identical to the backup, type REG_EXPAND_SZ as before, directory gone, Uninstall key list identical, Start Menu list identical.
* Not done: all-users install (not allowed), the interactive wizard pages.
* Note: bash on Windows reports exit status 127 for a native nvc started with stdout redirected to `/dev/null` while stderr is a pipe (a quirk of that
  shell, reproduced with the unmodified kernel build; with a file or a pipe it works). Do not redirect stdout to `/dev/null` in test scripts of this kind.

Linux (WSL Debian 13): isolation, `env -i PATH=/usr/bin:/bin LD_LIBRARY_PATH= ldd` on all 149 ELF files: nothing "not found"; resolving outside the bundle:
`libc.so.6` (147 files), `libm.so.6` (66), `libresolv.so.2` (8), the GL stack `libEGL.so.1`, `libGLX.so.0`, `libGLdispatch.so.0`, `libOpenGL.so.0` (34 each) and
`libsystemd.so.0` (36). Highest GLIBC symbol version needed: 2.38. `--check` OK; install with `--prefix` into a fresh directory and a throwaway `HOME`,
re-install over it, the flow 5 of 5 from `env -i PATH=<symlink dir>:/usr/bin:/bin`, headless GUI screenshot complete, `--uninstall` leaves the `HOME` listing identical,
`--add-path` writes the block to `.profile` and `.bashrc` and `--uninstall` restores `.profile` byte for byte and removes the new `.bashrc`, a flipped payload byte is
refused ("checksum mismatch", nothing created), tarball unpacked and the flow 3 of 3. Not tested: root install, other distributions, a real GPU on Linux.

Upstream package (`docs/upstream-contribution.md`, `kernel/upstream/`): see the note in section 1 of the guide: `git am` of all ten folders applies on `3133c7ec4`; upstream `master`
has since moved to `851dfd445` where plain `git am` fails on `testlist.txt` and `git am -3` applies all ten.

### Rebuild r4 (2026-11-07: `$time` rounding in nvc)

Reason: kernel patch 0007 now rounds Verilog `$time` to the nearest unit (ties up) and `nvc.exe` was rebuilt, so every package was rebuilt and re-verified.
Linux packages were made with `NVC_BUILD=~/build/nvc-linux17` (a tree with patches 0001 to 0016 in their current form). A bug found on the way:
`scripts/package-windows.ps1` ran `cmake --preset` in the caller's current directory and failed when started from another folder; it now does `Set-Location $root`.

Tests: `ctest --preset mingw-debug` 142 of 142; Linux, fresh `linux-release` configure with `-DLVDSIM_BUILD_GUI=ON` in `~/build/lvdsim-r4` (295 steps),
`ctest` with `QT_QPA_PLATFORM=offscreen`, `LVDSIM_NVC` and `LVDSIM_NVC_LIBDIR` at `~/build/nvc-linux17`: 142 of 142, one self-skip
(`TestWaveWidget::gpuFrameMatchesQPainterWaveArea`, headless GPU only for Direct3D 11 and Metal); `scripts_libs_compile` passed on both.
`examples/mixed_language/run.sh --batch` (copy outside the repository) printed `events {"00000010"} avg {"00101110"}` on both.

| Package | Location | Size | SHA-256 |
| --- | --- | --- | --- |
| Windows folder | `build/package-r4/lvdsim-0.1.0-win64/` | 215.5 MB, 1072 files | |
| Windows zip | `build/package-r4/lvdsim-0.1.0-win64.zip` | 76 877 730 bytes | `0544fbebfaa6c7d1644a8be92e98c750f06a8977b95804a6b28f29b4bc1640ce` |
| Windows installer | `build/package-r4/lvdsim-0.1.0-win64-setup.exe` | 54 655 715 bytes | `7f2fa0e774838bcb4ec4e4b3d9c60a3e34c3cad385cf2891d4f6f9929b2d346f` |
| Linux installer | WSL `~/build/lvdsim/appdir-r4/lvdsim-0.1.0-linux-x86_64.run` | 71 537 931 bytes | `f43fc63848b0797a1942ce3f9695524df4b32ab1da98b697a41f9ec3ca87e233` |
| Linux bundle | WSL `~/build/lvdsim/appdir-r4/lvdsim-0.1.0-linux-x86_64/` | 276 MB, 149 ELF files | |
| Linux tarball | WSL `~/build/lvdsim/package-r4/lvdsim-0.1.0-linux-x86_64.tar.gz` | 6 099 733 bytes | `ffe628e3d58d70d2e58e6810becc98aadeb2935cee264df38aae8f68372be5f5` |

(The payload checksum printed by the Linux installer is `746f2d0ba3d39af0151b4c47ceb70b7b3e420fba965eb86b15c0363086975134`; it changed from r3 because nvc changed.)

The nvc in each package is the fresh build: Windows `nvc/bin/nvc.exe` md5 `90d12812269d725fd0b7b1e2467f487e` equals `kernel/nvc/build/bin/nvc.exe`; the Linux tarball's
`nvc` equals the stripped `~/build/nvc-linux17/bin/nvc` (md5 `a203e4fff0fbbd91f7d83ba2206efb74`); the bundle's `nvc` has identical `.text`, `.rodata` and `.data`
(it is stripped and re-linked with `patchelf`, so the file hash differs).

Rounding check with the packaged tools (Windows unzip and Linux bundle give the same eleven lines, equal to the xsim 2022.2 reference lines):
`1ns/1ps`: 0.4 ns to 0, 0.5 to 1, 16.4 to 16, 16.5 to 17, 16.6 to 17, 17.5 to 18; `10ns/1ns`: 1.5 to 2, 1.6 to 2, 2.4 to 2, 4.1 to 4, 5.6 to 6.

Windows package: dependency scan 41 PE files, only the expected cross references; from a fresh unzip with `env -i` (TEMP, TMP, SystemRoot,
`PATH=/usr/bin:/c/Windows/System32`) the mixed_language flow gave the expected values 10 of 10 times (93 to 182 ms per `vsim`); headless GUI screenshot complete;
a start on the real windows platform stayed alive (6 s, stopped by PID, the nvc child had exited) and its own screenshot shows `Renderer: GPU`;
`--show-dialog compile-libs` (offscreen) shows the dialog.

Installer, per user, throwaway directories, user PATH saved first (raw `reg query` text and `reg export`; type REG_EXPAND_SZ, 427 characters):

* Run 1 (`/TASKS="!addtopath,!desktopicon"`, `D:/tmp/inst_r4a`): 1074 files, flow from the install directory 3 of 3, PATH unchanged, Uninstall key `lvdsim_is1`
  in HKCU only (not in HKLM or WOW6432Node), Start Menu group with 4 shortcuts. Uninstall (`unins000 /VERYSILENT`): directory gone, key gone, Start Menu group gone, PATH identical.
* Run 2 (`/TASKS="addtopath,!desktopicon"`, `D:/tmp/inst_r4b`): PATH ends with `;D:\tmp\inst_r4b` exactly once, `nvc\bin` not on it, still REG_EXPAND_SZ. A second install on top:
  "PATH already contains ..., left unchanged", still once. A new process with the raw registry PATH resolves `where vcom` to two hits: the user's own HDL tools' `vcom.exe` first
  and the install directory second (the installer appends, so the older tool wins; documented behaviour). Uninstall: raw PATH value and type byte identical to the backup
  (`fc /b` of the `reg query` text and of the `reg export` files), Uninstall list and Start Menu list identical, directory gone.
* Not done: all-users install, the interactive wizard pages.

Linux (WSL Debian 13): isolation (`env -i`, empty `LD_LIBRARY_PATH`, `ldd` on 149 ELF files): nothing "not found"; resolving outside the bundle: `libc.so.6` (147), `libm.so.6` (66),
`libresolv.so.2` (8), the GL stack `libEGL`, `libGLX`, `libGLdispatch`, `libOpenGL` (34 each) and `libsystemd` (36). Highest GLIBC symbol 2.38. `--check` OK, install with
`--prefix` and a throwaway `HOME`, re-install over it, the flow 5 of 5 from the installed symlinks, headless GUI screenshot complete (fresh `--settings` file; reusing a settings
file saved by an earlier offscreen run restored a squeezed wave pane, see tutorial problem 4), `--uninstall` leaves the `HOME` listing identical, `--add-path` and its undo (`.profile`
byte for byte, `.bashrc` removed), a flipped payload byte is refused ("checksum mismatch", nothing created), tarball unpacked and the flow 3 of 3.
Not tested: root install, other distributions, a real GPU on Linux.

Upstream package: upstream `master` is still `851dfd445`; plain `git am` applies each of the ten folders on a fresh clone (06 after 05, 10 after 09); all relative links and paths in the guide,
the README and the `PR.md` files exist; no text says the standard requires rounding (the IEEE section comment inside the old patch 0015 was the known one; it is not on the fork branch).

Documentation: parse-time baking of the rounding is described in `docs/libraries.md` and `docs/tutorial.md` section 6 problem 3 (recompile Verilog libraries after updating nvc). Stale statements
that the Windows installer was never run were corrected in the README and the tutorial.

Building from source on Linux, step by step: [building-linux.md](building-linux.md).

## Making a release

One command per platform. Each builds, packages, writes checksums and verifies its own output, always into a **new** folder; nothing
is deleted or overwritten (an existing output folder stops the script).

Windows (from a normal command prompt or PowerShell, any folder; the `.bat` bypasses the execution policy for this one script only):

    scripts\make-windows-release.bat

Linux (inside WSL Debian or on a Linux host), or from Windows through WSL:

    scripts/make-linux-run.sh --portable
    scripts\make-linux-run.bat --portable --copy-to-windows

| Windows option | Meaning |
| --- | --- |
| `-Version x.y.z` | default: `project(lvdsim VERSION ...)` in the top-level `CMakeLists.txt` (the only place the version is written; the scripts, the installer and `lvd::wave::version()` read it from there) |
| `-OutDir DIR` | default `build\release\<version>-<yyyyMMdd-HHmmss>`; must not exist |
| `-Preset NAME` | CMake preset, default `mingw-release` |
| `-SkipBuild` | reuse the build tree of the preset (the kernel in `kernel/nvc/build` is always reused, never rebuilt) |
| `-NoInstaller`, `-NoPortable` | leave out the installer or the zip |
| `-Help` | usage |

| Linux option | Meaning |
| --- | --- |
| `--out DIR` | default `~/build/lvdsim/release/<version>-<yyyymmdd-HHMMSS>`; must not exist |
| `--version X.Y.Z` | as above |
| `--nvc-build DIR` | nvc build tree; default: the newest `~/build/nvc-linux*` or `~/build/nvc-fork-linux*` with `bin/nvc` and `lib/`. The version line of its `nvc` must contain the abbreviated commit of `kernel/nvc` (`git -C kernel/nvc rev-parse --short=9 HEAD`), otherwise the script stops and says how to rebuild (`kernel/build/fetch-nvc.sh` and [kernel-tests.md](kernel-tests.md)); `--force-nvc` skips the check |
| `--portable` | also write the portable tarball |
| `--skip-build`, `--compress xz\|gz`, `--jobs N` | reuse `~/build/lvdsim/linux-release`, payload compression (default xz), build parallelism |
| `--help` | usage |

`make-linux-run.bat` takes the same options (paths of `--nvc-build` and `--out` are Linux paths), runs the script in the distro `Debian`
(override with the environment variable `LVDSIM_WSL_DISTRO`) and with `--copy-to-windows [DIR]` copies the four files into `DIR` (default
`build\release\linux-<version>-<timestamp>`, must not exist) and prints the SHA-256 of the copies.

**Outputs** (the Windows scripts add `win64`, the Linux ones `linux-x86_64`):

| File | What it is |
| --- | --- |
| `lvdsim-<ver>-win64-setup.exe` | Inno Setup installer (the payload has no `portable.txt`) |
| `lvdsim-<ver>-win64-portable.zip` | the same folder plus `portable.txt` and `README-portable.txt`, top folder `lvdsim-<ver>-win64-portable` |
| `lvdsim-<ver>-linux-x86_64.run` | self-extracting installer (see [Linux](#linux)) |
| `lvdsim-<ver>-linux-x86_64-portable.tar.gz` | the bundle folder plus `portable.txt` and `README-portable.txt`, top folder `lvdsim-<ver>-linux-x86_64-portable` |
| `SHA256SUMS.txt` (Windows), `SHA256SUMS` (Linux) | `<hash>  <file>`, UTF-8 without BOM |
| `build-info.txt` | version, build date, git commit (`none` when there is no commit), nvc base commit and number of patches, Qt version, compiler |
| `stage\lvdsim-<ver>-win64\` / `lvdsim-<ver>-linux-x86_64\` | the assembled folder that was packed (215 MB / 276 MB); delete it yourself if you do not need it |

**Verified automatically before the script prints OK.** Windows: installer exists and is larger than 20 MB; the zip opens, has no backslash
entries, and contains `lvdsim-gui.exe`, `nvc\bin\nvc.exe`, `tcl\compat\lvdcompat.tcl`, `scripts\libs`, `licenses\NVC-GPL-3.0.txt`, `portable.txt`
(and as many entries as the folder has files plus the two extra ones); the installer payload folder has no `portable.txt`; a dependency scan of
every PE file with `objdump -p` from `C:\msys64\mingw64\bin` (every import is in the same folder, in Windows, or is a Qt plugin or `siginit.dll`
referring to a DLL elsewhere in the package; a missing `objdump` is only a warning); a smoke run of the packaged `vlib`, `vcom -2008` and
`vsim -c -do "run 10 ns; quit"` on a tiny VHDL file with a minimal environment (only `SystemRoot`, `TEMP`, `TMP` and the Windows directories on
`PATH`). The script also warns when `nvc.exe` is older than the newest kernel patch. Linux: `--check` and `--help` of the `.run`; the payload
checksum recomputed from the file; an isolation scan over every ELF file (each dependency resolves inside the bundle or is glibc, the GL stack,
udev or systemd); a smoke install into a throwaway prefix and `HOME` with `--no-path`, `vlib`/`vcom`/`vsim -c` through the installed symlinks, a
headless GUI start (settings must land in `HOME`), `--uninstall` and a comparison of the `HOME` listing with the initial one; with `--portable`
also an extract of the tarball, the same flow, and a headless GUI start whose settings must land in `<dir>/data` and not in `HOME`. Any
failure stops the script with a non-zero exit code and a message.

**Publishing on GitHub Releases.** Create a release for the tag, attach the four files (`setup.exe`, `portable.zip`, `.run`, `portable.tar.gz`)
and both checksum files, and paste `build-info.txt`. Say in the notes that the installers are **not code signed** (Windows SmartScreen will warn)
and that the corresponding NVC sources are the commit of the `lvdsim` branch of `https://github.com/giorgiodelmondo/nvc` named in `build-info.txt` ("nvc commit"), as the licence requires for binary distribution.

### Portable mode

If a file `portable.txt` sits next to `lvdsim-gui` (on Linux next to `AppRun`, one level above `bin/`), or the environment variable
`LVDSIM_PORTABLE` is `1` (also `true`, `yes`, `on`), the GUI keeps its state inside the program folder:

- settings: `<folder>/data/lvdsim-gui.ini` (an INI file, created on demand; nothing is written to the registry or `~/.config`);
- default output folder of **Tools > Compile libraries**: `<folder>/data/libraries`;
- temporary files stay in the system temp folder; the command line tools (`vlib`, `vcom`, `vlog`, `vsim`, `lvdsim-cli`) keep no per-user state
  (their `lvdsim.ini` is the project file in the current folder, `LVDSIM_INI` is unchanged), so they need no marker.

If the folder is not writable (read-only medium) the GUI falls back to the normal per-user location and writes one note into the transcript.
`lvdsim-gui --settings FILE` always wins. The decision is made in one place, `gui/app/portable_mode.cpp` (`resolvePortable`, unit-tested in
`gui/tests/test_portable_mode.cpp`: marker present or absent, environment values, non-writable fallback, the Linux bundle layout, the ini and
library locations, the explicit file); every `QSettings` of the GUI is opened through `openGuiSettings`. To use it by hand, unzip anywhere,
carry the folder on a USB stick, and do not run it from a read-only location.

### Verification of the first scripted release (0.1.0)

Windows, `scripts\make-windows-release.bat` (default folder, build step incremental, 58 s): installer 54 659 469 bytes (SHA-256
`4a55d8b1dc6a8c2a9806c2897ee30c936f685f6c4a388ab8188de7bcee36f36e`), portable zip 76 809 058 bytes (`1412705c1196d3b9bb43a9e2fbfece59ac9ba4ccaa992eb78381d301794b5542`),
1074 entries, dependency scan 41 PE files. The zip was unpacked into a fresh folder; `lvdsim-gui` (offscreen, `examples/mixed_language`, `sim.do`) wrote
`data\lvdsim-gui.ini`, the Compile libraries dialog showed `<folder>\data\libraries`, and `reg export HKCU\Software\lvdsim` before and after was identical.
The installer, run per user with `/VERYSILENT /TASKS="!addtopath,!desktopicon"` into a throwaway folder, had no `portable.txt`; its GUI stored the
settings in the registry and proposed `%LOCALAPPDATA%\lvdsim\libraries`; `unins000 /VERYSILENT` left the raw user `PATH` byte for byte as before.
Linux (WSL Debian 13, `nvc-linux17`, patches 0001 to 0016, autodetected): `.run` 71 554 359 bytes (`071ca5adceb8e7f066e8265a434b5e2bfa8ce4cbb4ce6e927a82081cce073be2`),
portable tarball 102 543 544 bytes (`ff26ac051f63a1d78b28374442bdacee8ce4acf1e58f8e01eaea2fa2afe479af`), 149 ELF files isolated, smoke install and uninstall
leave `HOME` identical, the portable GUI wrote `data/lvdsim-gui.ini` and left `HOME` empty, the installed GUI wrote `~/.config/lvdsim/lvdsim.conf`.
Not tested: all-users installer, signing, other distributions, root install, a real GPU on Linux, Windows 10.

### Rebuild for the fork branch (2026-11-08)

Reason: the kernel strategy changed. The single patch series (`kernel/patches`, applied by `kernel/build/fetch-nvc.sh`) is retired; `kernel/nvc` is a
clone of the branch `lvdsim` of the NVC fork `giorgiodelmondo/nvc` (commit `754af76cc`, upstream `4e41ba459` plus three change sets, `kernel/PATCHES.md`).
All four packages were rebuilt from it with the release scripts. Statements above that mention patches 0001 to 0016 or `nvc-linux16`/`nvc-linux17`
describe the earlier builds. The scripts changed accordingly: `build-info.txt` now has the kernel commit, its remote and the number of uncommitted
changes in `kernel/nvc` ("nvc commit", "nvc tree"); `make-linux-run.sh` stops when the version line of the chosen nvc build does not contain the commit of
`kernel/nvc` (tested: `nvc-linux17`, built from an archive without commit information, is refused; the new build is accepted); the Windows script
warns in the same case; the default search of `--nvc-build` also finds `~/build/nvc-fork-linux*`.

Kernel builds: Windows `kernel/nvc/build` (MSYS2, `kernel/build/build-nvc-msys2.sh`), Linux `~/build/nvc-fork-linux-s1` (fresh clone `~/build/nvc-fork-src-s1`);
`nvc --version`: `nvc 1.24-devel (754af76cc)`. NVC's own tests: regress 1458 ok and 67 skipped (`disabled on windows`) of 1525 on Windows, 1525 ok on Linux, Linux unit
tests 24 suites, 1175 checks, 0 failures ([kernel-tests.md](kernel-tests.md)). lvdsim: `ctest` 193 of 193 (Windows debug and release, Linux Debug with the GUI in a fresh directory).

| File | Bytes | SHA-256 |
|------|------:|---------|
| `lvdsim-0.1.0-win64-setup.exe` | 54 737 090 | `1c81d11a08eb1efda787bb1eb78705371f2aba5118d6317e4c37f999a1194626` |
| `lvdsim-0.1.0-win64-portable.zip` | 76 890 073 | `28993845a87e7cc09fddfe2dfa3c24fab1725bfa7c8a119b69ad406aac571828` |
| `lvdsim-0.1.0-linux-x86_64.run` | 71 605 891 | `c3beb678464d380e1585c60a4f4d60dc597eb32c0f39d9c837c6442c999b8422` |
| `lvdsim-0.1.0-linux-x86_64-portable.tar.gz` | 102 622 174 | `308f26f1cd89b3351ad419557f61d0861af93184e6084818f993c1a51a46c948` |

Folders: `build/release/0.1.0-20261108-132823` (Windows) and `build/release/linux-0.1.0-20261108-132858` (Linux, copied with `--copy-to-windows`; originals in
WSL `~/build/lvdsim/release/0.1.0-20261108-132858`).

Verified by the scripts themselves: Windows installer and zip (1074 entries, no `portable.txt` in the installer payload, 41 PE files resolve their imports, smoke run
`vlib`, `vcom`, `vsim -c` in a minimal environment); Linux `--check`, payload checksum, 149 ELF files isolated, smoke install and uninstall, installed GUI settings in
`HOME/.config`, portable tarball flow with settings in `data/`. Verified by hand afterwards:

* Windows installer, per user, `/VERYSILENT /CURRENTUSER /TASKS="!addtopath,!desktopicon" /DIR=<throwaway>`: 1074 files, no `portable.txt`, `vlog`, `vlog -sv`, `vcom`, `vsim -c`
  of `examples/mixed_language` from the install directory with `PATH` reduced to it and `C:\Windows` (`events {"00000010"} avg {"00101110"}`, `vsim` exits), uninstall
  (`unins000 /VERYSILENT`): directory, HKCU uninstall key and Start Menu group gone, raw user `PATH` (REG_EXPAND_SZ, 440 characters) identical after install and after uninstall.
  **Note:** on this machine an all-users install of lvdsim 0.1.0 exists (`C:\Program Files\lvdsim`, key `HKLM\...\Uninstall\lvdsim_is1`, installed by the owner). Without
  `/CURRENTUSER` the per-user installer then waits for an elevation prompt that nobody sees (the first attempts hung and were stopped; nothing was installed). With
  `/CURRENTUSER` it installs next to it ("Detected previous administrative 64-bit install? Yes", display name `lvdsim 0.1.0 (Current user)`). The all-users install was not touched.
* Windows zip, unpacked into a clean folder, `PATH` reduced to the folder and `C:\Windows`, `portable.txt` present: the same flow, `nvc.exe` identical (SHA-256) to
  `kernel/nvc/build/bin/nvc.exe`, `lvdsim-gui` offscreen with `sim.do` wrote `data\lvdsim-gui.ini` and a screenshot (`Now: 2 us`, live waveform).
* Linux `.run`: `sha256sum -c` of both files, `--check`, install with `--prefix` into a throwaway directory and `HOME` and `--no-path`, the flow (`vlib`, `vlog`, `vlog -sv`, `vcom`, `vsim -c`, all exit 0) from
  `env -i PATH=<prefix>/bin:/usr/bin:/bin`, `lvdsim-gui` offscreen screenshot, `--uninstall` removes the prefix and the `~/.local` entries; the only difference in the
  `HOME` listing afterwards is `.config/lvdsim/lvdsim.conf` written by the installed GUI (normal mode keeps its settings there, as before). Portable tarball: same flow, GUI settings in `data/lvdsim-gui.ini`, `HOME` unchanged.

Not repeated: the second Windows installer run with the `PATH` task enabled (the `PATH` writing code is unchanged), a real GPU check of the new builds, Windows 10.

### Rebuild 2026-11-12

Reason: the current tree (many GUI changes: row arranging, scroll bars and follow, analog settings, default layout with Library and Sim tabs over Objects,
drag from Objects, port icons, end-of-data and analog fixes, GUI-default example scripts) and the kernel `kernel/nvc` at `lvdsim` commit `53d41eeae` (merge of
upstream master). Kernel not rebuilt: Windows `kernel/nvc/build` and Linux `~/build/nvc-fork-linux-s2` both report `nvc 1.24-devel (53d41eeae)`. The tree had
uncommitted changes, so `git commit` in `build-info.txt` (`5a73a31`) does not describe the sources exactly.

Tests before packaging: Windows `mingw-debug` (`-Werror`) 200 of 200; Linux (`~/build/lvdsim-rel2`, Debug, GUI on) 200 of 200 with `LVDSIM_NVC` pointing at the new
Linux nvc (the 10 integration tests then run instead of skipping). **`scripts_libs_compile` on Linux was a test-environment problem, not a regression:**
`scripts/libs/test_libs.sh` took `kernel/nvc/build/bin/nvc.exe` whenever it was "executable", and on a `/mnt/d` checkout under WSL every file is, so the Windows
nvc was asked to compile with Linux paths and every file failed (no Linux `nvc` is on that PATH). Fixed in the script (not the kernel): on Linux it now uses
`LVDSIM_NVC` if set, else the newest `~/build/nvc-fork-linux*` whose version line has the abbreviated kernel commit, else a Linux `kernel/nvc/build/bin/nvc`, else
`nvc` on `PATH`, else skips with 77. `nvc.exe` is only used on Windows.

| File | Bytes | SHA-256 |
|------|------:|---------|
| `lvdsim-0.1.0-win64-setup.exe` | 54 752 790 | `286ac23868572822dfe2728659bd53aea8b40eb77a162c73074c40e6c44ab9ce` |
| `lvdsim-0.1.0-win64-portable.zip` | 76 927 669 | `9b96df84485420f7e9eb669edf12d190f8e21399285304b6255278735fdc7543` |
| `lvdsim-0.1.0-linux-x86_64.run` | 71 684 171 | `f86384319f6f6cf5f16a215b063d2b4968574a0787aaeef1f5748f193a2a4a32` |
| `lvdsim-0.1.0-linux-x86_64-portable.tar.gz` | 102 682 708 | `b6fe16fb6e5d44855c2d4e9bc3d5bae6f034f8cfb3b225355b9b907d91244e78` |

Folders: `build/release/0.1.0-20261112-190124` (Windows) and `build/release/linux-0.1.0-20261112-r2` (Linux, `--copy-to-windows`; originals in WSL
`~/build/lvdsim/release/0.1.0-20261112-190128`). Linux nvc: `--nvc-build ~/build/nvc-fork-linux-s2` (Debian LLVM 19.1.7). The Linux `build-info.txt` says "15 uncommitted
change(s) in kernel/nvc": that count is taken by git on the `/mnt/d` drive (file mode noise); the Windows build-info of the same tree says 0.

Verified by the scripts: as in the earlier sections (Windows 1074 files, PE dependency scan, smoke `vlib`/`vcom`/`vsim -c`; Linux `--check`, 149 ELF files isolated, smoke
install, uninstall leaves `HOME` as before, portable flow). Verified by hand afterwards:

* Windows installer, `/VERYSILENT /CURRENTUSER /DIR=<throwaway>`: (a) `/TASKS="!addtopath,!desktopicon"`: 1074 files, no `portable.txt`, installed nvc is `53d41eeae`, `vlib` runs, uninstall removes the
  directory, raw user `PATH` (REG_EXPAND_SZ, 440 characters, read with `DoNotExpandEnvironmentNames`, backed up first) identical after install and after uninstall, `reg query` text identical;
  (b) `/TASKS="addtopath,!desktopicon"`: the raw value after install is exactly the old value plus `;<dir>`, after uninstall it is identical to the original again (kind and value, byte for byte).
  The all-users install in `C:\Program Files\lvdsim` and its HKLM key were not touched (still present).
* Windows zip, unpacked into a new folder, `PATH` = package folder plus `C:\Windows` and `System32`: `nvc.exe` SHA-256 equal to `kernel/nvc/build/bin/nvc.exe`; `vlib`, `vlog`, `vlog -sv`, `vcom -2008`, `vsim -c`
  of `examples/mixed_language` give `events {"00000010"} avg {"00101110"}`; `lvdsim-gui` offscreen with `-do sim.do work.tb_mixed --screenshot` shows Library|Sim tabs over Objects, the vertical and horizontal
  wave scroll bars and the new toolbar button; started on the real windows platform (no offscreen) its own screenshot shows `Renderer: GPU`.
* Linux `.run` (`sha256sum -c` of both files, `--check`), install into a throwaway prefix and `HOME` (`--no-path`), the same flow from `PATH=<prefix>/bin:/usr/bin:/bin` (all exit 0, same values), offscreen GUI screenshot
  (same layout, `Renderer: QPainter (GPU not available: platform offscreen)`), `--uninstall` removes the prefix, `.local`, icon and desktop entry (only `.config/lvdsim/lvdsim.conf` written by the GUI remains).
  Portable tarball in a clean `HOME`: same flow, GUI settings in `data/lvdsim-gui.ini`, `HOME` left empty.

Not verified: the Windows installer's installed GUI (same payload as the zip, only the zip GUI was started), all-users install, signing, a real GPU on Linux, Windows 10, other distributions.

### Release 0.1.5 (2026-11-14)

Reason: version 0.1.5 (`project(lvdsim VERSION 0.1.5)`), the current tree (new docks Project, Coverage, Memory, List, Call Stack, Locals, Problems,
Regression; console completion; expressions; SDF; projects and regression runs) and the kernel `kernel/nvc` at `lvdsim` commit `53d41eeae` plus the
uncommitted change sets 04 to 39 (140 files, all staged). The library format of the kernel changed (node layout), so both packages ship standard libraries
freshly built by their own nvc build. The tree had uncommitted changes, so `git commit` in `build-info.txt` (`5a73a31`) does not describe the sources exactly.

Kernel builds: Windows `kernel/nvc/build` (current, `nvc 1.24-devel (53d41eeae) (Using LLVM 23.1.5)`, not rebuilt). Linux: the existing builds did not fit
(`~/build/nvc-fork-linux-s9*` were made from upstream `0b2837794` plus patch files and report `0b2837794`; `~/build/nvc-fork-linux-s2` reports `53d41eeae` but
lacks the change sets), so a new one was made from the fork tree: clone of `kernel/nvc` (`core.autocrlf=false`) at `53d41eeae` in `~/build/nvc-fork-src-rel015`, the
working tree diff applied (`git diff --cached --binary HEAD`, LF only, `D:/tmp/nvc-rel015-wt.patch`), `configure --enable-tcl` in `~/build/nvc-fork-linux-rel015`
(Debian LLVM 19.1.7), 0 compiler warnings, `nvc 1.24-devel (53d41eeae) (Using LLVM 19.1.7)`. Its `make check`: regress 1571 ok of 1571, unit tests 24 suites,
1182 checks, 0 failures, 0 errors. Logs: WSL `~/build/nvc-fork-rel015-{autogen,configure,make,check}.log`.

Tests before packaging: Windows `mingw-debug` (`-Werror`, fresh `build/mingw-debug-rel015`) 317 of 317; Linux Debug with the GUI (fresh `~/build/lvdsim-rel015`,
`LVDSIM_NVC`/`LVDSIM_NVC_LIBDIR` set to `~/build/nvc-fork-linux-rel015`) 317 of 317. **The first release build failed** on both platforms: at `-O2` GCC 13.1
(MinGW) and GCC 14.2 (Debian) report `-Werror=stringop-overflow` for the range `insert` that appends child scopes to the growing work list in
`Database::childInstances` (`tools/src/coverage.cpp`), a known false positive that the Debug build does not see. Replaced by an equivalent `push_back` loop;
both debug suites rerun after the change, 317 of 317 each. The empty output folders of the failed runs were moved to
`D:/tmp/release-0.1.5-failed-win-20261114-121109` and `D:/tmp/release-0.1.5-failed-linux-20261114-121109`.

| File | Bytes | SHA-256 |
|------|------:|---------|
| `lvdsim-0.1.5-win64-setup.exe` | 55 986 702 | `e14a7c28d9bb298d0fe7a453a5d0c69d0019568c2e60a4da1007cc33f7ed0381` |
| `lvdsim-0.1.5-win64-portable.zip` | 80 568 376 | `be0e2c0163355de65c7186a77d5b90c381c98be63b3c6ecd9eb3fda9c10c40a5` |
| `lvdsim-0.1.5-linux-x86_64.run` | 72 398 615 | `3d223bd7c914a870eab6c91644bec64265cae78b80ddf05e6ba77e68109981cf` |
| `lvdsim-0.1.5-linux-x86_64-portable.tar.gz` | 106 120 262 | `31911fec187af95e19a6d10097072d9e1fc855cd526b8e623e7b731149f59e90` |

Folders: `build/release/0.1.5-20261114-121259` (Windows) and `build/release/linux-0.1.5-20261114-121317` (Linux, `--copy-to-windows`; originals in WSL
`~/build/lvdsim/release/0.1.5-20261114-121317`). Linux nvc: `--nvc-build ~/build/nvc-fork-linux-rel015`. `build-info.txt` counts the uncommitted kernel changes as
140 (Windows) and 152 (Linux, file mode noise of the `/mnt/d` checkout, as before).

Verified by the scripts: Windows installer 53.4 MB, zip 1074 entries with the required files, no `portable.txt` in the installer payload, 41 PE files resolve their imports,
smoke `vlib`/`vcom`/`vsim -c` in a minimal environment; Linux `--check` and `--help`, payload checksum, 149 ELF files isolated, minimum glibc 2.38, smoke install, installed GUI
settings in `HOME/.config`, uninstall leaves `HOME` as before, portable flow with settings in `data/`. Verified by hand afterwards:

* Windows installer, `/VERYSILENT /CURRENTUSER /DIR=<throwaway>`, raw user `PATH` (REG_EXPAND_SZ, 427 characters, read with `DoNotExpandEnvironmentNames`, backed up first to
  `D:/tmp/userpath_backup_rel015_1215.txt`): (a) `/TASKS="!addtopath,!desktopicon"`: 1074 files, no `portable.txt`, display name `lvdsim 0.1.5 (Current user)`, installed `nvc.exe`
  SHA-256 equal to `kernel/nvc/build/bin/nvc.exe`, `vlib`, `vlog`, `vlog -sv`, `vcom -2008`, `vsim -c` of `examples/mixed_language` with `PATH` = install folder, `C:\Windows`, `System32`
  give `events {"00000010"} avg {"00101110"}` (all exit 0); `PATH` unchanged by the install; (b) `/TASKS="addtopath,!desktopicon"`: the raw value after install is exactly the old value plus
  `;<dir>`. After each uninstall: directory, HKCU uninstall key and Start Menu group gone, raw `PATH` identical to the original (kind and value, byte for byte), `reg query` text identical.
  The all-users install in `C:\Program Files\lvdsim` and its HKLM key were not touched (still present). Script: `D:/tmp/winverify_rel015_inst.ps1`.
* Windows zip, unpacked into `D:/tmp/lvdsim_zip015_1220`, the same flow from the same minimal `PATH` (same values), `nvc.exe` SHA-256 equal to the kernel build
  (`0360515431f3930fdd1809e31e18e1b3f34ecc2be9df7b6b0118a1f7a7d23723`); `lvdsim-gui` offscreen with `-do sim.do work.tb_mixed --screenshot`: Library|Sim|Project tabs over Objects
  (port icons), live waveform to `Now: 2 us`, Processes|Breakpoints|Call Stack tabs and the Locals dock at the bottom, settings in `data\lvdsim-gui.ini`; a second run with
  `--send "view memory list coverage"` shows the List and Memory docks tabbed with Source and the Coverage dock tabbed with the Transcript, layout sane. Started on the real
  windows platform (no offscreen, no `LVDSIM_RENDERER`, no `QT_QPA_FONTDIR`) its screenshot shows `Renderer: GPU`.
* Linux `.run`: `sha256sum -c` of both files, `--check`, install into a throwaway prefix and `HOME` (`--no-path`), the same flow from `env -i PATH=<prefix>/bin:/usr/bin:/bin`
  (all exit 0, same values), offscreen GUI screenshot (same layout as on Windows, `Renderer: QPainter (GPU not available: platform offscreen)`), `--uninstall` removes the prefix
  and `.local` (only `.config/lvdsim/lvdsim.conf` written by the GUI remains). Portable tarball in a clean `HOME`: same flow, bundled nvc reports `53d41eeae`, GUI settings in
  `data/lvdsim-gui.ini`, `HOME` left empty. Script: `D:/tmp/linverify_rel015.sh`.

Not verified: the Windows installer's installed GUI (same payload as the zip, only the zip GUI was started), all-users install, signing, a real GPU on Linux, Windows 10, other
distributions. The bundled Linux `nvc` is not byte-identical to the build tree's binary (the bundle script patches ELF files with patchelf), so it was checked by its version line only.

### Release 0.1.5 rebuild (2026-11-14, second build)

Reason: the first 0.1.5 packages predate a number of fixes made the same day: coverage fixes, fixes for transcript hangs, the Break button
(Ctrl+Break) and the exit dialog, analog virtual buses, row heights, the view declaration, and kernel pull request 10 (`describe -location`,
`kernel/upstream/10-shell-describe-locations`). The version stays 0.1.5. The first 0.1.5 folders were kept as they were. The tree had uncommitted
changes, so `git commit` in `build-info.txt` (`5a73a31`) does not describe the sources exactly.

Kernel builds. Windows: `kernel/nvc/build`, `nvc 1.24-devel (53d41eeae) (Using LLVM 23.1.5)`, not rebuilt. No file under `kernel/nvc/src` or `lib` is newer than
`build/bin/nvc.exe` (14:53). Linux: a new build from the fork tree. `kernel/nvc` was cloned with `core.autocrlf=false` at `53d41eeae` into
`~/build/nvc-fork-src-rel015b`. Then `git -c core.autocrlf=false diff --binary HEAD` (staged and unstaged together, 141 files, LF only,
`D:/tmp/nvc-rel015b-wt.patch`) was applied and the 15 untracked files were copied in with CRs stripped (they had none). Twelve files in the Windows checkout
have CRLF line ends (`i/lf w/crlf`, for example `NEWS.md`, `src/simp.c` and `src/cov/cov-*.c`). Apart from the line ends they equal `HEAD`, so the clone keeps
the LF blobs. Check: `0b2837794` plus the patches `kernel/upstream/01` to `10` gives the 179 files the patches touch, and all 179 are byte-identical to the new
source tree. The only other change in that tree is `.gitignore`. The build is `configure --enable-tcl` in `~/build/nvc-fork-linux-rel015b` (Debian LLVM 19.1.7)
with 0 compiler warnings, and its version line is `nvc 1.24-devel (53d41eeae) (Using LLVM 19.1.7)`. `make check`: regress 1578 ok, unit tests 24 suites,
1182 checks, 0 failures, 0 errors. Logs: WSL `~/build/nvc-fork-rel015b-{autogen,configure,make,check}.log`.

Tests before packaging. Windows `mingw-debug` (`-Werror`, fresh `build/mingw-debug-rel015b`) passed 333 of 333. Linux Debug with the GUI (fresh
`~/build/lvdsim-rel015b`, `-Werror`, 0 warnings) passed 334 of 334, with `LVDSIM_NVC`/`LVDSIM_NVC_LIBDIR` set to `~/build/nvc-fork-linux-rel015b`. The one
extra Linux test is the POSIX-only "Ctrl-C during a child is left to the child". The release builds had no new `-Werror` warnings, and no source change was needed.

| File | Bytes | SHA-256 |
|------|------:|---------|
| `lvdsim-0.1.5-win64-setup.exe` | 56 069 940 | `62a90c8a4f69fa4feb52ecf79197e710117b56610950032bbb12f0ee11b812dd` |
| `lvdsim-0.1.5-win64-portable.zip` | 80 680 956 | `dc589ea30f7112275b3448565934025f0aacd103c8122e92ebb6ddb09ad28121` |
| `lvdsim-0.1.5-linux-x86_64.run` | 72 454 927 | `1406b13053e39aea93776f5d692da318c68e17a4c49fd2dac33bfcd07661ed8e` |
| `lvdsim-0.1.5-linux-x86_64-portable.tar.gz` | 106 218 932 | `8398a4a8eb79152b04926e3d040a8e1e681171b02e91957074a8336939533d71` |

Folders: `build/release/0.1.5-20261114-153934` (Windows) and `build/release/linux-0.1.5-20261114-154220` (Linux, copied with `--copy-to-windows`; the originals are in WSL
`~/build/lvdsim/release/0.1.5-20261114-154220`). Linux nvc: `--nvc-build ~/build/nvc-fork-linux-rel015b`. `build-info.txt` counts the uncommitted kernel changes as
156 on Windows and 168 on Linux. The Linux count is higher because of file mode noise in the `/mnt/d` checkout. Logs: `D:/tmp/rel015b-{win,lin}-release.log`.

Verified by the scripts: the same checks as for the first 0.1.5 build. Windows: installer 53.5 MB, zip 1074 entries, no `portable.txt` in the installer payload,
41 PE files resolve their imports, and the smoke run passes. Linux: `--check` and `--help`, payload checksum, 149 ELF files isolated, smoke install, installed GUI,
uninstall and the portable flow pass. Verified by hand afterwards (every headless GUI start used `--settings FILE`):

* Windows installer, `/VERYSILENT /CURRENTUSER /DIR=<throwaway>`. The raw user `PATH` (REG_EXPAND_SZ, 427 characters, read with `DoNotExpandEnvironmentNames`) was
  backed up first to `D:/tmp/userpath_backup_rel015b_1545.txt`.
  (a) `/TASKS="!addtopath,!desktopicon"`: 1074 files, no `portable.txt`, display name `lvdsim 0.1.5 (Current user)`. The installed `nvc.exe` SHA-256 equals
  `kernel/nvc/build/bin/nvc.exe` (`1694449c29eedf36d427ecff3734f6bdbf7338cab2b307d2f89322d17efe6acc`). `vlib`, `vlog`, `vlog -sv`, `vcom -2008` and `vsim -c` of
  `examples/mixed_language`, run with `PATH` = install folder, `C:\Windows` and `System32`, give `events {"00000010"} avg {"00101110"}` and all exit 0. The
  **installed GUI** was started offscreen (`-do sim.do`). Its screenshot shows Library|Sim|Project tabs over Objects with port icons, the live waveform to
  `Now: 2 us`, the Break button after Run -all on the toolbar (disabled while idle), and Processes|Breakpoints|Call Stack with Locals at the bottom. The
  install left `PATH` unchanged.
  (b) `/TASKS="addtopath,!desktopicon"`: after the install the raw value is exactly the old value plus `;<dir>`.
  After each uninstall, the directory, the HKCU uninstall key and the Start Menu group were gone. The raw `PATH` was identical to the original (kind and value, byte
  for byte), and the `reg query` text was identical too. `HKCU\Software\lvdsim` was unchanged. The all-users install in `C:\Program Files\lvdsim` and its HKLM key
  were not touched. Scripts: `D:/tmp/winverify_rel015b_{inst,flow}.ps1`, log `D:/tmp/rel015b-wininst.log`.
* Windows zip, unpacked into `D:/tmp/lvdsim_zip015b_1546` (`portable.txt` present): the same flow from the same minimal `PATH` gave the same values, and `nvc.exe` is
  identical to the kernel build. The offscreen GUI screenshot shows the same layout as the installed one. A start on the real windows platform (no offscreen, no
  `LVDSIM_RENDERER`, no `QT_QPA_FONTDIR`) shows `Renderer: GPU`. Log `D:/tmp/rel015b-winzip.log`.
* Linux `.run`: `sha256sum -c` of both files and `--check` pass. It was installed into a throwaway prefix and `HOME` with `--no-path`. The same flow from
  `env -i PATH=<prefix>/bin:/usr/bin:/bin` exits 0 everywhere and gives the same values. The offscreen GUI screenshot has the same layout as on Windows and shows
  `Renderer: QPainter (GPU not available: platform offscreen)`. `--uninstall` removes the prefix and `.local`, and the `HOME` listing is then identical to the
  one before the install. Portable tarball in a clean `HOME`: the same flow passes, the bundled nvc reports `53d41eeae`, `HOME` is left empty, and the screenshot
  is the same. Script: `D:/tmp/linverify_rel015b.sh`, log `D:/tmp/rel015b-linverify.log`, screenshots `D:/tmp/rel015b-lin-{inst,port}-shot.png`.

Not verified: the portable mode's own settings file (`data/lvdsim-gui.ini`) by hand, because `--settings` overrides it; the release scripts checked it on both
platforms. Also not verified: an all-users install, signing, a real GPU on Linux, Windows 10, and other distributions. The Windows kernel build was not re-run:
the timestamps show it is current, and a `make -n` only reports the usual automake `stamp-h1`/`config.h` timestamp artefact.

## Licences in the packages

`licenses/` holds: `lvdsim-GPL-3.0.txt` (lvdsim is GPL-3.0-or-later; same text as NVC's), `NVC-GPL-3.0.txt`, `fstapi-MIT.txt`,
`Qt-LGPL-3.0.txt` and `NOTICE-Qt.txt` (Qt 6.11.1 is LGPL-3.0 and linked dynamically: unmodified DLLs that the user can replace;
only Core, Gui, Widgets, Network and Svg with their plugins are deployed, no GPL-only modules), `NOTICE-third-party.txt`
(fstapi's LZ4 and FastLZ, zlib, MinGW runtime, nvc's DLLs) and `third-party/` with the MSYS2 licence files for libffi, LLVM,
readline, zlib, zstd, ncurses, termcap, libiconv, expat, libxml2, winpthreads. nvc is a separate program under GPL-3.0; the
fork changes are the branch `lvdsim` of `giorgiodelmondo/nvc` (commit in `build-info.txt`), which must accompany binary distribution as corresponding source, together with the lvdsim source.
GNU readline (GPL-3) is linked into nvc, which is compatible with nvc's own licence.
