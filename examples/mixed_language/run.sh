#!/bin/sh
# Compile the mixed-language example, then open it in the lvdsim GUI with sim.do (live waveform).
#   ./run.sh            compile, then start the GUI; close the window to end the run
#   ./run.sh --batch    compile and simulate 2 us without a window, print the final values, write mixed.fst
#   ./run.sh --help
# Needs vlib vlog vcom (and lvdsim-gui, or vsim, for the GUI) on PATH: see ../README.md section 1.
# LVDSIM_GUI_ARGS adds options to the GUI command line (for example --screenshot out.png for a headless run).
set -e
case "$1" in
    --help|-h) sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
vlib work
vlog -work work src/avg_filter.v
vlog -sv -work work src/level_monitor.sv
vcom -2008 -work work src/adc_model.vhd src/tb_mixed.vhd
if [ "$BATCH" = 1 ]; then
    vsim -c -do "run 2 us; puts [list events [examine /events] avg [examine /avg]]; quit" -lvdwave mixed.fst work.tb_mixed
else
    echo "Starting the GUI with sim.do; close the window to end the run."
    if [ "$GUI" = vsim ]; then vsim -do sim.do work.tb_mixed; else $GUI $LVDSIM_GUI_ARGS -do sim.do work.tb_mixed; fi
fi
