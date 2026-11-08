#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the linux-release preset and assembles a tarball (run inside WSL Debian or on any Linux):
#   ~/build/lvdsim/package/lvdsim-<version>-linux-x86_64/       the folder
#   ~/build/lvdsim/package/lvdsim-<version>-linux-x86_64.tar.gz
# Needs: cmake, ninja, g++, Qt 6 dev packages for the GUI (qt6-base-dev qt6-base-private-dev qt6-shadertools-dev). Without Qt the GUI is
# left out and the command line tools are packaged anyway.
# nvc comes from NVC_BUILD (default ~/build/nvc-linux, see docs/kernel-tests.md); it uses the system Tcl, LLVM,
# libffi, zstd, readline (apt line in docs/packaging.md).
# Usage: scripts/package-linux.sh [--skip-build]
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
nvcBuild=${NVC_BUILD:-$HOME/build/nvc-linux}
outDir=${OUT_DIR:-$HOME/build/lvdsim/package}
buildDir=$HOME/build/lvdsim/linux-release
version=$(sed -n 's/^project(lvdsim VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
name=lvdsim-$version-linux-x86_64
stage=$outDir/$name

[ -x "$nvcBuild/bin/nvc" ] || { echo "nvc not found in $nvcBuild/bin (set NVC_BUILD)"; exit 1; }
[ -e "$stage" ] && { echo "$stage exists; remove it or set OUT_DIR (this script never deletes)"; exit 1; }
[ -e "$stage.tar.gz" ] && { echo "$stage.tar.gz exists"; exit 1; }

gui=OFF
cm=/usr/lib/x86_64-linux-gnu/cmake
if [ -d "$cm/Qt6Widgets" ] && [ -d "$cm/Qt6ShaderTools" ]; then
    gui=ON
else
    echo "note: Qt6 Widgets/GuiPrivate/ShaderTools dev files not all present; building without the GUI."
    echo "      sudo apt install qt6-base-dev qt6-base-private-dev qt6-shadertools-dev qt6-svg-dev"
fi
if [ "${1:-}" != "--skip-build" ]; then
    cmake --preset linux-release -DLVDSIM_BUILD_GUI=$gui
    cmake --build --preset linux-release
fi
bin=$buildDir/bin

mkdir -p "$stage/bin" "$stage/nvc/bin" "$stage/nvc/lib" "$stage/tcl/compat" "$stage/licenses"

# Our programs: the five command names are copies of the multi-call lvdsim-cli.
cp "$bin/lvdsim-cli" "$stage/bin/"
for t in vlib vmap vcom vlog vsim; do cp "$bin/lvdsim-cli" "$stage/bin/$t"; done
if [ -x "$bin/lvdsim-gui" ]; then cp "$bin/lvdsim-gui" "$stage/bin/"; else echo "note: no lvdsim-gui in this package (no Qt 6 dev packages)"; fi
strip "$stage"/bin/* 2>/dev/null || true

# nvc and its libraries (no test plugins, no static archives).
cp "$nvcBuild/bin/nvc" "$stage/nvc/bin/"
strip "$stage/nvc/bin/nvc" 2>/dev/null || true
for l in std std.08 std.19 ieee ieee.08 ieee.19 nvc nvc.08 nvc.19 vital vital.08 vital.19 synopsys synopsys.08 synopsys.19 siginit.so; do
    [ -e "$nvcBuild/lib/$l" ] && cp -r "$nvcBuild/lib/$l" "$stage/nvc/lib/"
done

cp "$root/tcl/compat/lvdcompat.tcl" "$stage/tcl/compat/"
# Library compile scripts (the GUI runs them from Tools > Compile libraries; no vendor sources inside).
mkdir -p "$stage/scripts/libs"
cp "$root/scripts/libs/compile_libs.sh" "$root/scripts/libs/compile_libs.bat" "$stage/scripts/libs/"
chmod +x "$stage/scripts/libs/compile_libs.sh"
cp -r "$root/packaging/licenses/." "$stage/licenses/"
cp "$root/kernel/nvc/COPYING" "$stage/licenses/NVC-GPL-3.0.txt"
cp "$root/third_party/fstapi/LICENSE" "$stage/licenses/fstapi-MIT.txt"
cat > "$stage/README.txt" <<'EON'
lvdsim - open source HDL simulation front end (NVC kernel, familiar simulator commands)

Add bin/ to PATH, then:
    vlib work
    vcom -2008 design.vhd
    vsim -c -do "run -all; quit" top      command line batch flow (also vlog, vmap)
    lvdsim-gui                             the graphical simulator (when present in bin/)

nvc, its VHDL libraries and the Tcl command package are found relative to the program directory (nvc/, tcl/compat/).
scripts/libs/compile_libs.sh builds the Xilinx (Vivado) and Altera (Quartus) simulation libraries from your own installation
(lvdsim-gui: Tools > Compile libraries); no vendor sources are shipped.
No environment variables are needed; LVDSIM_NVC, LVDSIM_NVC_LIBDIR and LVDSIM_TCL_DIR still override.
Licences are in licenses/ (GPL-3.0 for lvdsim and NVC, Qt under LGPL-3.0 linked dynamically).
EON
cat >> "$stage/README.txt" <<'EON'

Linux notes: the programs are in bin/ (add it to PATH). nvc uses the system Tcl, LLVM, libffi, zstd and readline:
    sudo apt install tcl8.6 libllvm19 libffi8 libzstd1 libreadline8
lvdsim-gui uses the system Qt 6 (dynamic, LGPL):
    sudo apt install libqt6widgets6 libqt6gui6 libqt6core6 libqt6svg6 qt6-qpa-plugins
EON

(cd "$outDir" && tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@${SOURCE_DATE_EPOCH:-0}" -czf "$name.tar.gz" "$name")
du -sh "$stage" "$stage.tar.gz"
