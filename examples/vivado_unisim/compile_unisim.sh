#!/bin/sh
# Compile the Xilinx VHDL unisim library from a Vivado installation into ./unisim.
# The Xilinx sources are not part of lvdsim (they are under Xilinx's licence): point the script at your own copy.
#
#   ./compile_unisim.sh                       uses $XILINX_VIVADO/data/vhdl/src/unisims
#   ./compile_unisim.sh /path/to/unisims      a folder with unisim_VCOMP.vhd and unisim_VPKG.vhd
#
# Run it once; the library is reused by run.sh.
set -e
# Xilinx unisim_VCOMP is a very large package: give the compiler a bigger heap.
export LVDSIM_NVC_OPTS="${LVDSIM_NVC_OPTS:--M 128m}"
cd "$(dirname "$0")"

SRC="${1:-$XILINX_VIVADO/data/vhdl/src/unisims}"
if [ ! -f "$SRC/unisim_VCOMP.vhd" ] || [ ! -f "$SRC/unisim_VPKG.vhd" ]; then
    echo "unisim_VCOMP.vhd / unisim_VPKG.vhd not found in '$SRC'." >&2
    echo "Set XILINX_VIVADO or pass the unisims folder as the first argument." >&2
    exit 1
fi

vlib unisim
vcom -2008 -explicit -work unisim "$SRC/unisim_VPKG.vhd"
vcom -2008 -explicit -work unisim "$SRC/unisim_VCOMP.vhd"

if [ -d "$SRC/primitive" ]; then
    # Vivado layout: one file per primitive. Compile all of them.
    for f in "$SRC"/primitive/*.vhd; do
        vcom -2008 -explicit -work unisim "$f"
    done
else
    # Older single-file layout.
    [ -f "$SRC/unisim_SMODEL.vhd" ] && vcom -2008 -explicit -work unisim "$SRC/unisim_SMODEL.vhd"
    vcom -2008 -explicit -work unisim "$SRC/unisim_VITAL.vhd"
fi
echo "unisim library ready in $(pwd)/unisim"
