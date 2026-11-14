# NVC interactive shell: command inventory

Captured 2026-11-06 from `kernel/nvc` at upstream commit 386a23083, the base before the fork branch `lvdsim` existed (nvc 1.24-devel, LLVM 23.1.5),
built with `--enable-tcl`. Start with `nvc [-L libdir] -i [unit]`. The shell is standard Tcl 8.6 plus the
commands below. Names are relative to the elaborated top unit, so the root is `/` (for example `/cnt`).

## Native commands

| Command | Notes |
|---------|-------|
| `run [time]` | Start or resume. `run 100 ns` works. Upstream: Ctrl-C (SIGINT) ends `nvc -i`. Fork (41): it ends the run before its next time step (reply `interrupted at <time> fs`, `-json` status `interrupted`) and `nvc -i` ignores it at the prompt; see "Fork additions". |
| `restart` | Upstream replies "restart command is not currently supported". Fork (12): works, `restart [-nobreakpoint] [-noforce] [-json]` (see "Fork additions"). |
| `force [sig value]` / `noforce sig` | Enum literal, integer or bit-string value. No args lists forces. Fork (09): array elements `sig(i)`, `-json`, and `deposit`, `readmem` (see "Fork additions"). |
| `examine` / `exa` `[-radix type \| -hex ...] name...` | Prints current value. `-hex` gives `0xa`. |
| `find signals\|regions [-r] pattern` | Recursive wildcard search. `find signals -r /*` lists all. |
| `describe name...` | One line: name, path, kind, type. Upstream aborts on a Verilog net or variable ("T_VERILOG does not have item I_TYPE"); fork (35): the Verilog type as declared, `wire [3:0]`, `logic [7:0] [0:3]`. Fork (42, PR 10): `-location` adds `file`, `line`, `column` of the declaration, and for regions `unit`, `unit_file`, `unit_line`, `inst_file`, `inst_line`; generics, constants and process variables are accepted by path (`/uut/width`, `/uut/reg/count`). See `docs/protocol.md`, "Source locations". |
| `run` reaching a Verilog `$stop` | Upstream: fatal "system task $stop not registered". Fork (30): the run ends with a note; fork (36): in the shell the process stops like at a breakpoint (stop reason `stop`), `run` continues after the `$stop`. |
| `echo`, `copyright`, `help [cmd]`, `exit` | `quit` does not exist. |
| `vhpi::*` | assert, compare_handles, get, get_str, get_value, handle, handle_by_index, handle_by_name, iterate, null, put_value. |

## Findings of the first survey

1. **No waveform output from the interactive shell.** `-i` takes no options. The wave dumper is created
   only by the batch `-r` path in `src/nvc.c`. The GUI cannot get an FST file from an interactive
   session until the fork adds one. This was the first kernel patch (0001, and 0002 for the live stream).
2. **No `add wave`, `log`, `quit`, `bp`, `step`, `delete`, `when`.** `quit` is a one-line alias.
   Breakpoints and stepping need real kernel work.
3. **`restart` is unsupported** in the shell today. Needed for the usual simulator workflows.
4. Replies are human text (Tcl lists and dicts). Patch 0003 adds a `-json` option, see Fork additions.
5. Without an installed library tree, run from the build directory with `-L build/lib`.
6. Upstream now describes NVC as a VHDL and SystemVerilog compiler. Re-evaluate the Verilog scope against
   the current upstream Verilog support before planning kernel Verilog work.

## Survey of the shell for stepping and breakpoints (2026-11-06, upstream commit 386a23083)

Checked in `src/tcl/tcl-shell.c` (the command table in `shell_new`) and with `help` in the running shell.

| Capability | Upstream shell | Notes |
|------------|----------------|-------|
| Breakpoints | none | No source or signal breakpoints. The model has no breakpoint hook. |
| Stepping | none | `rt/model.c` has `model_step()` (one cycle, used by unit tests) but no shell command. |
| Run to a time | `run <n> <unit>` | Relative time only. Stops after the last cycle at or before that time. |
| Process list | none | `describe` only knows signals and regions. Processes live in `rt_scope_t.procs`. |
| Signal examine | `examine`, `exa` | Native. Strings are formatted by the NVC printer, no type information. |
| Signal search | `find signals\|regions` | Native, glob on the full path, always recursive. |
| Object info | `describe` | Name, path, kind, type. Returns a Tcl dict. |
| Time | `$now` (fs), `$deltas` | Linked read-only variables, updated after `run`. |
| Force | `force`, `noforce` | Native. |
| Restart | `restart` | Present, replies "not currently supported". |
| Machine-readable replies | none | Every reply is Tcl list or dict text. |

Everything marked none above is added by patches 0003 to 0005 except source level breakpoints, which change set 06 adds
(see Fork additions).

## Command mapping status

| Command | NVC today | Status |
|--------|-----------|--------|
| run | `run` | native |
| force / noforce | same | native |
| examine | `examine` | native |
| find | `find signals` (different syntax) | wrapper |
| describe | `describe` | native |
| restart | `restart [-nobreakpoint] [-noforce]` | fork (12) |
| quit | none | wrapper alias |
| add wave / log | none, no dumper in shell | fork-needed (first patch) |
| bp | `breakpoint add <t> <unit>`, `breakpoint add [-command s] <file>:<line>` | fork (0005 time, 06 source line), name differs |
| step | `step [-delta\|-time]`, `step [-into\|-over\|-out]` | fork (0005 cycle, 06 statement) |
| call stack, locals | `stack`, `locals [-frame n]` | fork (06) |
| when | none | fork-needed |
| process list | `processes` | fork (0004) |

## Fork additions

- **0001** `nvc -i` and `nvc --do` accept `--wave[=file]`, `--format=fst|vcd`, `--gtkw`, `--dump-arrays`,
  `--include`, `--exclude`. The dump starts when the shell starts and the file is closed on `exit`, end
  of input or end of script. Verified by reading the FST back with fstapi.
- **Limitation, important for the GUI:** fstapi writes the hierarchy and index only when the file is
  closed, so a file that is still being written cannot be read. The GUI therefore cannot show waveforms
  live from FST. Patch 0002 adds the live stream (`--format=live`) for this, see `docs/protocol.md`.

- **0003** `-json` on `run`, `find`, `examine`, `describe`. One line JSON object with a `"v"` version field.
  Schema in `docs/protocol.md`. Plain replies are unchanged.
- **0004** `processes [-json] [pattern]`: path, scheduling state (`ready`, `delayed`, `waiting`), source file and line.
- **0005** `step [-delta|-time] [-json]` and `breakpoint add|list|delete`. `run` now also stops at the earliest
  time breakpoint. `run` and `step` with `-json` report `now`, `delta` and a `status`.
- Not done: runtime dump control (add or remove signals from the live stream while running), `restart`,
  value or source breakpoints, process state beyond the three above. Process files are reported as NVC stored
  them, which is the path given to `nvc -a`; the GUI should analyse with absolute paths.
- Stepping and breakpoints run the phase callbacks (start and end of simulation) on every call, exactly as
  every `run` already does, so the live stream is flushed after each `step`.
- **Change set 04** (working tree, `kernel/upstream/04-shell-port-mode`): `describe -json` gives `"mode"` (`in`, `out`, `inout`,
  `buffer`, `linkage`, `internal`); the live stream writes `D <mode>` after each `V` record. The plain `describe` reply is unchanged.
- **Change set 05** (`kernel/upstream/05-live-stream-run-end`): `run <time>` counts from the time the previous run reached (not from
  `$now`, which stays at the last event), and the live stream ends each run with that time.
- **Change set 06** (`kernel/upstream/06-shell-source-debugger`): source line breakpoints, statement stepping, call stack and
  variables. JSON forms and how the GUI learns about a stop: `docs/protocol.md`, "Source debugger". Plain forms:

  | Command | Plain reply |
  |---------|-------------|
  | `breakpoint add [-command script] <file>:<line>` | the id. The file is the path given to `nvc -a` or its last components |
  | `breakpoint list` | time breakpoints `{id 1 time 18000000}`, then line breakpoints `{id 2 file tb.vhd line 30 hits 1}` |
  | `run` (and `step`) when a process reaches the line | `breakpoint 2 in /pulser at /src/tb.vhd:30`; the shell then prompts while the run is suspended |
  | `run` while stopped | resumes; the reply is that of the resumed run (empty when it ends, as before) |
  | `step [-into]`, `step -over`, `step -out` | `stopped in /counter at /src/tb.vhd:51` |
  | `stack` | `{level 0 kind function name double symbol WORK.TB.DOUBLE(I)I file /src/tb.vhd line 11} {level 1 kind process name /counter ...}` |
  | `locals [-frame n]` | `{name total kind variable type INTEGER value 3} {name name kind variable type STRING value {"abc"}} ...` |

  Example session (`tb.vhd` as `test/regress/tcldbg1.vhd` of the patch):

      % breakpoint add tb.vhd:50
      1
      % run
      breakpoint 1 in /counter at /src/tb.vhd:50
      % step
      stopped in /counter at /src/tb.vhd:11
      % stack
      {level 0 kind function name double ... line 11} {level 1 kind process name /counter ... line 50}
      % step -out
      stopped in /counter at /src/tb.vhd:51
      % locals
      {name total kind variable type INTEGER value 0} ...
      % breakpoint delete all
      % run

  In a script (`--do`) a stop reads commands from standard input; give the breakpoint or step a `-command` script that ends with
  `run` or `step` to keep a script non-interactive. Not done: function parameters and variables, conditions, `run <time>` while
  stopped (see `docs/protocol.md`).
- **Change set 09** (`kernel/upstream/09-shell-array-elements`): array elements in signal names and two new commands. JSON forms in
  `docs/protocol.md`.

  | Command | Plain reply |
  |---------|-------------|
  | `examine /tb/mem(5)` | `"10101011"`: one index in parentheses per level (`/tb/mem(5)(3)` is one bit); `[5]` works too when quoted for Tcl. Verilog variables and words print as `0 1 z x` strings |
  | `force [-json] /tb/mem(5) X"AB"` | empty. Values as before (`'1'`, `42`, `"0101"`, `X"AB"`); a Verilog word takes `0101`, `X"A5"` or `8'hA5` (shorter values are extended with zeros) |
  | `noforce /tb/mem(5)` | empty; `noforce *` and `noforce /tb/mem` also release forces of single elements |
  | `force` (no arguments) | arrays forced in part, or that the printer cannot show whole (arrays of arrays), are listed per element: `force /tb/mem(5) "10101011"` |
  | `deposit [-json] signal value` | empty. The value becomes the driving value in the next delta cycle of the next `run` and lasts until a driver writes that element again |
  | `readmem [-json] [-binary] file signal [start [end]]` | the number of words. File format of `readmemh`/`readmemb` (`//` and `/* */` comments, `@hex-index`, `_` in words, `x`/`z` digits); words go from the lowest index (or `start`) towards the highest (or `end`); every word is checked before anything is written. Elements: bit vectors, single bits, integers (unsigned words), words of a Verilog memory variable with one unpacked dimension |

  `force`, `deposit` and `readmem` are refused while a process is stopped at a breakpoint. Variables (`variable ram : ...` in a process)
  cannot be written: they are not signals.
- **Change set 11** (`kernel/upstream/11-shell-debugger-followup`): `breakpoint add file:line` fails for a file that is not a source of the
  design (`no source file of the design matches 'nope.vhd'`) and for a line with no statement at or after it; a line without a statement
  (comment, blank, declaration without a value) moves to the next statement: the reply is still the id, a note
  `breakpoint 2 is at line 14 of tb.vhd, the first statement after line 12` says where, `breakpoint list` and the `-json` reply give the
  line used. When a process stops, the live stream is flushed up to the time of the stop.
- **Change set 12** (`kernel/upstream/12-shell-restart`): `restart [-nobreakpoint] [-noforce] [-json]` starts the elaborated design again
  at time zero in the same process (about 22 ms on a 10 000 signal design, a new process takes about 170 ms). Kept: Tcl procedures and
  variables, time breakpoints not yet reached, source line breakpoints (hit counts back to 0), the values given with `force` that are still
  forced (only the parts still forced; a later force wins). Not kept: deposits; breakpoints with `-nobreakpoint`; forces with `-noforce`.
  An FST or VCD dump is written again from time zero; the live stream gets an `R` record (`docs/protocol.md`). Given while a process is
  stopped at a source line, `restart` finishes the current cycle without stopping and the interrupted `run`/`step` gives the restart reply.
  Not available with VHPI plugins (`--load`). A design recompiled after the start is not reloaded: start a new process for that.

      % run 100 ns
      % force /tb/en '0'
      % restart -json
      {"v":1,"now":0,"delta":0,"status":"restarted","breakpoints":0,"forces":1}
- **Change set 13** (`kernel/upstream/13-shell-coverage`): a design elaborated with `--cover` writes its run-time coverage counts when the
  shell (`-i`, `--do`) ends, as `nvc -r` does; `coverage save [-json] [file]` writes them during the session (plain reply: the file name).
  The counts of runs separated by `restart` add up.
- **Change set 14** (`kernel/upstream/14-cover-export-json`): `nvc --cover-export --format=json` (schema in `docs/protocol.md`, "Coverage").
- **Change set 41** (part of `kernel/upstream/07-interactive-debugger`): Ctrl-C (SIGINT; on Windows also a console `CTRL_BREAK_EVENT`, which a
  front end can send to one process group) ends a `run` or `step -into` of the shell before its next time step, instead of ending nvc. The
  run counts as ended at `$now`: `run 100 ns` goes on from there, `run` to the end, time breakpoints stay (as after a time breakpoint).
  `nvc -i` ignores Ctrl-C at the prompt and while a process is stopped at a source line; a `--do` script outside a run, and a second Ctrl-C
  before the run reached its next time step (a process looping without `wait` never gets there), end nvc (status 130). Batch `nvc -r` is
  unchanged ("interrupted in process ..." fatal error), and now reacts to Ctrl-Break the same way.

      % run -json
      ^C
      {"v":1,"now":3842735000000,"delta":1,"status":"interrupted"}
      % run 100 ns
      % run
      ^C
      interrupted at 3871365000000 fs

- **Change set 42** (`kernel/upstream/10-shell-describe-locations`, needs 03 and 07): `describe -location` adds where each object is declared
  (`file`, `line`, `column`) and, for a region, its design unit or block/generate statement (`unit`, `unit_file`, `unit_line`) and the
  statement that instantiates it (`inst_file`, `inst_line`, not for the top level). A port collapsed into an outer signal reports its own port
  declaration. `describe` also accepts generics, constants and shared variables of a region (`/uut/width`) and constants and variables of a
  process (`/uut/reg/count`), kind `generic`, `constant`, `variable`. The block of a Verilog module instance now has the location of the
  instance statement. Replies without the option are unchanged. Format: `docs/protocol.md`, "Source locations".

      % describe -location /uut/clk
      name clk path /uut/clk kind signal type BIT file src/sub.vhd line 3 column 12
      % dict get [describe -location /uut/] inst_line
      53

## libwave measurements (2026-11-06, Windows, MinGW 13.1 Release, scaled trace)

400 signals, 20 M value changes (mix of clocks, 32 and 64 bit buses, reals):

| Measure | Result | Budget (100 M changes) |
|---------|--------|------------------------|
| FST load | 1.5 s, 13 M changes/s | < 20 s, projected 8 s |
| Memory | 510 MB (about 25 bytes per change) | none set, projected 2.5 GB |
| Numeric index build (200 signals) | 2.0 s | < 10 s, projected 10 s, borderline |
| Column query (hold + range) | 350 ns median, 530 ns p99 | < 1 us, met |
| Min/max range query | 134 ns median | met |

Known quirk: fstapi drops variable length (string) values written before the first explicit time change.
NVC always emits a time change first, so its files are unaffected.

## Viewer measurements (2026-11-06, Windows, MinGW Release, offscreen platform)

300 rows, 10 M value changes (clocks, 32 bit buses, 30 analog rows), 1920x1080:

| Measure | Result | Target |
|---------|--------|--------|
| Full-zoom paint, cold cache | 81 ms | one-off |
| Repaint, warm cache | 4.5 ms | 16 ms |
| Zoom step, cold at the new scale | 8 ms average, 17 ms worst | 16 ms |
| Pan 40 px per frame | 3 ms | 16 ms |

Run with `build/mingw-release/gui/bench_paint.exe [signals] [changes] [width] [height] [varied]` (`varied`: rows of 14 to 103
pixels instead of the default heights).
Headless screenshots: `lvdsim-wave-demo.exe trace.fst --screenshot out.png [--cursor T] [--cursor2 T]
[--view START END] [--analog NAME] [--radix NAME RADIX] [--height NAME[,NAME] PIXELS]`; set `QT_QPA_PLATFORM=offscreen` and
`QT_QPA_FONTDIR=C:\Windows\Fonts` (without fonts the offscreen platform draws text as boxes).
