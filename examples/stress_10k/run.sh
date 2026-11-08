#!/bin/sh
# Compile the stress design (about 10 000 signals), then open it in the lvdsim GUI with sim.do. Run ./compile_libs.sh once first.
#   ./run.sh [N_LANES] [RUN_TIME]            GUI: default 118 lanes and a 200 us run, then the window stays open
#   ./run.sh --batch [N_LANES] [RUN_TIME]    no window: default 118 lanes and 20 ms, prints the signal count and the checksum
#   ./run.sh --help                          examples: ./run.sh 20 1ms     ./run.sh --batch 500 20ms
# Set WAVE=1 with --batch to also write stress.fst (large: see README.md). The GUI records its own waveform.
# The full 118-lane design is heavy in the GUI (10 039 signals): keep GUI runs short, or lower N_LANES.
# Needs vlib vlog vcom (and lvdsim-gui, or vsim, for the GUI) on PATH: see ../README.md section 1.
# LVDSIM_GUI_ARGS adds options to the GUI command line (for example --screenshot out.png for a headless run).
set -e
BATCH=0
LANES=""
TIME=""
for a in "$@"; do
    case "$a" in
        --help|-h) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        --batch) BATCH=1 ;;
        -*) echo "Unknown argument '$a' (try --help)." >&2; exit 2 ;;
        *) if [ -z "$LANES" ]; then LANES="$a"; elif [ -z "$TIME" ]; then TIME="$a"; else echo "Too many arguments (try --help)." >&2; exit 2; fi ;;
    esac
done
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
export LVDSIM_NVC_OPTS="${LVDSIM_NVC_OPTS:--M 128m}"
[ -d unisim ] && [ -d unimacro ] || { echo "Run ./compile_libs.sh first." >&2; exit 1; }
LANES="${LANES:-118}"
vlib work
vlog -work work src/lane_acc.v
vlog -sv -work work src/lane_mon.sv
vcom -2008 -explicit -work work -L unisim -L unimacro src/lane_lfsr.vhd src/lane_cell.vhd src/stress_top.vhd
if [ "$BATCH" = 1 ]; then
    TIME="${TIME:-20 ms}"
    WAVEOPT=""
    [ -n "$WAVE" ] && WAVEOPT="-lvdwave stress.fst"
    START=$(date +%s)
    vsim -c -L unisim -L unimacro -g N_LANES="$LANES" $WAVEOPT -do "puts \"signals: [llength [find signals -r /*]]\"; run $TIME; puts [list checksum [examine /checksum] event_total [examine /event_total]]; quit" work.stress_top
    echo "wall time: $(( $(date +%s) - START )) s"
else
    # sim.do runs 200 us unless LVDSIM_RUN names another time.
    [ -n "$TIME" ] && export LVDSIM_RUN="$TIME"
    echo "Starting the GUI with sim.do ($LANES lanes, run ${LVDSIM_RUN:-200 us}); close the window to end the run."
    [ "$LANES" -gt 20 ] 2>/dev/null && echo "Note: this design is heavy in the GUI; use './run.sh 20' for a lighter session or './run.sh --batch' for the full run."
    if [ "$GUI" = vsim ]; then vsim -L unisim -L unimacro -g N_LANES="$LANES" -do sim.do work.stress_top; else $GUI $LVDSIM_GUI_ARGS -L unisim -L unimacro -g N_LANES="$LANES" -do sim.do work.stress_top; fi
fi
