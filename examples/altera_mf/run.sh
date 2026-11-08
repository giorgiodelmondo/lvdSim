#!/bin/sh
# Compile the Altera megafunction demo, then open it in the lvdsim GUI with sim.do (live waveform).
#   ./run.sh            compile, then start the GUI; close the window to end the run
#   ./run.sh --batch    compile and run without a window; prints sum=61 cnt=3 ram=90 and PASS
#   ./run.sh --help
# Needs altera_mf and lpm from your own Quartus installation:
#   scripts/libs/compile_libs.sh --quartus /d/cad/intelFPGA_pro/24.2 --out ./libs      (a few seconds)
#   LVDSIM_INI=./libs/lvdsim.ini ./run.sh
# Needs vlib vcom (and lvdsim-gui, or vsim, for the GUI) on PATH: see ../README.md section 1.
# LVDSIM_GUI_ARGS adds options to the GUI command line (for example --screenshot out.png for a headless run).
set -e
case "$1" in
    --help|-h) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --batch) BATCH=1 ;;
    "") BATCH=0 ;;
    *) echo "Unknown argument '$1' (try --help)." >&2; exit 2 ;;
esac
cd "$(dirname "$0")"
for t in vlib vcom; do
    command -v $t >/dev/null 2>&1 || { echo "Cannot find '$t' on PATH. Set up the shell as in examples/README.md section 1, or put the lvdsim folder on PATH." >&2; exit 1; }
done
GUI=lvdsim-gui
if [ "$BATCH" = 1 ]; then
    command -v vsim >/dev/null 2>&1 || { echo "Cannot find 'vsim' on PATH (see examples/README.md section 1)." >&2; exit 1; }
elif ! command -v lvdsim-gui >/dev/null 2>&1; then
    command -v vsim >/dev/null 2>&1 || { echo "Cannot find 'lvdsim-gui' or 'vsim' on PATH. Set up the shell as in examples/README.md section 1, or put the lvdsim folder on PATH. './run.sh --batch' needs only the command line tools." >&2; exit 1; }
    GUI=vsim
fi
[ -n "$LVDSIM_INI" ] || [ -f lvdsim.ini ] || echo "Note: LVDSIM_INI is not set; the libraries altera_mf and lpm must be mapped in a lvdsim.ini (see above)." >&2
vlib work
vcom -2008 -explicit -L altera_mf -L lpm src/alt_demo.vhd
if [ "$BATCH" = 1 ]; then
    vsim -c -L altera_mf -L lpm -do "run -all" work.alt_demo
else
    echo "Starting the GUI with sim.do; close the window to end the run."
    if [ "$GUI" = vsim ]; then vsim -L altera_mf -L lpm -do sim.do work.alt_demo; else $GUI $LVDSIM_GUI_ARGS -L altera_mf -L lpm -do sim.do work.alt_demo; fi
fi
