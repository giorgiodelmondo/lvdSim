# Command-set compatibility

lvdsim accepts the command names and option spellings of the common commercial simulators for the everyday flow, translates them to
`nvc`, and leaves the rest alone. Nothing here is copied from any vendor; the behaviour comes from what scripts
rely on. Status values: **native** (NVC does it as is), **wrapped** (translated), **accepted** (parsed, no
effect), **unsupported** (clear error or warning), **fork** (needs a kernel change).

## Tools

One binary, `lvdsim-cli`, behaves as `vlib`, `vmap`, `vcom`, `vlog` or `vsim` depending on the name it is
started with. The build copies it under those names next to itself; `lvdsim-cli vcom ...` also works.
Unknown options produce a warning and are skipped. `-strict` (or `LVDSIM_STRICT=1`) makes them errors.

### Environment

| Variable | Meaning |
|----------|---------|
| `LVDSIM_NVC` | nvc executable. Default: `nvc/bin/nvc[.exe]` inside the installation (next to the tool or one directory up), then next to the tool, then `PATH`. |
| `LVDSIM_NVC_LIBDIR` | extra library directory for nvc (`-L`); for a build tree use `<build>/lib`. Default for a packaged nvc: `nvc/lib` of the installation. |
| `LVDSIM_NVC_OPTS` | extra global nvc options, split at spaces, for example `-M 128m` for very large packages such as Xilinx `unisim_VCOMP`. |
| `LVDSIM_NVC_DLLDIR` | directory with the DLLs nvc needs, put in front of `PATH` (MSYS2: `C:/msys64/mingw64/bin`). Default for a packaged nvc: `nvc/bin`. |
| `LVDSIM_TCL_DIR` | directory containing `lvdcompat.tcl`. Default: next to the tool, then the source tree. |
| `LVDSIM_STD` | default VHDL standard `93`, `02`, `08` or `19`. Default `08`. |
| `LVDSIM_STRICT` | if set, unsupported options are errors. |
| `LVDSIM_INI` | lvdsim.ini to read and write. Default `./lvdsim.ini`. |

### Libraries

A library is a directory. A name resolves through the `[Library]` section of `lvdsim.ini`, otherwise to a
directory of that name in the current directory. `vlib` creates the directory and the marker file NVC expects.
`vmap name path` records a mapping, `vmap name` shows it, `vmap` lists all. `-work lib` selects the library to
compile into (default `work`) and `-L lib` makes another one visible. A missing work library is an error that
names the fix (`vlib work`), as other simulators do.

### vlib, vmap

| Option | Status |
|--------|--------|
| `<name>...` | wrapped: creates `<name>/` with the NVC marker |
| `-type directory`, `-unix`, `-dos`, `-short` | accepted |
| vmap `-c`, `-del`, `-inifile` | accepted, no effect |

### vcom

| Option | Status |
|--------|--------|
| `-work lib`, `-work=lib`, `-L lib` | wrapped (`--work`, `--map`) |
| `-93`, `-2002`, `-2008`, `-2019` | wrapped (`--std`); the last one wins |
| `-87` | wrapped as `-93` with a warning |
| `-f file` | wrapped: words of the file are inserted, `//` and `#` comments, quotes group, nests |
| `-explicit` | wrapped (`--relaxed`: ambiguous operators resolve to the explicit declaration, needed by older Xilinx unisim files) |
| `-quiet -novopt -nocheck -check_synthesis -lint -pedanticerrors -suppress n -nowarn n -O0..-O5 -nologo` | accepted |
| anything else | warning (error with `-strict`) |

The default standard is VHDL-2008 (other simulators default to 2002; use `-2002` or `LVDSIM_STD=02`).

### vlog

| Option | Status |
|--------|--------|
| `-work`, `-L`, `-f` | as vcom |
| `-sv` | wrapped (`--keywords=1800-2017`) |
| `+define+A=1+B` | wrapped (`-D A=1 -D B`) |
| `+incdir+d1+d2` | wrapped (`-I d1 -I d2`) |
| `-quiet -novopt -incr -mfcu -lint -pedanticerrors -timescale x -suppress n -nowarn n -vopt -v2k5 -O0..-O5`, `+acc`, `+nowarn*` | accepted |
| `-y dir`, `-v file`, `-sv_lib` | unsupported |

How much Verilog and SystemVerilog is accepted is up to the NVC build (experimental upstream).

### vsim

With `-c` or `-batch` vsim runs in batch mode. Without it vsim opens the GUI (`lvdsim-gui`, which must be next to the tools or named by `LVDSIM_GUI`) and keeps the simulator as a child process; the same options apply.

| Option | Status |
|--------|--------|
| `[lib.]top` | wrapped: elaborate `top` from that library. `(arch)` is ignored with a warning |
| `[lib.]top [lib.]unit ...` | wrapped: several design units are elaborated together as top levels (`nvc -e top unit`), e.g. `vsim work.top work.glbl` for designs that use Xilinx Verilog models. All units must be in one library; the first one is the root of the signal paths (`/clk`); the shell cannot see the signals of the later units (`/glbl/GSR`), but the models that read them work. Needs the fork (patch 0006); see `docs/verilog-coverage.md` |
| `-c`, `-batch` | wrapped: batch mode |
| `-do "cmds"` or `-do file` | wrapped: a string is written to a temporary script; scripts run after `lvdcompat.tcl` |
| no `-do` | wrapped: interactive `nvc -i` shell |
| `-lib lib`, `-L lib` | wrapped |
| `-g name=value`, `-G name=value` | wrapped (`-e -g`) |
| `-wlf file` | wrapped: waveform written as `file.fst` (`.wlf` is replaced); needs the fork |
| `-lvdwave file` | extension: waveform file written exactly as named; needs the fork |
| `-gui` or no `-c` | wrapped: opens the GUI; `-do` becomes a startup script, waveform file options are ignored (the GUI records a live stream) |
| `-lvdlive file` | extension: write the live waveform stream (`--format=live`) in batch mode; needs the fork |
| `-sdfmin -sdftyp -sdfmax -pli -foreign` (and their argument) | unsupported, warning |
| `-l file` | accepted with a warning: redirect the output instead |
| `-t -quiet -novopt -voptargs=* -sv_seed -sv_lib -msgmode -title -onfinish -coverage -assertdebug -nolog -wlfdeleteonquit -nowlf -displaymsgmode -suppress` | accepted |

The exit status of `vsim -c` is the status of the script: `quit -code N` exits with N. A script that ends
without `quit` ends the session (other simulators stay at their prompt).

## Simulator commands (`tcl/compat/lvdcompat.tcl`)

| Command | Status | Notes |
|---------|--------|-------|
| `run <n> <unit>`, `run <n><unit>`, `run 1.5 us` | wrapped | decimal values and units fs, ps, ns, us, ms, sec |
| `run` | wrapped | 100 ns, the usual default run length |
| `run -all` | wrapped | until no events are left or the design finishes |
| `run -continue` | accepted | |
| `run -step -next -over -out` | fork | stepping needs kernel support |
| `force [-freeze\|-drive\|-deposit] sig value` | wrapped | based numbers (`16#FF`, `2#101`, `0xFF`) become integers |
| `force` with a time list, `-repeat`, `-cancel` | wrapped | the first value is applied now, with a warning |
| `force` without arguments | native | lists forced signals |
| `noforce sig` | native | |
| `examine [-radix r \| -hex ...] sig...`, `exa` | native | |
| `find signals [-r] pattern`, `describe`, `echo` | native | |
| `quit [-f] [-force] [-sim] [-code n]` | wrapped | `exit -code n` |
| `do file` | wrapped | `source`, with `argv` set |
| `quietly cmd` | wrapped | |
| `add wave [-r] [-radix r] [-color c] [-label l] [-group g] [-divider t] [-format analog] [-min x] [-max y] pattern...` | wrapped | in the GUI it fills the wave window (patterns like `/dut/*`, `/tb/dut/clk`; a vector named `count[7:0]` matches `count`); no effect in batch mode; lvdsim extensions: `-group a/b` nests (one level per `/`), `-group g` alone declares an empty group, `-collapse`, `-bits`, `-vbus`, see display-format.md |
| `delete wave *` | wrapped | GUI: removes all rows; batch: no effect |
| `add list`, `add log`, `log`, `wave`, `view`, `radix`, `configure`, `transcript` | accepted | no effect |
| `onerror`, `onbreak`, `onElabError` | accepted | no effect yet |
| `restart` | fork | listed by NVC but not implemented |
| `step [-delta\|-time]`, `breakpoint add\|list\|delete`, `processes` | fork | native commands of the kernel branch (old patches 0004 and 0005, `kernel/PATCHES.md`); `-json` for the GUI |
| `when`, `bp` | fork | clear error |
| `vcd ...` | unsupported | use `-wlf` or `-lvdwave` |

## Compatibility notes
- Vendor `.wlf` files are not produced. Waveforms are FST files for the lvdsim viewer or GTKWave.
- `force` on vectors takes plain integers; bit string literals are not translated yet.
- Time lists, `when` conditions and breakpoints are not implemented; they need further kernel work.

## GUI (lvdsim-gui)

Start it with `vsim [options] design` (no `-c`) or `lvdsim-gui [options] design`. It shows the wave window, an
objects browser, the transcript and run controls. Typed commands go to the simulator, except:
`restart` starts over from time zero and keeps the wave rows (the startup `-do` script is not run again),
`quit` or `exit` close the window, and `vlib`, `vmap`, `vcom` and `vlog` run the tools and print their output,
so edit, `vcom`, `restart` works. Headless options for scripts and tests: `--send "cmd"`, `--screenshot f.png`,
`--transcript f.txt`.

### Layout and the Library dock (GUI)

- Default layout: **Library** and **Sim** are tabs of the top left dock, **Objects** is below it, the waveform fills the middle,
  **Source** is on the right, **Transcript** and **Processes** | **Breakpoints** are at the bottom. Library is in front until a design is
  loaded, then Sim. View > Reset layout restores this arrangement; the layout is otherwise saved in the session file.
- The **Library** dock lists libraries and design units. Double-click (or the arrow) only expands or collapses a row. To simulate a unit
  right-click it and choose **Simulate** (same as `vsim work.unit`), or type `vsim` as usual. Refresh rescans the libraries.

### Processes, Source and breakpoints (GUI)

- **Processes** dock: name, path, state (`ready`, `delayed`, `waiting`) and `file:line` of every process, read with
  `processes -json` after every `run`, `step` or breakpoint stop, after the startup script and with the Refresh
  button. Selecting a row shows the file in the **Source** dock with an arrow on its line (green dots mark the lines
  of processes that are `ready`); double-click also raises the dock. Relative file names are looked up in the
  current directory, so analyse with absolute paths if the GUI starts elsewhere.
- **Source** dock: read-only text with line numbers, VHDL colours and a find bar (Ctrl+F, F3, Shift+F3).
- **Breakpoints** dock (tabbed with Processes): time breakpoints only, one shot, absolute time. Add takes a time and a unit
  (fs to ms; decimals such as 0.5 us are sent as 500 ns), Delete and Delete all remove them. A hit is shown in the
  status bar (`Breakpoint 1 at 50 ns`) and as a `# Breakpoint ...` line in the transcript. Typing
  `breakpoint add 50 ns` works too and the list follows.
- **Step delta** and **Step time** (toolbar and Simulation menu) send `step -delta` and `step -time`: one simulation
  cycle, or the rest of the time step. These are simulation cycles, not source lines.
- `run` and `step` typed in the transcript or sent by the GUI go to the kernel with `-json` inserted (the transcript
  shows `run -json 10 ns`), so the GUI can read the stop reason; the reply itself is replaced by a one-line summary.
  Other `-json` replies typed by the user are printed as they are. A reply that is not valid JSON or has an unknown
  `"v"` is shown in the transcript and otherwise ignored.
- The layout (which docks are visible and where) is saved in the session file. The layout format has a version;
  a layout saved before these docks existed is ignored once and the default layout is used.
- Headless: `lvdsim-gui ... --process NAME --screenshot f.png` selects that process and shows its source;
  `--settings f.ini` uses a separate session file.
