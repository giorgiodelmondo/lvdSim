# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/) (while the major version is 0, minor versions may change behaviour).

## [Unreleased]

### Added
- GUI: **View Declaration / View Definition / View Instantiation / View Source** - double-click a signal in Objects to open the Source dock
  at its declaration (the name selected, the line flashing); a port merged with the signal it connects to shows its own port declaration.
  Sim: double-click (or View Definition) shows the architecture, module, block or generate statement of an instance, View Instantiation the
  statement in the parent. Processes (View Source), the Wave name column and List column headers (View Declaration), Locals (variables of
  the stopped process) and Library (double-click a unit: the open project's file, else the file nvc names; never a simulation; context menu
  View Source and Simulate) do the same. **Back / Forward** in the Source dock (toolbar arrows, Alt+Left, Alt+Right) walk the jumps.
  The Objects context menu is View Declaration, Add to Wave, Add to List, Add to Memory, Copy Path. Without a simulation or with a kernel
  that does not report locations the entries are greyed out with the reason as tool tip; missing source files are reported in the transcript.
  Kernel change set 42 (new pull request 10, needs 03 and 07): `describe -location` gives `file`, `line`, `column` of declarations and, for
  regions, `unit`, `unit_file`, `unit_line`, `inst_file`, `inst_line` (`docs/protocol.md`, "Source locations"); `describe` also accepts
  generics, constants and process variables by path; the block of a Verilog module instance has the location of the instance statement.
  New regress tests `descloc1`, `descloc2`, `descloc3`; GUI tests `test_source_navigation`, `test_view_declaration`;
  `lvdsim-gui --view-declaration PATH` for headless screenshots.
- GUI: **Break** (toolbar, Simulation > Break, Ctrl+Break) ends the run that is going at the simulation time it has reached; the status bar
  says `Interrupted at <time>`, Run goes on from there, commands typed meanwhile run after it. Pressed again while a process loops without a
  `wait` (the run cannot end), it offers to kill the simulator. Kernel change set 41 (in PR 07): Ctrl-C (SIGINT; Windows console Ctrl-C and
  Ctrl-Break) ends a run of the shell before its next time step instead of ending nvc, with the new run status `interrupted`
  (`docs/protocol.md`, "Interrupting a run"); `nvc -i` ignores it at the prompt. The GUI starts nvc in its own process group on Windows and
  sends it a console Ctrl-Break, on Linux SIGINT. New regress tests `tclbreak1`, `shellbreak1`; GUI test `test_break_run`.
- GUI: **the simulator has not exited yet** - when Stop, closing the window or a new start asks the simulator to exit and it has not after 3 s,
  a dialog offers Wait (another 3 s) or Kill, and says that killing loses the coverage counts when the simulation collects coverage. Headless
  runs and tests keep the automatic kill (`NvcSession::setWaitPolicy`).
- GUI: **row heights in pixels** for every wave row (bits, buses, analog traces, virtual buses, expressions, compare rows, dividers):
  **Height...** dialog (context menu and Wave menu, 12 to 800 px, Default button) for all selected rows, dragging a row's bottom border
  in the name or value column (live, Esc cancels), Alt+Up / Alt+Down (2 px steps). Analog traces scale to the height, bit and bus rows
  centre trace and text and use a smaller font when the row is lower than the font. One undo step per change; saved in layouts
  (`heightpx=`) and `.do` files (`add wave -height P` now sets the pixel height of any row; analog multiples of 22 stay lines);
  `lvdsim-wave-demo --height NAME PX`. GPU renderer: the first atlas of a row height is 16 MB instead of 64 MB, and atlases of heights
  no longer drawn are freed when all atlases would exceed 384 MB (they were kept until the device was released).

### Changed
- GUI: **double-click in Objects shows the declaration** instead of adding the signal to the wave window. Adding is Return, Add to Wave in
  the context menu, a drag, or `add wave`. Double-click on a Library unit opens its source (it only expanded the row before).

### Fixed
- GUI: **Stop during a run** ended the simulator only by killing it after 3 s (the shell read `exit` only when the run ended, which on a
  free-running clock is never; coverage counts and the end of the waveform were lost). Stop now breaks the run first, so the simulator exits
  normally.
- **Console commands no longer seem to hang after a stop**: a time breakpoint added (or deleted) while a process is stopped at a line or at
  a `$stop` now ends the resumed run there (kernel change set 40; the run went on to its old limit, for ever on a free-running clock,
  and every command typed afterwards waited behind it); after a `$stop` in a run started without line breakpoints Step Into / Over / Out
  are refused with a message instead of resuming a run that never stops again, and a line breakpoint set then says it applies from the
  next run. A command word that names a program (`pause`, `sort`, `more`, `cmd`) is an unknown command in the GUI instead of a program
  that read the shell's input and swallowed every later command. A command typed while a run is still going gets a one-line note that it
  waits for the run. Protocol: a JSON reply printed right after the prompt in the same read (the stop probe's `puts`) was given to the
  previous command, and half a prompt behind a held-back line was dropped (the session then stayed busy). `LVDSIM_SHELL_TRACE=<file>`
  logs every byte to and from the shell.
- GUI: **analog view of a virtual bus** - Format > Analog trace, Analog Settings, `add wave -vbus ... -format analog*` and a layout with
  `bus ... analog=` left a virtual bus drawn as a hex bus (the bus row had no numeric index). A bus now builds a min/max index from its
  composed value and draws exactly like a vector of the same bits (same values, auto range, manual range, zoom levels, live growth, restart,
  QPainter and GPU); X/U bits give no value, buses wider than 64 bits stay bus rows. `add wave -vbus` now accepts `-format analog*`, `-min`,
  `-max`, `-height` and the `.do` export writes them. Analog traces of signals and buses read the bits as two's complement when the radix is
  signed (before: only with the layout's `signed` flag).
- GUI coverage: the **Coverage dock fills when the simulation reaches its end** (`run -all`, `$finish`, `std.env.finish`), not only on
  Refresh coverage or when the session ends; a refresh keeps the selected instance. At the end of a session the counts the shell writes
  are always loaded (a stale time-stamp check could skip them and printed that "this nvc saves coverage only for batch runs").
- GUI coverage: **a new elaboration no longer overwrites the database the dock shows**: a restart in a new process or a new start waits
  for a running export and gives Save merged coverage and the HTML report a copy of the previous counts (before: they silently used the
  new, empty database, and the export could race the elaboration).
- GUI coverage: the database name given to nvc is **absolute** (`--cover-file`), so Refresh coverage still finds it after a project
  changed the working directory; when the working directory is **not writable** (a program folder) the database goes to the data folder
  instead of nvc stopping with "failed to open coverage database".
- GUI coverage: **sources of a database opened from another folder** (a batch run's relative names such as `src/top.vhd`) are found next
  to the database, so selecting an instance shows and annotates the source again.
- Coverage numbers: **excluded bins count as covered** as in nvc's report (exclude flag, and unreachable toggles/expression terms with
  `--cover=...,exclude-unreachable`); **record fields** in the toggle details read `r.a` (were `ra`); design units with an extended
  name containing `-` are shown correctly. An empty database (Verilog top: nvc collects VHDL coverage only) says so in the dock.
- Kernel: `coverage save -json` reports the time the simulation reached in `now` (it gave the end time of the last `run` command).
- `lvdsim-gui --coverage-refresh` (headless): Simulation > Refresh coverage after the `--send` commands.

## [0.1.5] - 2026-11-14

### Added
- **Projects and regressions** (docs/projects.md): a plain-text project file (`.lvdproj`: libraries, source files in compile order with
  per-file VHDL standard, Verilog defines and include directories, tests with generics, plus arguments, do scripts or inline
  commands, seeds, time-outs, expected output, waveform and coverage options). `lvdsim-cli compile` analyses only changed files and the
  files that depend on them, in dependency order (unit scanner for VHDL and Verilog, compile state with time stamps and content hashes in
  the library root); `lvdsim-cli regress` runs the tests as parallel batch simulations (`-j`, time-outs, pass/fail from the exit code,
  error/failure messages and an optional regular expression, per-run logs, `--rerun-failed`, `--junit` XML, merged coverage);
  `lvdsim-cli project import` builds a project from a compile `.do` script and `lvdsim.ini`; `project status`. Qt-free code in `tools/src`
  (`project`, `hdl_scanner`, `compile_plan`, `compile_runner`, `diagnostics`, `do_import`, `regression`, `project_cli`); a non-blocking
  `ChildProcess` with output to a file and kill of the whole process tree (job object / process group) in `process.hpp`.
- GUI: **Project** menu (new, open, recent, save, import compile script, compile out-of-date F7 / all, run regression, rerun failed);
  **Project** dock (libraries, files with status icons and the reason they are out of date, tests; compile all / out-of-date /
  selected, add, remove, reorder, simulate a test, regress), **Problems** dock (analysis messages, double-click opens the source line),
  **Regression** dock (results table, run all / selected / failed, stop, jobs, open log, open waveform as a dataset, merged coverage).
  Opening a project makes its directory the working directory and its libraries visible to simulations. Layout version 8.
- GUI: **Restart in the running simulator** (`restart -json`) when no library was analysed again since the start, else (or when the
  kernel refuses) a new process; an open project's changed files are analysed first. The wave window shows the new run from time zero,
  the event band and line breakpoint hit counts start again. **Simulation > Refresh coverage** (`coverage save -json`) shows the counts of
  the running simulation; databases are read through the JSON export, with the XML export as fallback for older kernels.
- Kernel (fork change sets 30 to 39, `kernel/upstream/30-*` to `39-*`): **SDF back-annotation and Verilog fixes** - `nvc -e --sdf=[min=|typ=|max=][INSTANCE=]FILE`
  annotates the VITAL timing generics of VHDL instances (`IOPATH`, `PORT`, `INTERCONNECT`, setup/hold/recovery/removal, width, period) and the
  timing check limits of Verilog instances; `vsim -sdfmin/-sdftyp/-sdfmax [inst=]file` now map to it (were unsupported); see docs/sdf.md.
  Verilog module path delays are not supported yet. Verilog `$setuphold`/`$recrem` are checked, timing check events honour `posedge`/`negedge`
  and limits follow the `timescale`. `$stop` ends a batch run (and waits at the prompt in `nvc -i`, stop reason `stop`), `$isunknown` works.
  The Verilog `altsyncram` of `altera_mf.v` now writes and reads back (nets driven during initialisation get an event at time zero; input
  ports driven inside the module or connected to an initialised `reg` keep their own net); all 113 `altera_mf.v` models run. Fixed crashes:
  a task with an output argument and a delay, two instances with an internal driver on an input connected to one `reg`, `describe` of a
  Verilog signal, `-g` of a `delay_length` generic.
- GUI: **Verilog `$stop` in the debugger** - the stop reason `stop` (change set 36) is read (`StopReason` in `StopReply`, was rejected as
  an unknown reason): the status bar says `Stopped by $stop at file:line`, Source marks the line, Call Stack and Locals are filled and
  Continue goes on after the `$stop`. In batch runs and regressions a `$stop` ends the run like VHDL `stop` and does not fail the test.
- Kernel (fork change sets 23 to 29, `kernel/upstream/23-*` to `29-*`): **more Verilog for the vendor libraries** - hierarchical task and
  function calls (`eva.GET_PARAMETER_VALUE(...)`, `ram_core0.initialize_mem_contents();`), rise/fall/turn-off delays on continuous
  assignments, `x`/`z` delays count as zero, unpacked array `localparam` with constant selects, a module instance as a generate block without
  `begin`/`end`, port ranges that depend on parameters, and `tri0`/`tri1` nets that pull when undriven. Verilog unisim: all 428 models compile,
  318 run (DCIRESET, DCM_SP). Altera `lpm_ver` (`220model.v`) and `altera_mf_ver` (`altera_mf.v`) compile; 33 of 33 and 108 of 113 of their
  models elaborate and run; `lpm_ram_dp` works, the Verilog `altsyncram` does not yet; `twentynm_atoms.v` and `cyclone10gx_atoms.v` analyse
  (19 of 66 models each run, the rest need encrypted modules). `altera_lnsim.sv` and `tennm_atoms.sv` still fail (docs/libraries.md). Change
  sets 23, 24 and 26 change the kernel library format: recompile the vendor libraries after updating nvc.
- Kernel (fork change sets 15 to 22, `kernel/upstream/15-*` to `22-*`): **Verilog gaps of the vendor simulation libraries** - `defparam`
  through instances and generate blocks (and parameters read through hierarchical names), hierarchical names into and between generate
  blocks (`g.u.x`, `gen[2].u.x`; XPM block RAM memories and FIFOs now elaborate and work), arrays of gate instances, `min:typ:max` timing
  check limits and part selects in specify paths, an empty last `$display` argument, specparams in timing checks,
  `$realtobits`/`$bitstoreal`, `function time` and `const` variables. Verilog unisim (Vivado 2022.2): 427 of 428 models compile (was 418),
  316 analyse, elaborate and run (was 303); see docs/libraries.md and docs/verilog-coverage.md. Change set 17 changes the kernel library
  format: recompile the vendor libraries with scripts/libs/compile_libs after updating nvc.
- Kernel (fork change sets 13 and 14): **coverage from the Tcl shell** - `nvc -i`/`--do` on a design elaborated with `--cover` load the
  database and write the run-time counts when the shell ends (so Simulation > Collect coverage gets real counts; the
  `coverage_integration_tests` shell case now runs instead of skipping); new shell command `coverage save [-json] [file]` for a snapshot
  during a session (writes never count twice; counts of runs separated by `restart` add up); `nvc --cover-export --format=json`, a
  versioned export with every bin's kind, location (file, line, column), hits, threshold and exclusion flags (`docs/protocol.md`).
- tools: `cov::parseExportJson` and `cov::parseExport` (either format), `exportArgs(..., ExportFormat::Json)`; `Bin` has `column`,
  `threshold` (`hit()` honours it), `excluded`, `userExcluded`; `Database::mask`.
- Kernel (fork change set 12, `kernel/upstream/12-shell-restart`): **`restart` in the Tcl shell** without a new process:
  `restart [-nobreakpoint] [-noforce] [-json]` starts the elaborated design again at time zero (about 22 ms against about 170 ms for a new
  `nvc -i` on a 10 000 signal design). Tcl procedures and variables, time breakpoints not reached yet, source line breakpoints and the
  values given with `force` are kept; FST/VCD dumps are written again; the live stream carries a new `R` record. Given while stopped at a
  source line it finishes the cycle and the interrupted `run`/`step` replies. A recompiled design is not reloaded (start a new process).
- libwave: `LiveReader` understands the restart record (`PollResult::restarted`, `restartCount()`): the store's values are cleared
  (`WaveStore::clearValues`, signals and ids stay); `Signal::generation()` makes `NumericIndex`, `ExpressionSignal`, `SignalCompare`,
  `ListIndex` and `MemoryImage` start again after a clear. `gui/sim`: `parseRestartReply`, `isRestartReply`, `restartCommand()`.
- **Coverage dock** (View > Coverage, Simulation > Show coverage): instance hierarchy with statement, branch, expression, toggle, FSM
  state and functional coverage as colour bars (rows include the instances below), filter, Uncovered only, and details of the selected
  instance (lines not covered, toggles per signal bit, FSM states). Selecting an instance or item shows its source with a count column
  and tinted lines in the Source gutter, next to the breakpoint marks. File > Open coverage (several `.ncdb` are merged), Save merged
  coverage (`nvc --cover-merge`), Coverage HTML report (`nvc --cover-report`, opened in the browser); Simulation > Collect coverage
  (setting `simulation/coverage`) elaborates with `--cover=... --cover-file=<top>.ncdb`. Databases are read through
  `nvc --cover-export --format=xml` by the Qt-free `lvd::tools::cov` (`tools/src/coverage.{hpp,cpp}`, also plans the nvc calls);
  `lvdsim-gui --coverage F --coverage-select PATH --coverage-uncovered --coverage-filter TEXT` for headless screenshots
  (docs/tutorial.md "Code coverage"). The simulator writes the run-time counts when its shell ends, and Simulation > Refresh coverage
  (`coverage save -json`) shows them during a session.
- `vsim -coverage` collects coverage (was accepted and ignored): `nvc -e --cover=<all kinds> --cover-file=<top>.ncdb`; new
  `-lvdcover KINDS` (words or letters `s b c/e f t`) and `-lvdcoverfile F`. `vcom`/`vlog` accept `+cover` and `+cover=<letters>`
  with a note that nvc selects coverage at elaboration. Simulation > Collect coverage now passes `-coverage` through the same
  translation (`planVsim`) instead of inserting nvc options itself.
- Kernel (fork change set 09, `kernel/upstream/09-shell-array-elements`): **writing memories from the shell**. `examine`, `force` and
  `noforce` take array elements (`/tb/mem(5)`, Verilog `/top/m(3)`); new `deposit [-json] signal value` (driving value deposit at the next
  delta cycle) and `readmem [-json] [-binary] file signal [start [end]]` (readmemh/readmemb file format, all words checked before any is
  written); `force -json`; `examine` prints Verilog variables (binary). Refused while stopped at a breakpoint. Fixes in the model: the force
  list of signals forced in part, deposits and released forces coming back after a nexus split, VHDL-93 `std_logic_vector` values.
- Kernel (fork change set 10, `kernel/upstream/10-generic-override-paths`): generic override paths through generate and block statements
  (`gen(1).u.W`), every override applies to all matching instances, `*` patterns, new `nvc -e --generic-default=NAME=VALUE`. `vsim -gNAME`
  now sets every instance without a generic-map value (and the top level), `-GNAME` every instance, `-g/tb/gen(1)/u/NAME` one generate
  iteration (was an error); docs/command-compat.md "Generic overrides".
- Kernel (fork change set 11, `kernel/upstream/11-shell-debugger-followup`): the live waveform stream is flushed when a process stops at a
  breakpoint, so the wave window shows data up to the stop; `breakpoint add file:line` rejects unknown files and lines after the last
  statement and moves a line without a statement to the next one (`-json` member `"requested"`).
- **Memory window** (View > Memory, `add mem`, Objects > Open in Memory window): the memories of the design (arrays of vectors or
  integers) in a list and one as an address/data grid with words per line, address and data radix, go to address, find value,
  the last written words marked, the contents at the wave cursor (time travel) or at the end of the run (live), previous / next
  write, and Save as a `$readmemh` / `$readmemb` file. libwave `lvd::wave::MemoryImage` (`lvdwave/memory.hpp`) indexes the
  element signals once (repeated values are no writes) and answers word-at-time, writes-at-time and find in O(log n);
  `bench_memory`: 64K words with 2 M writes, index 91 ms, a screen of 1024 words 0.55 ms, update per live poll 0.8 ms. The Objects
  pane groups the words of a memory under one item. Memories are recorded only on request: Simulation > Record memories
  (setting `simulation/recordArrays`) or `vsim -lvdarrays` (`--dump-arrays`). `view memory|list|...` shows docks from scripts.
- Kernel (fork change set 06, `kernel/upstream/06-shell-source-debugger`): **source line breakpoints and statement stepping** in the
  shell. `breakpoint add [-command script] file:line` stops a process when it starts the line (VHDL and Verilog); the shell then reads
  commands while the run is suspended; `run` resumes, `step [-into|-over|-out]` steps by statement; `stack` gives the call stack and
  `locals [-frame n]` the variables of process frames and of procedures that can wait (with parameters). All with `-json` (schema 1,
  new status `"stopped"`; docs/protocol.md "Source debugger"). No cost without line breakpoints (same intermediate code and interpreter loop); with a line breakpoint set the design runs
  in the interpreter. Fixes a double free in the shell's value printer when a subtype and its base type are both printed.
- GUI protocol readers for the source debugger (`gui/sim/parsers/json_replies.hpp`: `isStopReply`, `parseStopReply`, `parseStackReply`,
  `parseLocalsReply`, `parseLineBreakpointAdded`, `parseLineBreakpointList`; `parseBreakpointList` skips line breakpoints) and the
  command builders `gui/sim/debug_commands.hpp`.
- **Source debugger in the GUI** (uses change set 06): click in the Source gutter (or F9) to set or remove a line breakpoint (red dot;
  red ring when disabled; grey when the shell refused it or the line has no statement). Line breakpoints are kept per design in the
  settings and set again before the startup script at every start and restart; ones typed in the transcript join the list. When a
  process stops, the Source dock opens the file with a yellow arrow on the line and the status bar says `Stopped at tb.vhd:28 in
  /accumulate`. Debug menu and toolbar: Continue (F5), Step Into (F11), Step Over (F10), Step Out (Shift+F11), enabled only when they
  apply; Run with a time and the cycle steps are disabled while stopped, and a command the shell refuses then shows its error in the
  status bar. New docks **Call Stack** (select a frame: green arrow on the call, its variables in Locals) and **Locals** (name, type,
  value; aggregates expand into fields or elements; values changed since the last stop in red). The Breakpoints dock lists line
  breakpoints after the time breakpoints, with enable check box, hit count and state. A stop inside a `-do` script is noticed too.
  Headless options `--breakpoint FILE:LINE` and `--frame N`. `run` without a time and `run -continue` of the command layer resume a
  stopped run. Tests: `test_source_debugger` (headless) and `test_debug_session` (real kernel).
- **Expression signals** in the wave window: rows computed from other signals (`valid and ready`, `bus = 16#3F`, `cnt > 100`,
  `data(15 downto 8) & valid`, `signed(x) < 0`; VHDL-style operators with C spellings, slices, concatenation, nine-valued logic,
  reals and enumeration text). libwave `lvd::wave::Expression` / `ExpressionSignal` (`lvdwave/expression.hpp`): parser, type
  checker and incremental evaluator whose result is an ordinary `Signal`, so renderers, LOD, the searches, analog display and the
  List window use it unchanged; live runs are followed incrementally. GUI: **Add expression signal...** dialog with validation
  (error and column while typing), edit by double-click, undo; saved in the layout (`expr`) and the wave format; `add wave -expr {...}`
  and `add list -expr {...}` from the console. `bench_expression`: 18-103 ns per evaluated time step over 1 M changes per input.
- **Datasets**: File > Open dataset... opens finished FST files next to the simulation (`lvd::wave::WaveSpace`, names `ref:/tb/x`;
  other time units converted with `retimeStore`; a file the simulation is writing is refused). Dataset rows from `add wave ref:/...`,
  the context menu (**Add from dataset**) and expressions; each row ends where its own waveform ends.
- **Waveform compare**: compare rows (red, high while two signals differ) from **Compare with dataset**, **Compare waveforms...**
  (whole waveforms or one scope, with a summary) and `add wave -compare [-tolerance T] REF TEST`; **Next / Previous difference**.
  libwave `SignalCompare` (incremental, time tolerance, L/H equal 0/1), `compareStores`, `findDifference`, `parseDuration`.
- Headless options `--dataset NAME=FILE` (lvdsim-gui and lvdsim-wave-demo), `--wave-op` commands `expr`, `compare-with`,
  `compare-datasets`, `difference`; docs/expressions.md.
- Wave window **edge navigation**: next / previous edge of the selected rows with Left/Right, **Tab / Shift+Tab** and the toolbar; an
  edge kind button (any, rising, falling; rising and falling on single-bit rows and bit rows). Repeated values and glitches inside one
  time step are not edges. Status bar message when there is none (`WaveWidget::jumpToEdge(dir, kind)`, `navigationMessage`).
- **Find Value** dialog (toolbar, Ctrl+F; F3 / Shift+F3 repeat): equals / not equals a value in the row's radix (hex, octal, binary,
  decimal, ascii; X, Z, U and `?` for any digit), or a threshold crossing (rising, falling, either) on numeric rows; forward or backward
  from the active cursor, optional wrap (off by default). Works on signals, bit rows and virtual buses.
- libwave **search** (`lvdwave/search.hpp`): `findEdge`, `findValue`, `ValuePattern`, `BitsSequence`; binary search for the start,
  crossings skip blocks with the `NumericIndex` min/max pyramid. Catch2 tests against brute force; `bench_search` (1 M changes: next edge
  0.4 us, full value scan 4.8 ms, crossing with index 1.8 us).
- **List** dock (View > List, `add list`, drag from Objects): time plus one column per signal, rows on any change, on a trigger signal
  or at a strobe interval, values in each column's radix, changed values bold; header menu for radix, move, remove, trigger; Ctrl+C,
  **Export CSV...** (UTF-8, time unit in the header); cursor sync with the wave window both ways; live growth. Rows are not built:
  `lvd::wave::ListIndex` merges the change lists with a checkpoint every 1024 rows (1 M rows indexed in 4 ms release, 55 ms debug).
- `add list` and `delete list *` now act in the GUI (they were accepted without effect).
- `--wave-op` commands `edge next|prev [any|rising|falling]` and `find next|prev VALUE [condition]` for headless screenshots.
- Objects pane: **drag signals to the wave window** (one, many, or a scope for all its signals). Custom mime type
  `application/x-lvdsim-signal-paths` (JSON list of `add wave` paths) plus `text/plain`; drag picture with names and count. The wave
  window accepts the drop on the name column, the value column and the wave area with the row drag's blue line and "into group"
  highlight, appends below the last row, scrolls near the edges, shows "not allowed" elsewhere. The drop runs the `add wave` machinery
  (`lvd::wave::applyAddWave`), selects the new rows and is one undo step (`WaveWidget::dropSignals`, `signalDropTargetAt`).
- Objects pane: **port direction icons** (input, output, inout, buffer, internal, unknown) drawn as vectors (`gui/app/port_icons.*`), with
  tooltips; `lvd::gui::PortMode`, `ObjectsPane::setPortModes`, optional `"mode"` member of `describe -json` (`parseDescribeReply`,
  docs/protocol.md "Port mode"). The kernel reports them now (see below).
- Kernel change set 04 (working tree of `kernel/nvc`, PR folder `kernel/upstream/04-shell-port-mode`): port modes in `describe -json`
  (`"mode"`) and in the live stream (`D` record after each `V`); `LiveReader` and `loadFst` store them per entry
  (`lvd::wave::PortDirection`, `Signal::direction()`), and the Objects pane shows them without any request.
- Kernel change set 05 (`kernel/upstream/05-live-stream-run-end`): the live stream ends each run with the time the run reached, so the
  end of data and Follow use the run end (`run 100 ns` with the last event at 40 ns ends at 100 ns), and `run <time>` counts from it.
- Wave window: a horizontal scroll bar for time (range = the run known to the store, grows while live; thumb = visible window) and a
  dark-themed vertical bar for rows (name, value and wave columns scroll together; 10 000 rows tested). Hit testing of rows is now a
  binary search.
- Wave toolbar: a checkable **Follow simulation (stay at end)** button (also View > Follow simulation, Ctrl+Shift+F, saved as `wave/follow`).
  It keeps the right edge of the view at the end of the live data with the current zoom width. Manual panning, Home/End, Go to and
  Zoom full turn it off; zooming keeps it on with the end pinned.
- **Analog Settings...** dialog (context menu of analog rows, Wave menu): auto range or manual Min/Max with validation, one undo step,
  saved in layouts and `.do` files. Values outside a manual range are clipped to the row edges (QPainter and GPU).
- `lvd::wave::resolveAnalogRange`, `analogFraction`, `analogRangeError`; `add wave -min/-max` now ignores invalid pairs.
- `--wave-op` commands `follow`, `zoom`, `scroll-time` and `analog-range` for headless screenshots.
- **Console comfort** in the Transcript dock: persistent command history per user (settings `console/history`, last 1000, no
  duplicates), Up/Down, **Ctrl+R** history search, Transcript > Command history... dialog (filter, insert, run, clear); **Tab
  completion** of command names (built-in list plus the shell's `info commands`, asked once on the first command-name Tab and again
  after `proc`/`do`/`source`), sub-commands, options and `-radix` values of the common commands, design paths from the waveform
  hierarchy as the shell names them (`/u_sub/a`), and file names for `do`/`source`/`vcom`/`vlog`, with a popup when ambiguous;
  **multi-line input** (Shift+Enter; Enter on an unfinished command continues on a `>` line, decided in the GUI like `info
  complete`; pasted lines are sent one by one, a command spanning lines goes to the shell as one `eval "..."` line); transcript
  lines coloured by `** Error` / `** Warning` / `** Note`, `file:line` and Tcl `(file "x.do" line N)` references as links that open the
  Source window, Ctrl+F find bar (F3, Shift+F3, wraps), Save transcript, Clear, Copy without prompts, line cap setting
  `console/maxLines` (20000); `.do`/`.tcl` files dropped on the console run with `do`, Transcript > Run do file... and Recent do
  files (10). New Transcript menu. Classes `ConsoleInput`, `ConsoleCompleter`, `ConsoleHistory`, `ConsoleHistoryDialog`,
  `gui/app/console_text.hpp`. The appender inserts each output chunk at once instead of line by line: `bench_transcript`, 1M lines
  in 4 KB chunks with the 20000-line cap, 10.9 s (92k lines/s, slowest append 4.6 ms) against 75 s (slowest 245 ms) before.
  Headless options `--complete TEXT`, `--crop-dock NAME`, `--dock-height NAME H` of `lvdsim-gui`. Tests: `test_console_text`,
  `test_console`, `test_console_session` (real kernel). The shell protocol is unchanged.

### Fixed
- `vsim -g<name>=<value>` and `-G<name>=<value>` (value joined to the option) no longer warn "option not supported"; they reach the
  elaboration (`nvc -e -gNAME=VALUE`) in batch mode and in the GUI. Hierarchical names (`/tb/u/WIDTH`), quoted strings and based
  integers (`16#FF#`) are translated; limits of the kernel's override (top-level only for plain names, no generate paths) are in
  docs/command-compat.md.

### Changed
- Kernel (fork change set 07, `kernel/upstream/07-wave-changed-elements`): arrays in the waveform (`--dump-arrays`, Record memories) are
  written element by element only when an element changes: a 64K word RAM written every clock, 20 us, live stream 5.35 GB / 21.5 s before,
  5.8 MB / 0.35 s after; FST 1.59 s before, 0.39 s after (same content).
- Kernel (fork change set 08, `kernel/upstream/08-verilog-memory-dump`): Verilog memories are recorded word by word (`m[3][7:0]`) with
  `--dump-arrays` and left out otherwise (they appeared as one vector holding word 0).
- The saved window layout version is 4 (List dock): a layout saved by an older build is ignored once.
- Edge navigation skips changes that repeat the previous value (it used to stop at every recorded change).
- The wave area is shorter by the height of the horizontal bar (`WaveWidget::rowsBottom()`); the first live `dataChanged` no longer
  treats a freshly attached store as "showing the end".

