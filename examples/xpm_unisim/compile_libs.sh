#!/bin/sh
# Compile the Xilinx Verilog simulation models and the XPM library into ./unisim_v and ./xpm.
# The Xilinx sources are under Xilinx's licence and are not part of lvdsim: point the script at your Vivado.
#
#   export XILINX_VIVADO=/path/to/Vivado/2022.2
#   ./compile_libs.sh                 or   ./compile_libs.sh /path/to/Vivado/2022.2
#
# Run it once. The Verilog unisim models are one file per primitive; a few do not compile with NVC yet
# (see docs/verilog-coverage.md) and are skipped. Takes about a minute.
set -e
cd "$(dirname "$0")"
export LVDSIM_NVC_OPTS="${LVDSIM_NVC_OPTS:--M 128m}"

ROOT="${1:-$XILINX_VIVADO}"
U="$ROOT/data/verilog/src/unisims"
X="$ROOT/data/ip/xpm"
if [ ! -f "$U/FDRE.v" ] || [ ! -f "$X/xpm_cdc/hdl/xpm_cdc.sv" ]; then
    echo "Vivado sources not found under '$ROOT' (looked for data/verilog/src/unisims and data/ip/xpm)." >&2
    echo "Set XILINX_VIVADO or pass the Vivado installation folder." >&2
    exit 1
fi

# XPM: SystemVerilog modules plus the VHDL package that declares them as components
vlib xpm
vlog -sv -work xpm "$X/xpm_cdc/hdl/xpm_cdc.sv"
vlog -sv -work xpm "$X/xpm_memory/hdl/xpm_memory.sv"
vlog -sv -work xpm "$X/xpm_fifo/hdl/xpm_fifo.sv"
vcom -2008 -work xpm "$X/xpm_VCOMP.vhd"

# Verilog unisim models
vlib unisim_v
ok=0
skipped=""
for f in "$U"/*.v; do
    if vlog -work unisim_v "$f" > /dev/null 2>&1; then
        ok=$((ok + 1))
    else
        skipped="$skipped $(basename "$f" .v)"
    fi
done
echo "unisim_v: $ok models compiled, skipped:$skipped"
echo "libraries ready in $(pwd)"
