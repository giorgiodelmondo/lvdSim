#!/bin/sh
# Compile the Xilinx unisim example, then open it in the lvdsim GUI with sim.do (live waveform).
#   ./run.sh            compile, then start the GUI; close the window to end the run
#   ./run.sh --batch    compile and simulate 2 us without a window; prints count and above, writes xilinx.fst
#   ./run.sh --help
# Run ./compile_unisim.sh once first.
# Needs vlib vlog vcom (and lvdsim-gui, or vsim, for the GUI) on PATH: see ../README.md section 1.
# LVDSIM_GUI_ARGS adds options to the GUI command line (for example --screenshot out.png for a headless run).
set -e
case "$1" in
    --help|-h) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
[ -d unisim ] || { echo "Run ./compile_unisim.sh first." >&2; exit 1; }
vlib work
vlog -work work src/edge_counter.v
vcom -2008 -work work -L unisim src/comparator_sync.vhd src/tb_xilinx.vhd
if [ "$BATCH" = 1 ]; then
    vsim -c -L unisim -do "run 2 us; puts [list count [examine /count] above [examine /above]]; quit" -lvdwave xilinx.fst work.tb_xilinx
else
    echo "Starting the GUI with sim.do; close the window to end the run."
    if [ "$GUI" = vsim ]; then vsim -L unisim -do sim.do work.tb_xilinx; else $GUI $LVDSIM_GUI_ARGS -L unisim -do sim.do work.tb_xilinx; fi
fi
