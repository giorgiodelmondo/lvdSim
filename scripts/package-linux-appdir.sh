#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Self-contained Linux bundle (an AppDir without AppImage tooling) and a shell installer, built from the linux-release preset.
# Run inside WSL Debian or any Linux. Nothing is downloaded; nothing is deleted (existing outputs stop the script).
#
#   ~/build/lvdsim/appdir/lvdsim-<ver>-linux-x86_64/        the bundle (AppRun, bin/, lib/, plugins/, nvc/, tcl/, licenses/ ...)
#   ~/build/lvdsim/appdir/lvdsim-<ver>-linux-x86_64.run     installer: sh stub + __PAYLOAD_BELOW__ + tar.xz, SHA-256 checked
#
# Usage: scripts/package-linux-appdir.sh [--skip-build] [--no-installer]
# Environment: NVC_BUILD (default ~/build/nvc-linux, a build of the lvdsim branch of the NVC fork), OUT_DIR, COMPRESS=xz|gz (default xz), SOURCE_DATE_EPOCH.
# Needs: cmake ninja g++ Qt 6 dev packages, patchelf, strip, ldd, objdump, dpkg, xz, python3 is not needed.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
nvcBuild=${NVC_BUILD:-$HOME/build/nvc-linux}   # must be built from the lvdsim branch (it has the clean exit after quit)
outDir=${OUT_DIR:-$HOME/build/lvdsim/appdir}
buildDir=$HOME/build/lvdsim/linux-release
version=$(sed -n 's/^project(lvdsim VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
name=lvdsim-$version-linux-x86_64
cd "$root"   # cmake presets are read from the current directory
stage=$outDir/$name
runFile=$outDir/$name.run
compress=${COMPRESS:-xz}
epoch=${SOURCE_DATE_EPOCH:-0}
skipBuild=0
noInstaller=0
for a in "$@"; do
    case "$a" in
        --skip-build) skipBuild=1 ;;
        --no-installer) noInstaller=1 ;;
        *) echo "unknown option $a"; exit 1 ;;
    esac
done

[ -x "$nvcBuild/bin/nvc" ] || { echo "nvc not found in $nvcBuild/bin (set NVC_BUILD)"; exit 1; }
[ -e "$stage" ] && { echo "$stage exists; remove it or set OUT_DIR (this script never deletes)"; exit 1; }
[ -e "$runFile" ] && { echo "$runFile exists"; exit 1; }
for t in patchelf strip ldd objdump dpkg tar; do command -v $t >/dev/null || { echo "missing tool: $t"; exit 1; }; done
[ "$compress" = xz ] && ! command -v xz >/dev/null && { echo "xz not found, using gzip"; compress=gz; }

if [ $skipBuild = 0 ]; then
    cmake --preset linux-release -DLVDSIM_BUILD_GUI=ON
    cmake --build --preset linux-release
fi
bin=$buildDir/bin
for p in lvdsim-gui lvdsim-cli; do [ -x "$bin/$p" ] || { echo "$bin/$p missing (build with LVDSIM_BUILD_GUI=ON)"; exit 1; }; done

qtplug=$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)
[ -d "$qtplug" ] || { echo "Qt plugin directory not found"; exit 1; }

mkdir -p "$stage/bin" "$stage/nvc/bin" "$stage/nvc/lib" "$stage/tcl/compat" "$stage/scripts/libs" "$stage/lib" \
    "$stage/plugins" "$stage/licenses/third-party"
work=$(mktemp -d "${TMPDIR:-/tmp}/lvdsim-appdir.XXXXXX")   # removed at the end (created by this script)
trap 'rm -rf "$work"' EXIT

# ---- programs ----
cp "$bin/lvdsim-cli" "$bin/lvdsim-gui" "$stage/bin/"
for t in vlib vmap vcom vlog vsim; do cp "$bin/lvdsim-cli" "$stage/bin/$t"; done

# ---- nvc, its VHDL libraries and the Tcl script library ----
cp "$nvcBuild/bin/nvc" "$stage/nvc/bin/"
for l in std std.08 std.19 ieee ieee.08 ieee.19 nvc nvc.08 nvc.19 vital vital.08 vital.19 synopsys synopsys.08 synopsys.19 siginit.so; do
    [ -e "$nvcBuild/lib/$l" ] && cp -r "$nvcBuild/lib/$l" "$stage/nvc/lib/"
done
# Tcl_Init finds <exe dir>/../lib/tcl8.6 (the same layout as the Windows package)
tclLib=$(ls -d /usr/share/tcltk/tcl8.6 /usr/share/tcl8.6 2>/dev/null | head -1 || true)
[ -n "$tclLib" ] || { echo "Tcl script library (tcl8.6) not found"; exit 1; }
cp -r "$tclLib" "$stage/nvc/lib/tcl8.6"
rm -f "$stage/nvc/lib/tcl8.6/tclAppInit.c"
tclMods=$(ls -d /usr/share/tcltk/tcl8 2>/dev/null | head -1 || true)
[ -n "$tclMods" ] && cp -r "$tclMods" "$stage/nvc/lib/tcl8"

cp "$root/tcl/compat/lvdcompat.tcl" "$stage/tcl/compat/"
cp "$root/scripts/libs/compile_libs.sh" "$root/scripts/libs/compile_libs.bat" "$stage/scripts/libs/"
chmod +x "$stage/scripts/libs/compile_libs.sh"

# ---- Qt plugins: only what a lvdsim session needs ----
copyPlugins() {   # dir files...
    d=$1; shift
    [ -d "$qtplug/$d" ] || return 0
    mkdir -p "$stage/plugins/$d"
    for f in "$@"; do [ -e "$qtplug/$d/$f" ] && cp -L "$qtplug/$d/$f" "$stage/plugins/$d/"; done
}
copyPlugins platforms libqxcb.so libqoffscreen.so libqminimal.so libqwayland-egl.so libqwayland-generic.so
copyPlugins platforminputcontexts libcomposeplatforminputcontextplugin.so libibusplatforminputcontextplugin.so
copyPlugins imageformats libqgif.so libqico.so libqjpeg.so libqsvg.so
copyPlugins iconengines libqsvgicon.so
copyPlugins xcbglintegrations libqxcb-egl-integration.so libqxcb-glx-integration.so
copyPlugins tls libqcertonlybackend.so libqopensslbackend.so
copyPlugins wayland-shell-integration libxdg-shell.so libqt-shell.so libwl-shell-plugin.so libfullscreen-shell-v1.so libivi-shell.so
copyPlugins wayland-graphics-integration-client libqt-plugin-wayland-egl.so libshm-emulation-server.so libdmabuf-server.so \
    libdrm-egl-server.so libvulkan-server.so
copyPlugins wayland-decoration-client libbradient.so
# the GTK3 platform theme (libqgtk3.so) would pull in GTK and its whole tree: not bundled

# ---- shared libraries: every dependency of every ELF, minus the exclude list ----
# Excluded (taken from the host): glibc and its helpers, the OpenGL/EGL/GBM/DRM/Vulkan stack and udev (they must match the
# host's GPU driver and kernel). libstdc++ and libgcc_s are bundled.
exclude='^(linux-vdso\.so.*|ld-linux.*|libc\.so.*|libm\.so.*|libdl\.so.*|libpthread\.so.*|librt\.so.*|libutil\.so.*|libresolv\.so.*|libanl\.so.*|libnsl\.so.*|libmvec\.so.*|libGL\.so.*|libGLX\.so.*|libGLdispatch\.so.*|libEGL\.so.*|libOpenGL\.so.*|libGLESv2\.so.*|libgbm\.so.*|libdrm.*\.so.*|libvulkan\.so.*|libudev\.so.*|libsystemd\.so.*)$'
echo "$exclude" >"$work/exclude.re"
: >"$work/map.txt"      # soname <TAB> source path
collectDeps() {
    for f in "$@"; do
        env -u LD_LIBRARY_PATH ldd "$f" 2>/dev/null | while IFS= read -r line; do
            case "$line" in
                *"not found"*) echo "ERROR: unresolved dependency of $f: $line" >&2; echo "$line" >>"$work/missing.txt" ;;
                *"=>"*) n=${line%%=>*}; n=$(echo "$n" | tr -d ' \t'); p=${line#*=>}; p=${p%% (*}; p=$(echo "$p" | tr -d ' \t')
                        [ -n "$p" ] && printf '%s\t%s\n' "$n" "$p" ;;
            esac
        done
    done | sort -u
}
elfs() { find "$stage/bin" "$stage/nvc/bin" "$stage/nvc/lib" "$stage/plugins" "$stage/lib" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) \
        -exec sh -c 'for f; do head -c 4 "$f" | grep -q "^.ELF" && echo "$f"; done' sh {} +; }
pass=0
while :; do
    pass=$((pass + 1))
    collectDeps $(elfs) >"$work/deps.txt"
    added=0
    while IFS=$'\t' read -r n p; do
        [ -n "$n" ] || continue
        echo "$n" | grep -Eq -f "$work/exclude.re" && continue
        [ -e "$stage/lib/$n" ] && continue
        cp -L "$p" "$stage/lib/$n"; printf '%s\t%s\n' "$n" "$p" >>"$work/map.txt"; added=$((added + 1))
    done <"$work/deps.txt"
    echo "pass $pass: $added libraries added"
    [ $added = 0 ] && break
done
[ -s "$work/missing.txt" ] && { echo "unresolved dependencies, stopping"; exit 1; }
chmod u+w "$stage/lib"/* "$stage/plugins"/*/* 2>/dev/null || true

# ---- strip, then RPATH relative to each file so the programs also run without AppRun ----
for f in $(elfs); do strip --strip-unneeded "$f" 2>/dev/null || true; done
patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$stage"/bin/*
patchelf --force-rpath --set-rpath '$ORIGIN/../../lib' "$stage/nvc/bin/nvc"
patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$stage/nvc/lib/siginit.so" 2>/dev/null || true
for f in "$stage"/lib/*; do [ -f "$f" ] && head -c 4 "$f" | grep -q '^.ELF' && patchelf --force-rpath --set-rpath '$ORIGIN' "$f"; done
find "$stage/plugins" -name '*.so' | while read -r f; do
    rel=${f#"$stage/plugins/"}; depth=$(echo "$rel" | awk -F/ '{print NF-1}')
    up=$(printf '../%.0s' $(seq 1 $((depth + 1))))
    patchelf --force-rpath --set-rpath "\$ORIGIN/${up}lib" "$f"
done

# ---- launcher, qt.conf, desktop entry, icon ----
cat >"$stage/AppRun" <<'EOF'
#!/bin/sh
# lvdsim launcher. Without arguments it starts the GUI. "AppRun --run <tool> args" runs lvdsim-gui, lvdsim-cli, vlib,
# vmap, vcom, vlog or vsim. A symlink named like a tool (vcom -> .../AppRun) runs that tool. The programs carry an RPATH
# into lib/, so bin/<tool> also works without this script; the script adds what Qt needs for the GUI.
self=$(readlink -f "$0")
here=$(dirname "$self")
tool=$(basename "$0")
case "$tool" in
    AppRun|AppRun.sh)
        tool=lvdsim-gui
        if [ "$1" = "--run" ]; then
            [ $# -ge 2 ] || { echo "usage: AppRun --run <lvdsim-gui|lvdsim-cli|vlib|vmap|vcom|vlog|vsim> [arguments]" >&2; exit 2; }
            tool=$2
            shift 2
        fi ;;
esac
case "$tool" in
    lvdsim-gui|lvdsim-cli|vlib|vmap|vcom|vlog|vsim) ;;
    *) echo "AppRun: unknown program '$tool'" >&2; exit 2 ;;
esac
if [ "$tool" = lvdsim-gui ]; then
    LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export LD_LIBRARY_PATH
    QT_PLUGIN_PATH="$here/plugins"
    export QT_PLUGIN_PATH
    unset QT_QPA_PLATFORMTHEME
fi
exec "$here/bin/$tool" "$@"
EOF
chmod +x "$stage/AppRun"
printf '[Paths]\nPrefix = ..\nPlugins = plugins\n' >"$stage/bin/qt.conf"
cat >"$stage/lvdsim.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=lvdsim
GenericName=HDL simulator
Comment=Open source HDL simulation front end (NVC kernel)
Exec=AppRun %F
TryExec=AppRun
Icon=lvdsim
Terminal=false
Categories=Development;Electronics;
Keywords=VHDL;Verilog;simulator;waveform;
EOF
cp "$root/packaging/lvdsim-256.png" "$stage/lvdsim.png"

# ---- licences: project texts, then one notice per bundled shared library ----
cp -r "$root/packaging/licenses/." "$stage/licenses/"
cp "$root/kernel/nvc/COPYING" "$stage/licenses/NVC-GPL-3.0.txt"
cp "$root/third_party/fstapi/LICENSE" "$stage/licenses/fstapi-MIT.txt"
notice=$stage/licenses/NOTICE-bundled-libraries.txt
{
    echo "Shared libraries bundled in lib/ and plugins/ (taken unmodified from the Debian 13 build machine)"
    echo "Each package's own copyright file is in licenses/third-party/<package>-copyright."
    echo "Source of every package: apt source <package> (Debian 13) or https://sources.debian.org/"
    echo
    printf '%-38s %-28s %s\n' "File" "Debian package" "Licence summary"
    printf '%-38s %-28s %s\n' "----" "--------------" "---------------"
} >"$notice"
{ cat "$work/map.txt"
  find "$stage/plugins" -name '*.so' | while read -r f; do printf '%s\t%s\n' "plugins/${f#"$stage/plugins/"}" "$qtplug/${f#"$stage/plugins/"}"; done
  printf '%s\t%s\n' "nvc/lib/siginit.so" "(built from NVC)"; } | sort >"$work/files.txt"
: >"$work/pkgs.txt"
while IFS=$'\t' read -r n p; do
    case "$p" in "("*) continue ;; esac
    pkg=
    for cand in "$p" "$(readlink -f "$p")" "/usr${p}"; do
        pkg=$(dpkg -S "$cand" 2>/dev/null | grep -v "^diversion" | head -1 | cut -d: -f1 || true)
        [ -n "$pkg" ] && break
    done
    [ -n "$pkg" ] || pkg=UNKNOWN
    cf=/usr/share/doc/$pkg/copyright
    if [ -f "$cf" ]; then
        lic=$(grep -h '^License:' "$cf" | sed 's/^License: *//' | sort -u | grep -v '^$' | head -12 | tr '\n' ';' | sed 's/;$//; s/;/; /g' || true)
        [ -n "$lic" ] || lic="see copyright file"
        cp "$cf" "$stage/licenses/third-party/$pkg-copyright"
    else
        lic="see the package"
    fi
    printf '%-38s %-28s %s\n' "$n" "$pkg" "$lic" >>"$notice"
    echo "$pkg" >>"$work/pkgs.txt"
done <"$work/files.txt"
{ echo; echo "Packages: $(sort -u "$work/pkgs.txt" | tr '\n' ' ')"; echo
  echo "Qt 6 is LGPL-3.0 and linked dynamically: the bundled libraries are the unmodified Debian builds and can be replaced"
  echo "by putting other builds of the same sonames in lib/ and plugins/ (or in LD_LIBRARY_PATH when starting through AppRun)."
  echo "Not bundled (taken from the host): glibc, libGL/libEGL/libOpenGL/libgbm/libdrm/libvulkan and GPU drivers, libudev, libsystemd."
} >>"$notice"
grep -q UNKNOWN "$notice" && echo "warning: some library has no Debian package (see $notice)"

# ---- minimum glibc: highest GLIBC_x.y version needed by any ELF of the bundle ----
: >"$work/glibc.txt"
for f in $(elfs); do
    objdump -T "$f" 2>/dev/null | grep -o 'GLIBC_[0-9][0-9.]*' | sort -u | sed "s|\$|\t$f|" >>"$work/glibc.txt" || true
done
minGlibc=$(cut -f1 "$work/glibc.txt" | sed 's/GLIBC_//' | sort -V | tail -1)
{ echo "Minimum glibc: $minGlibc"
  echo "Built on: $(. /etc/os-release; echo "$PRETTY_NAME"), glibc $(ldd --version | head -1 | sed 's/.* //')"
  echo "Files needing GLIBC_$minGlibc:"; grep -P "^GLIBC_${minGlibc//./\\.}\t" "$work/glibc.txt" | cut -f2 | sed "s|^$stage/||" | head -20; } >"$work/glibcinfo.txt"
cat >"$stage/README.txt" <<EOF
lvdsim $version - open source HDL simulation front end (NVC kernel, familiar simulator commands)

This folder is self contained (Linux x86_64). Start the GUI with ./AppRun (or the installed lvdsim-gui). The command line tools
are in bin/ (vlib vmap vcom vlog vsim lvdsim-cli): add bin/ to PATH, or run "./AppRun --run vsim ...", or symlink vsim etc. to
AppRun. nvc, its VHDL libraries, the Tcl library and the compatibility package are found relative to the program; no
environment variables are needed.

Needs: Linux x86_64 with glibc $minGlibc or newer (built on Debian 13). OpenGL, EGL, GBM, DRM, Vulkan, udev and libsystemd come from the
host system (the GUI draws with the QPainter renderer when no usable GPU is present). The GUI needs an X11 or Wayland session
(xcb and wayland Qt plugins are included); for headless use set QT_QPA_PLATFORM=offscreen. Fonts come from the host (fontconfig).

Licence: GPL-3.0-or-later (licenses/lvdsim-GPL-3.0.txt). licenses/ lists every bundled library and its licence.
Simulation libraries of FPGA vendors are not shipped: scripts/libs/compile_libs.sh builds them from your own installation.
EOF
cp "$work/glibcinfo.txt" "$stage/licenses/MIN-GLIBC.txt"
echo "--- $(head -1 "$work/glibcinfo.txt")"

# normalise times and permissions
find "$stage" -exec touch -h -d "@$epoch" {} +
du -sh "$stage"
[ $noInstaller = 1 ] && exit 0

# ---- installer: stub + marker + payload, with a SHA-256 of the payload inside the stub ----
if [ "$compress" = xz ]; then
    (cd "$outDir" && tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$epoch" -cf - "$name" | xz -T0 -6 >"$work/payload")
else
    (cd "$outDir" && tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$epoch" -cf - "$name" | gzip -9n >"$work/payload")
fi
size=$(wc -c <"$work/payload")
sha=$(sha256sum "$work/payload" | cut -d' ' -f1)
sed -e "s|@VERSION@|$version|" -e "s|@SIZE@|$size|" -e "s|@SHA256@|$sha|" -e "s|@COMPRESSION@|$compress|" \
    "$root/packaging/linux/installer-stub.sh" >"$runFile"
cat "$work/payload" >>"$runFile"
chmod +x "$runFile"
echo "installer: $runFile ($(du -h "$runFile" | cut -f1), payload $size bytes, sha256 $sha, $compress)"
