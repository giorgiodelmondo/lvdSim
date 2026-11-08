#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Compile the Vivado and Quartus (Altera/Intel) simulation libraries with the lvdsim tools into one output
# directory and write a lvdsim.ini that maps them. See docs/libraries.md.
#
#   compile_libs.sh --out DIR [--vivado DIR] [--quartus DIR] [--only LIST] [--quiet]
#
#   --vivado DIR   Vivado installation (default $XILINX_VIVADO). Read only, never copied.
#   --quartus DIR  Quartus installation (default $QUARTUS_ROOTDIR, then $ALTERA_ROOT): the version folder or its
#                  quartus subfolder, whichever contains eda/sim_lib. Read only, never copied.
#   --out DIR      output directory (default ./vivado_libs, or ./altera_libs for an Altera only run);
#                  gets one folder per library, lvdsim.ini, logs/
#   --only LIST    comma list. Xilinx: unisim, unimacro, unisim_verilog, xpm (default unisim,unimacro).
#                  Altera: altera_mf, lpm, sgate, altera_primitives (VHDL, the default set), altera_lnsim,
#                  altera_verilog, cyclone10gx, twentynm, tennm (device atoms, need altera_lnsim).
#                  Without --only: the Xilinx default, or the Altera default when only --quartus is given.
#                  "secureip-skip" is accepted for clarity: encrypted models are never compiled.
#   --quiet        print only the summary
#
# vlib, vmap, vcom and vlog must be on PATH (the lvdsim tools); LVDSIM_NVC* as in docs/command-compat.md.
# Exit status is non-zero if unisim or unimacro (the default set) fail, or on a usage or source error.
# A failing file never stops the run. Files containing a `protect block are skipped and counted.

VIVADO="$XILINX_VIVADO"
QUARTUS="${QUARTUS_ROOTDIR:-$ALTERA_ROOT}"
OUT=""
ONLY=""
GOTV=0; GOTQ=0
QUIET=0
usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; }
while [ $# -gt 0 ]; do
    case "$1" in
        --vivado) VIVADO="$2"; GOTV=1; shift 2 ;;
        --quartus) QUARTUS="$2"; GOTQ=1; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --only) ONLY="$2"; shift 2 ;;
        --jobs|-j) echo "note: --jobs ignored (files of one library are compiled one after the other)" >&2; shift 2 ;;
        --quiet) QUIET=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option '$1'" >&2; usage >&2; exit 2 ;;
    esac
done

XDEF=unisim,unimacro
ADEF=altera_mf,lpm,sgate,altera_primitives
if [ -z "$ONLY" ]; then
    if [ $GOTQ = 1 ] && [ $GOTV = 0 ]; then ONLY=$ADEF
    elif [ $GOTQ = 1 ]; then ONLY=$XDEF,$ADEF
    else ONLY=$XDEF; fi
fi
if [ -z "$OUT" ]; then
    case ",$ONLY," in *,unisim*|*,unimacro,*|*,xpm,*) OUT=./vivado_libs ;; *) OUT=./altera_libs ;; esac
fi
WANT=" "
for w in $(echo "$ONLY" | tr ',' ' '); do
    case "$w" in
        unisim|unimacro|unisim_verilog|xpm) WANT="$WANT$w " ;;
        altera_mf|lpm|sgate|altera_primitives|altera_lnsim|altera_verilog|cyclone10gx|twentynm|tennm) WANT="$WANT$w " ;;
        secureip-skip) ;;
        *) echo "unknown library '$w' in --only (unisim, unimacro, unisim_verilog, xpm, altera_mf, lpm, sgate, altera_primitives, altera_lnsim, altera_verilog, cyclone10gx, twentynm, tennm)" >&2; exit 2 ;;
    esac
done
want() { case "$WANT" in *" $1 "*) return 0 ;; esac; return 1; }
[ "$WANT" = " " ] && { echo "--only selects nothing" >&2; exit 2; }
for t in vlib vmap vcom vlog; do
    command -v $t > /dev/null 2>&1 || { echo "$t not found on PATH (the lvdsim tools)" >&2; exit 2; }
done
NEEDX=0; NEEDA=0
for w in unisim unimacro unisim_verilog xpm; do want $w && NEEDX=1; done
for w in altera_mf lpm sgate altera_primitives altera_lnsim altera_verilog cyclone10gx twentynm tennm; do want $w && NEEDA=1; done
if [ $NEEDX = 1 ]; then
    [ -d "$VIVADO" ] || { echo "Vivado folder '$VIVADO' not found: use --vivado or set XILINX_VIVADO" >&2; exit 2; }
fi
ASIM=""
if [ $NEEDA = 1 ]; then
    for c in "$QUARTUS/eda/sim_lib" "$QUARTUS/quartus/eda/sim_lib"; do [ -d "$c" ] && { ASIM="$c"; break; }; done
    [ -n "$ASIM" ] || { echo "Quartus simulation sources not found under '$QUARTUS' (looked for eda/sim_lib): use --quartus or set QUARTUS_ROOTDIR" >&2; exit 2; }
fi

mkdir -p "$OUT/logs" || exit 2
OUT=$(cd "$OUT" && pwd -W 2>/dev/null || pwd)       # absolute; drive-letter form under Git Bash
export LVDSIM_INI="$OUT/lvdsim.ini"
export LVDSIM_NVC_OPTS="${LVDSIM_NVC_OPTS:--M 128m}"   # unisim_VCOMP is a very large package
VHDL="$VIVADO/data/vhdl/src"
VLOG="$VIVADO/data/verilog/src"
XPM="$VIVADO/data/ip/xpm"
say() { [ $QUIET = 1 ] || echo "$@"; }

SUMMARY=""
RC=0
OK=0; FAIL=0; SKIP=0; FAILED=""; LOG=/dev/null; LIBNAME=""

newlib() {      # name: create the library folder and map it in lvdsim.ini
    LIBNAME=$1; OK=0; FAIL=0; SKIP=0; FAILED=""
    LOG="$OUT/logs/$1.log"; : > "$LOG"
    vlib "$OUT/$1" >> "$LOG" 2>&1 && vmap "$1" "$OUT/$1" >> "$LOG" 2>&1 || { echo "cannot create library $1" >&2; FAIL=$((FAIL + 1)); }
}
cfile() {       # tool, file, options...: compile one file, count the result
    tool=$1; f=$2; shift 2
    case "$f" in *_ncrypt.*) SKIP=$((SKIP + 1)); say "  skipped (encrypted, *_ncrypt): $(basename "$f")"; return ;; esac
    if grep -q '`protect' "$f" 2> /dev/null; then
        SKIP=$((SKIP + 1)); say "  skipped (encrypted, \`protect): $(basename "$f")"; return
    fi
    echo "== $tool $f" >> "$LOG"
    if $tool "$@" "$f" >> "$LOG" 2>&1; then
        OK=$((OK + 1))
    else
        FAIL=$((FAIL + 1)); FAILED="$FAILED $(basename "$f")"; say "  FAILED: $(basename "$f") (see $LOG)"
    fi
}
finish() {      # status of the library just compiled: $1 = 1 when failures count for the exit status
    T=$(( $(date +%s) - T0 ))
    SUMMARY="$SUMMARY$(printf '%-18s %8s %8s %8s %6ss' "$LIBNAME" "$OK" "$FAIL" "$SKIP" "$T")
"
    [ -n "$FAILED" ] && echo "$LIBNAME failed:$FAILED" >> "$OUT/logs/failed.txt"
    [ "$1" = 1 ] && [ $FAIL -gt 0 ] && RC=1
    say "$LIBNAME: $OK ok, $FAIL failed, $SKIP skipped in ${T}s"
}
need() { [ -f "$1" ] || { echo "source not found: $1" >&2; return 1; }; }

: > "$OUT/logs/failed.txt"

if want unisim; then
    T0=$(date +%s); U="$VHDL/unisims"
    if need "$U/unisim_VCOMP.vhd"; then
        say "unisim (VHDL)"
        newlib unisim
        cfile vcom "$U/unisim_VPKG.vhd" -2008 -explicit -work unisim
        cfile vcom "$U/unisim_VCOMP.vhd" -2008 -explicit -work unisim
        # retarget_VCOMP replaces the component package by a superset: before the primitives
        [ -f "$U/retarget_VCOMP.vhd" ] && cfile vcom "$U/retarget_VCOMP.vhd" -2008 -explicit -work unisim
        [ -f "$U/unisim_retarget_VCOMP.vhd" ] && cfile vcom "$U/unisim_retarget_VCOMP.vhd" -2008 -explicit -work unisim
        for f in "$U"/primitive/*.vhd; do [ -f "$f" ] && cfile vcom "$f" -2008 -explicit -work unisim; done
        for f in "$U"/retarget/*.vhd; do [ -f "$f" ] && cfile vcom "$f" -2008 -explicit -work unisim; done
        finish 1
    else RC=1; fi
fi

if want unimacro; then
    T0=$(date +%s); M="$VHDL/unimacro"
    if need "$M/unimacro_VCOMP.vhd"; then
        say "unimacro (VHDL, needs unisim)"
        [ -d "$OUT/unisim" ] || echo "warning: no unisim library in $OUT; unimacro needs it (add unisim to --only)" >&2
        newlib unimacro
        cfile vcom "$M/unimacro_VCOMP.vhd" -2008 -explicit -work unimacro -L unisim
        for f in "$M"/*_MACRO.vhd; do [ -f "$f" ] && cfile vcom "$f" -2008 -explicit -work unimacro -L unisim; done
        finish 1
    else RC=1; fi
fi

if want unisim_verilog; then
    T0=$(date +%s); U="$VLOG/unisims"
    if need "$U/FDRE.v"; then
        say "unisim_v (Verilog models)"
        newlib unisim_v
        for f in "$U"/*.v; do [ -f "$f" ] && cfile vlog "$f" -work unisim_v; done
        finish 0
    else RC=1; fi
fi

if want xpm; then
    T0=$(date +%s)
    if need "$XPM/xpm_cdc/hdl/xpm_cdc.sv"; then
        say "xpm (SystemVerilog + component package)"
        newlib xpm
        cfile vlog "$XPM/xpm_cdc/hdl/xpm_cdc.sv" -sv -work xpm
        cfile vlog "$XPM/xpm_memory/hdl/xpm_memory.sv" -sv -work xpm
        cfile vlog "$XPM/xpm_fifo/hdl/xpm_fifo.sv" -sv -work xpm
        cfile vcom "$XPM/xpm_VCOMP.vhd" -2008 -work xpm
        finish 0
    else RC=1; fi
fi

# ---- Altera / Intel Quartus (sources: $ASIM = <quartus>/eda/sim_lib; read only, never copied) ----
asrc() { [ -f "$ASIM/$1" ] || { echo "source not found: $ASIM/$1" >&2; return 1; }; }
AOPT="-2008 -explicit"

if want altera_mf; then
    T0=$(date +%s)
    if asrc altera_mf.vhd; then
        say "altera_mf (VHDL megafunctions)"
        newlib altera_mf
        cfile vcom "$ASIM/altera_mf_components.vhd" $AOPT -work altera_mf
        cfile vcom "$ASIM/altera_mf.vhd" $AOPT -work altera_mf
        finish 1
    else RC=1; fi
fi

if want lpm; then
    T0=$(date +%s)
    if asrc 220model.vhd; then
        say "lpm (VHDL 220pack + 220model)"
        newlib lpm
        cfile vcom "$ASIM/220pack.vhd" $AOPT -work lpm
        cfile vcom "$ASIM/220model.vhd" $AOPT -work lpm
        finish 1
    else RC=1; fi
fi

if want sgate; then
    T0=$(date +%s)
    if asrc sgate.vhd; then
        say "sgate (VHDL, needs lpm)"
        [ -d "$OUT/lpm" ] || echo "warning: no lpm library in $OUT; sgate needs it (add lpm to --only)" >&2
        newlib sgate
        cfile vcom "$ASIM/sgate_pack.vhd" $AOPT -work sgate
        cfile vcom "$ASIM/sgate.vhd" $AOPT -work sgate -L lpm
        finish 1
    else RC=1; fi
fi

if want altera_primitives; then
    T0=$(date +%s)
    if asrc altera_primitives.vhd; then
        say "altera_primitives (VHDL)"
        newlib altera_primitives
        cfile vcom "$ASIM/altera_primitives_components.vhd" $AOPT -work altera_primitives
        cfile vcom "$ASIM/altera_primitives.vhd" $AOPT -work altera_primitives
        finish 1
    else RC=1; fi
fi

if want altera_lnsim; then
    T0=$(date +%s)
    if asrc altera_lnsim_components.vhd; then
        say "altera_lnsim (VHDL components + SystemVerilog models)"
        newlib altera_lnsim
        cfile vcom "$ASIM/altera_lnsim_components.vhd" -2008 -work altera_lnsim
        [ -f "$ASIM/altera_lnsim.sv" ] && cfile vlog "$ASIM/altera_lnsim.sv" -sv -work altera_lnsim
        finish 0
    else RC=1; fi
fi

for fam in cyclone10gx twentynm tennm; do
    if want $fam; then
        T0=$(date +%s)
        if asrc ${fam}_atoms.vhd; then
            say "$fam (VHDL device atoms, needs altera_lnsim)"
            [ -d "$OUT/altera_lnsim" ] || echo "warning: no altera_lnsim library in $OUT; $fam needs it (add altera_lnsim to --only)" >&2
            newlib $fam
            cfile vcom "$ASIM/${fam}_atoms.vhd" $AOPT -work $fam -L altera_lnsim
            [ -f "$ASIM/${fam}_components.vhd" ] && cfile vcom "$ASIM/${fam}_components.vhd" $AOPT -work $fam -L altera_lnsim
            finish 0
        else RC=1; fi
    fi
done

if want altera_verilog; then
    say "altera_verilog (altera_ver, sgate_ver, lpm_ver, altera_mf_ver)"
    for pair in altera_ver:altera_primitives.v sgate_ver:sgate.v lpm_ver:220model.v altera_mf_ver:altera_mf.v; do
        lib=${pair%%:*}; f=${pair#*:}
        if [ -f "$ASIM/$f" ]; then
            T0=$(date +%s)
            newlib $lib
            cfile vlog "$ASIM/$f" -work $lib
            finish 0
        else echo "source not found: $ASIM/$f" >&2; RC=1; fi
    done
fi

echo
printf '%-18s %8s %8s %8s %7s\n' library "files ok" failed skipped time
printf '%s' "$SUMMARY"
echo "output: $OUT   (lvdsim.ini written; logs in $OUT/logs)"
[ -s "$OUT/logs/failed.txt" ] && { cut -c1-200 "$OUT/logs/failed.txt"; echo "(full lists: $OUT/logs/failed.txt)"; }
if want altera_verilog || want altera_lnsim; then
    echo "Altera Verilog models: altera_mf_ver, lpm_ver and the altera_lnsim .sv part use constructs nvc does not analyse yet (docs/libraries.md)."
fi
if want unisim_verilog || want xpm; then
    echo "Verilog designs also need glbl: vlog -work work \"$VLOG/glbl.v\" and 'vsim ... work.top work.glbl'."
fi
exit $RC
