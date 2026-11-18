#!/bin/sh
# Compile the Xilinx XPM and Verilog unisim example, then open it in the lvdsim GUI with sim.do (live waveform).
#   ./run.sh            compile, then start the GUI; close the window to end the run
#   ./run.sh --batch    compile, then run both testbenches without a window (PASS lines)
#   ./run.sh --help
# Run ./compile_libs.sh once first; XILINX_VIVADO must name your Vivado installation (glbl.v).
# The GUI shows the VHDL design (work.xpm_demo with work.glbl as second top level).
# Needs vlib vlog vcom (and lvdsim-gui, or vsim, for the GUI) on PATH: see ../README.md section 1.
# LVDSIM_GUI_ARGS adds options to the GUI command line (for example --screenshot out.png for a headless run).
set -e
case "$1" in
    --help|-h) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --batch) BATCH=1 ;;
    "") BATCH=0 ;;
    *) echo "Unknown argument '$1' (try --help)." >&2; exit 2 ;;
esac
cd "$(dirname "$0")"
for t in vlib vlog vcom; do
    command -v $t >/dev/null 2>&1 || { echo "Cannot find '$t' on PATH. Set up the shell as in examples/README.md section 1, or put the lvdsim folder on PATH." >&2; exit 1; }
done
GUI=lvdsim-gui
if [ "$BATCH" = 1 ]; then
    command -v vsim >/dev/null 2>&1 || { echo "Cannot find 'vsim' on PATH (see examples/README.md section 1)." >&2; exit 1; }
elif ! command -v lvdsim-gui >/dev/null 2>&1; then
    command -v vsim >/dev/null 2>&1 || { echo "Cannot find 'lvdsim-gui' or 'vsim' on PATH. Set up the shell as in examples/README.md section 1, or put the lvdsim folder on PATH. './run.sh --batch' needs only the command line tools." >&2; exit 1; }
    GUI=vsim
fi
[ -d xpm ] && [ -d unisim_v ] || { echo "Run ./compile_libs.sh first." >&2; exit 1; }
GLBL="${XILINX_VIVADO:?set XILINX_VIVADO}/data/verilog/src/glbl.v"
export LVDSIM_NVC_OPTS="${LVDSIM_NVC_OPTS:--M 128m}"
vlib work
vlog -work work "$GLBL"
vlog -work work src/lut6_and.v src/tb_unisim.v
vcom -2008 -work work -L xpm -L unisim_v src/xpm_demo.vhd
if [ "$BATCH" = 1 ]; then
    echo "== VHDL design: IBUF, BUFG, FDRE, LUT6 and SRL16E from Verilog unisim, XPM CDC and FIFO"
    vsim -c -L xpm -L unisim_v -do "run -all" work.xpm_demo work.glbl
    echo "== Verilog testbench: unisim primitives and RAMB18E1"
    vsim -c -L unisim_v -do "run -all" work.tb_unisim work.glbl
else
    echo "Starting the GUI with sim.do; close the window to end the run."
    if [ "$GUI" = vsim ]; then vsim -L xpm -L unisim_v -do sim.do work.xpm_demo work.glbl; else $GUI $LVDSIM_GUI_ARGS -L xpm -L unisim_v -do sim.do work.xpm_demo work.glbl; fi
fi
