#!/bin/sh
# Compile the Xilinx VHDL libraries this example needs into ./unisim and ./unimacro.
# The Xilinx sources are under Xilinx's licence and are not part of lvdsim: point the script at your Vivado.
#
#   export XILINX_VIVADO=/path/to/Vivado/2022.2
#   ./compile_libs.sh                 or   ./compile_libs.sh /path/to/Vivado/2022.2
#
# Run it once. It takes a minute or two.
set -e
cd "$(dirname "$0")"
export LVDSIM_NVC_OPTS="${LVDSIM_NVC_OPTS:--M 128m}"   # unisim_VCOMP is a very large package

ROOT="${1:-$XILINX_VIVADO}"
U="$ROOT/data/vhdl/src/unisims"
M="$ROOT/data/vhdl/src/unimacro"
if [ ! -f "$U/unisim_VCOMP.vhd" ] || [ ! -f "$M/unimacro_VCOMP.vhd" ]; then
    echo "Vivado sources not found under '$ROOT' (looked for data/vhdl/src/unisims and unimacro)." >&2
    echo "Set XILINX_VIVADO or pass the Vivado installation folder." >&2
    exit 1
fi

vlib unisim
vcom -2008 -explicit -work unisim "$U/unisim_VPKG.vhd"
vcom -2008 -explicit -work unisim "$U/unisim_VCOMP.vhd"
# The retarget package replaces the component package with a superset: it must come before the primitives.
if [ -f "$U/unisim_retarget_VCOMP.vhd" ]; then
    vcom -2008 -explicit -work unisim "$U/unisim_retarget_VCOMP.vhd"
fi
for f in "$U"/primitive/*.vhd; do
    vcom -2008 -explicit -work unisim "$f"
done
if [ -d "$U/retarget" ]; then
    for f in "$U"/retarget/*.vhd; do
        vcom -2008 -explicit -work unisim "$f"
    done
fi

vlib unimacro
vcom -2008 -explicit -work unimacro -L unisim "$M/unimacro_VCOMP.vhd"
for f in "$M"/*_MACRO.vhd; do
    vcom -2008 -explicit -work unimacro -L unisim "$f"
done
echo "libraries ready in $(pwd)"
