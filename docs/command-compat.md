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
| `+cover`, `+cover=<letters>` | accepted with a note: nvc selects coverage when the design is elaborated, so nothing is recorded at compile time and no per-library setting is kept; the note names the equivalent `vsim -coverage -lvdcover <letters>` |
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
| `+cover`, `+cover=<letters>` | as vcom |
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
| `-g<name>=<value>`, `-G<name>=<value>` (also `-g name=value`) | wrapped, also in the GUI (see "Generic overrides" below): a plain `-gNAME` becomes `nvc -e --generic-default=*.NAME=...` (every instance without a generic-map value, and the top level), a plain `-GNAME` becomes `-g*.NAME=...` (every instance), a name with a path becomes `-g<path>.NAME=...` (that instance); needs the fork (change set 10). For the same name `-G` wins over `-g`, otherwise the last one wins. Hierarchical names `/top/inst/NAME` or `inst/NAME` become `inst.NAME`, generate iterations included (`/tb/gen(1)/u/NAME` becomes `gen(1).u.NAME`). Values: integers, reals, enumeration literals, `x"A5"` as nvc reads them; `"text"` loses its outer quotes (`""` inside is one quote); based integers `16#FF#` are converted to decimal. A malformed override is an error, a value of the wrong type fails the elaboration |
| `-wlf file` | wrapped: waveform written as `file.fst` (`.wlf` is replaced); needs the fork |
| `-lvdwave file` | extension: waveform file written exactly as named; needs the fork |
| `-gui` or no `-c` | wrapped: opens the GUI; `-do` becomes a startup script, waveform file options are ignored (the GUI records a live stream) |
| `-lvdlive file` | extension: write the live waveform stream (`--format=live`) in batch mode; needs the fork |
| `-lvdarrays` | extension: put arrays of composite elements (memories, arrays of integers) into the waveform (`nvc --dump-arrays`), for the Memory window; the GUI adds it when Simulation > Record memories is on. Warning without a waveform option in batch mode |
| `-coverage` | wrapped: `nvc -e --cover=statement,branch,expression,toggle,fsm-state,functional --cover-file=<top>.ncdb ...`; the database is named after the first top level in lower case and written in the working directory. Also in the GUI: Simulation > Collect coverage adds `-coverage` and the GUI shares this translation. Read, merge and report databases with the GUI (File > Open coverage) or `nvc --cover-export`/`--cover-report`. The shell (`-do`, `-c`, the GUI) writes the run-time counts when it ends and on `coverage save`. The GUI gives nvc the absolute name, and uses its data folder when the working directory is not writable. nvc instruments VHDL only: Verilog modules have no coverage |
| `-lvdcover kinds` | extension: with `-coverage`, the kinds to collect: words `statement branch expression toggle fsm-state functional` (also `fsm`, `all`), comma separated, or letters `s b c`/`e f t` as in `sbt`. Unknown words are ignored; nothing known left gives a warning and every kind. Warning without `-coverage` |
| `-lvdcoverfile file` | extension: with `-coverage`, the database file instead of `<top>.ncdb` (`--cover-file`). Warning without `-coverage` |
| `-sdfmin [<instance>=]<file>`, `-sdftyp ...`, `-sdfmax ...` | wrapped: `nvc -e --sdf=min=[<instance>=]<file>` (`typ=`, `max=`), in the order given, several allowed; needs the fork (change set 38, `kernel/upstream/38-sdf-annotation`). The instance is a path such as `/tb/dut` or `dut` (the top-level name may be included); the cell instances of the file are relative to it. `.sdf` and `.sdo` files. VHDL: VITAL timing generics (`IOPATH`, `PORT`, `INTERCONNECT` and the timing checks); Verilog: timing check limits only, module path delays are not annotated yet (warning). See [sdf.md](sdf.md) |
| `-pli -foreign` (and their argument) | unsupported, warning |
| `-l file` | accepted with a warning: redirect the output instead |
| `-t -quiet -novopt -voptargs=* -sv_seed -sv_lib -msgmode -title -onfinish -assertdebug -nolog -wlfdeleteonquit -nowlf -displaymsgmode -suppress` | accepted |

#### Generic overrides

nvc has `-gNAME=VALUE` and, in the fork (change set 10, `kernel/upstream/10-generic-override-paths`), `--generic-default=NAME=VALUE`
and patterns. How lvdsim maps the vsim options:

| vsim | nvc | Effect |
|------|-----|--------|
| `-gNAME=V` | `--generic-default=*.NAME=V` | the top-level generic and every instance generic NAME that has no value in a generic map (also `NAME => open`) |
| `-GNAME=V` | `-g*.NAME=V` | the top-level generic and every instance generic NAME, replacing generic-map values |
| `-g/top/a/b/NAME=V`, `-G/top/a/b/NAME=V`, `-ga/b/NAME=V` | `-ga.b.NAME=V` | that instance, replacing a generic-map value |
| `-g/top/NAME=V` | `-gNAME=V` | the top-level generic only |

What the kernel does with a name:

- The path is the dotted list of labels below the top level: instances, blocks and generate statements, a for-generate
  iteration with its index (`gen(2).u.W`). The implicit level of a component instantiation is not part of it (`c.sub.W` for
  `/top/c/sub`). A path with only the instance labels (`u.W` for `/top/gen(2)/u`) still works and applies to the instance in every
  generate iteration.
- Every override applies to every instance it matches (older kernels used each override once, so in a `for ... generate` only the
  first iteration got the value). `generic value for NAME not used` means it matched nothing.
- `*` in a name is a pattern over the whole path; `*.NAME` also matches the top-level generic NAME. A full name wins over a pattern,
  otherwise the override given last wins; a `-g` override wins over `--generic-default`.
- A plain name without a path given to nvc directly (`nvc -e -gW=8`) still sets only the top-level generic, as in upstream nvc.
- Shells of the MSYS family (Git Bash, `make` under MSYS2) rewrite an argument like `-g/tb/u/WIDTH=8` into a Windows
  path before vsim sees it. vsim recognises the result and stops with a hint: set `MSYS_NO_PATHCONV=1` or write the
  dotted form `-gu.WIDTH=8`. cmd.exe and PowerShell pass the argument unchanged.
- Verilog module parameters are not overridden by either option (as in upstream nvc).

| Command | Options covered | Status | Notes |
|---------|-----------------|--------|-------|
| `vsim -g<name>=<value>` without a path | every instance without a generic-map value, and the top level | fork (10) | `--generic-default=*.NAME` |
| `vsim -G<name>=<value>` without a path | every instance and the top level | fork (10) | `-g*.NAME` |
| `vsim -g/-G` with a path through generate or block statements | that instance | fork (10) | generate (`gen(1)`) and block labels are part of the kernel path; one override serves every match |

The exit status of `vsim -c` is the status of the script: `quit -code N` exits with N. A script that ends
without `quit` ends the session (other simulators stay at their prompt).

### Project commands (lvdsim's own)

These have no counterpart in the classic command set; they work on a project file (`docs/projects.md`). Options are table driven
(`tools/src/project_cli.cpp`).

| Command | Options | Notes |
|---------|---------|-------|
| `lvdsim-cli compile <p.lvdproj> [FILE...]` | `--all`, `--dry-run` | analyses the out-of-date files and their dependents in dependency order (with FILEs: those and their dependents); exit 1 if a file failed |
| `lvdsim-cli regress <p.lvdproj>` | `-j N`/`--jobs N`, `--junit F`, `--test NAME` (repeatable, also `NAME@SEED`), `--rerun-failed`, `--no-compile`, `--timeout S`, `--wave`, `--coverage`, `--out DIR` | compiles what is out of date, runs the tests as parallel batch simulations, writes `results.txt`, the JUnit file and the merged coverage; exit 0 all passed, 1 some failed, 2 setup error |
| `lvdsim-cli project import <script.do>` | `-o F.lvdproj`, `--ini lvdsim.ini`, `--name N` | builds a project from `vlib`/`vmap`/`vcom`/`vlog`/`vsim` lines; never overwrites |
| `lvdsim-cli project status <p.lvdproj>` | | each file in analysis order with up to date, stale (reason), failed or missing |

## Simulator commands (`tcl/compat/lvdcompat.tcl`)

| Command | Status | Notes |
|---------|--------|-------|
| `run <n> <unit>`, `run <n><unit>`, `run 1.5 us` | wrapped | decimal values and units fs, ps, ns, us, ms, sec |
| `run` | wrapped | 100 ns, the usual default run length; while a process is stopped at a source line it resumes the run (a time is refused by the kernel then) |
| `run -all` | wrapped | until no events are left or the design finishes; resumes a stopped run |
| `run -continue` | wrapped | resumes a run stopped at a source line; otherwise accepted without effect on the length (the default run), also after a run ended by Break / Ctrl-C (it does not go on to the end of the broken run) |
| Ctrl-C, Ctrl-Break during `run` | native (kernel 41) | ends the run before its next time step (`vsim -c` in a terminal, Break in the GUI); the reply says `interrupted at <time> fs` and `run`, `run -all`, `run <time>` go on from there, as after a time breakpoint; ignored at the prompt of an interactive session |
| `run -step -next -over -out` | unsupported | clear error; use the native `step -into`, `step -over`, `step -out` |
| `force [-freeze\|-drive\|-deposit] sig value` | wrapped | based numbers (`16#FF`, `2#101`, `0xFF`) become integers |
| `force` with a time list, `-repeat`, `-cancel` | wrapped | the first value is applied now, with a warning |
| `force` without arguments | native | lists forced signals |
| `noforce sig` | native | |
| `examine [-radix r \| -hex ...] sig...`, `exa` | native | |
| `find signals [-r] pattern`, `describe`, `echo` | native | `describe -location` (fork, kernel PR 10) adds the source location of declarations, units and instantiations (protocol.md, "Source locations") |
| `quit [-f] [-force] [-sim] [-code n]` | wrapped | `exit -code n` |
| `do file` | wrapped | `source`, with `argv` set |
| `quietly cmd` | wrapped | |
| `add wave [-r] [-radix r] [-color c] [-label l] [-group g] [-divider t] [-format analog] [-min x] [-max y] pattern...` | wrapped | in the GUI it fills the wave window (patterns like `/dut/*`, `/tb/dut/clk`; a vector named `count[7:0]` matches `count`); no effect in batch mode; lvdsim extensions: `-group a/b` nests (one level per `/`), `-group g` alone declares an empty group, `-collapse`, `-bits`, `-vbus`, see display-format.md; `-min`/`-max` need both limits with min < max (else the range stays automatic), the same range is edited by the wave window's "Analog Settings..." dialog; `-height P` is the row height in pixels of any row kind (12 to 800; analog rows: a multiple of 22 is kept as lines), also set with the wave window's "Height..." entry, by dragging a row's bottom border or Alt+Up/Down; `-vbus NAME -format analog*` draws the virtual bus as an analog trace; `-expr {EXPR}` adds an expression row (`-label` names it; see expressions.md), patterns may name a dataset (`ref:/tb/*`), `-compare [-tolerance T] REF TEST` adds compare rows for the signal pairs |
| `delete wave *` | wrapped | GUI: removes all rows; batch: no effect |
| `add list [-r] [-radix r] [-label l] pattern...` | wrapped | in the GUI it adds columns to the List dock (same patterns as `add wave`; rows on any change, on a trigger signal or at a strobe interval are chosen in the dock); no effect in batch mode; lvdsim extensions: `-expr {EXPR}` adds an expression column, dataset patterns `ref:/tb/*` |
| `add wave -expr {EXPR} [-label l] [-radix r]` | lvdsim | GUI: an expression signal row computed from other signals (VHDL-style operators, slices, comparisons; see expressions.md); errors are reported with their column; no effect in batch mode |
| `add wave -compare [-label l] [-tolerance T] REF TEST` | lvdsim | GUI: compare rows (high where REF and TEST differ) for the signal pairs of the two patterns, which must match the same number of signals; `T` like `2 ns` ignores shorter differences |
| `delete list *` | wrapped | GUI: removes all List columns; batch: no effect |
| `add mem [-wordsperline n] [-addressradix r] [-dataradix r] path...` | wrapped | GUI: shows the memory in the Memory dock with these settings (radix names as for `add wave`); the memory must be in the waveform (`-lvdarrays`); no effect in batch mode |
| `view memory`, `view list`, `view objects` ... | wrapped | GUI: shows the named docks (memory, list, objects or signals, transcript, source, processes, breakpoints, structure, library); `view wave` is accepted; no effect in batch mode |
| `add log`, `log`, `wave`, `radix`, `configure`, `transcript` | accepted | no effect |
| `onerror`, `onbreak`, `onElabError` | accepted | no effect yet |
| `restart [-nobreakpoint] [-noforce]` | fork (12) | native: back to time zero in the same process, Tcl state, breakpoints and forces kept (options drop them); a design analysed again after the start is not reloaded. The GUI sends it for Restart and starts a new process when libraries changed |
| `coverage save [file]` | fork (13) | lvdsim's own: writes the coverage counts so far of a design elaborated with coverage; the GUI's Simulation > Refresh coverage |
| `step [-delta\|-time]`, `breakpoint add\|list\|delete`, `processes` | fork | native commands of the kernel branch (old patches 0004 and 0005, `kernel/PATCHES.md`); `-json` for the GUI |
| `bp file line` (source line breakpoint) | fork | name differs: native `breakpoint add [-command script] file:line` (change set 06); the GUI sets them by a click in the Source gutter |
| `step -into\|-over\|-out`, `stack`, `locals [-frame n]` | fork | native statement stepping, call stack and variables (change set 06); the GUI's Debug menu and the Call Stack and Locals docks use them |
| `when`, `bp` | fork | clear error |
| `vcd ...` | unsupported | use `-wlf` or `-lvdwave` |

## Compatibility notes
- Vendor `.wlf` files are not produced. Waveforms are FST files for the lvdsim viewer or GTKWave.
- `force` on vectors takes plain integers; bit string literals are not translated yet.
- Time lists and `when` conditions are not implemented, nor are conditions on line breakpoints; they need further kernel work.

## GUI (lvdsim-gui)

Start it with `vsim [options] design` (no `-c`) or `lvdsim-gui [options] design`. It shows the wave window, an
objects browser, the transcript and run controls. Typed commands go to the simulator, except:
`restart` starts over from time zero and keeps the wave rows (the startup `-do` script is not run again; in the running simulator
unless a library was analysed again since the start, then in a new process),
`quit` or `exit` close the window, and `vlib`, `vmap`, `vcom` and `vlog` run the tools and print their output,
so edit, `vcom`, `restart` works. Headless options for scripts and tests: `--send "cmd"`, `--screenshot f.png`,
`--transcript f.txt`. **Break** (toolbar, Simulation > Break, Ctrl+Break) ends the run that is going (Ctrl-C of the kernel, see the table
above); **Stop** breaks a run first, then exits the simulator, and asks Wait or Kill when it has not exited after 3 s (also on closing
the window; never in headless runs).

### Layout and the Library dock (GUI)

- Default layout: **Library** and **Sim** are tabs of the top left dock, **Objects** is below it, the waveform fills the middle,
  **Source** is on the right, **Transcript**, **Processes** | **Breakpoints** | **Call Stack** and **Locals** are at the bottom. Library is in front until a design is
  loaded, then Sim. View > Reset layout restores this arrangement; the layout is otherwise saved in the session file.
- The **Library** dock lists libraries and design units. Double-click (or the arrow) only expands or collapses a row. To simulate a unit
  right-click it and choose **Simulate** (same as `vsim work.unit`), or type `vsim` as usual. Refresh rescans the libraries.

### Navigation, Find value and the List dock (GUI)

- Next / previous edge of the selected wave rows: Left / Right, Tab / Shift+Tab, toolbar buttons; the edge kind button chooses any,
  rising or falling (single-bit rows). Find value (Ctrl+F; F3 / Shift+F3 repeat): equals / not equals a value in the row's radix (`?` for
  any digit), or a threshold crossing on numeric rows; from the active cursor, wrap off by default. These are GUI actions, not Tcl
  commands.
- List dock: `add list`, View > List, or drag from Objects. Header menu: radix, move, remove, use as trigger; Ctrl+C copies rows,
  Export CSV... writes all rows. A selected row moves the wave cursor and the other way round.

### Expressions, datasets and compare (GUI)

- Wave context menu / Wave menu: **Add expression signal...**, **Edit expression...** (or double-click an expression row),
  **Add from dataset**, **Compare with dataset**, **Compare waveforms...**, **Next / Previous difference**. File menu: **Open dataset...**
  (a finished FST file; its signals are `name:/path`), **Close dataset**. Details in expressions.md.
- Headless: `--dataset NAME=FILE.fst`, and the `--wave-op` commands `expr NAME TEXT`, `compare-with DATASET ROW[,ROW] [TOLERANCE]`,
  `compare-datasets REF TEST [SCOPE] [TOLERANCE]` (`-` for the simulation) and `difference next|prev`.

### Processes, Source and breakpoints (GUI)

- **Processes** dock: name, path, state (`ready`, `delayed`, `waiting`) and `file:line` of every process, read with
  `processes -json` after every `run`, `step` or breakpoint stop, after the startup script and with the Refresh
  button. Selecting a row shows the file in the **Source** dock with an arrow on its line (green dots mark the lines
  of processes that are `ready`); double-click also raises the dock. Relative file names are looked up in the
  current directory, so analyse with absolute paths if the GUI starts elsewhere.
- **Source** dock: read-only text with line numbers, VHDL colours and a find bar (Ctrl+F, F3, Shift+F3).
- **Breakpoints** dock (tabbed with Processes and Call Stack): time breakpoints, then source line breakpoints (check box to
  enable or disable, stops, state; see "Source debugger" below). Time breakpoints are one shot, absolute time. Add takes a time and a unit
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

### Source debugger (GUI)

- A click in the Source gutter (or F9) sets or removes a line breakpoint: `breakpoint add -json <file>:<line>` with the file name (the
  kernel matches it against the path given at analysis; a path is used when two processes come from files of the same name). Line
  breakpoints are kept in the settings per design (working directory and top unit, key `debug/lineBreakpoints/<hash>`) and sent before
  the startup script at every start and restart. Disabled ones are deleted in the kernel and added again when enabled. Typed
  `breakpoint add|delete` is merged into the list after the command (`breakpoint list -json`).
- A `run` or `step` reply with status `stopped` (or, after a script, a quiet `stack -json` probe) puts the GUI in the stopped state:
  Source marker on the line, `Stopped at file:line in /process` in the status bar, `stack -json` into Call Stack and
  `locals -frame n -json` into Locals for the selected frame.
- Debug menu and toolbar: Continue (F5, `run -json`), Step Into (F11, `step -into -json`), Step Over (F10, `step -over -json`), Step Out
  (Shift+F11, `step -out -json`). While stopped, Run (with a time), Step delta and Step time are disabled; a refused typed command shows
  the kernel's error in the transcript and in the status bar (`Refused while stopped: ...`).
- Headless: `--breakpoint FILE:LINE` (as a gutter click, before the `--send` commands), `--frame N` (select a Call Stack frame after
  them), `--show-dock stack|locals`.
