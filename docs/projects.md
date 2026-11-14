# Projects, incremental compile and regressions

A project collects what a design needs besides its sources: the libraries and the files in each, the order they are analysed in, the
test benches with their generics and scripts, and how a regression runs. lvdsim keeps it in one small text file, `NAME.lvdproj`, next
to the sources. The GUI (Project menu, Project / Problems / Regression docks) and the command line (`lvdsim-cli compile`, `regress`,
`project`) read the same file.

- [The project file](#the-project-file)
- [Incremental compile](#incremental-compile)
- [Regressions](#regressions)
- [Importing a compile script](#importing-a-compile-script)
- [In the GUI](#in-the-gui)
- [Design decisions](#design-decisions)

## The project file

```ini
# lvdsim project (format: docs/projects.md)
[project]
name = demo
vhdl = 2008
jobs = 4
timeout = 120
coverage = on

[library util]
file = src/util_pkg.vhd
file = src/old_model.vhd vhdl=93 relaxed

[library core_lib]
file = rtl/core.vhd
file = rtl/fifo.sv define=DEPTH=16 incdir=rtl/include

[library work]
file = tb/tb.vhd

[test smoke]
top = work.tb
generic = MODE=0
expect = TEST PASSED

[test random]
top = work.tb
do = sim/random.do
seeds = 1 2 3
seed_generic = SEED
timeout = 30
```

Rules:

- Sections are `[project]`, `[library NAME]` and `[test NAME]`. Names use letters, digits, `_`, `-` and `.`; a name may be used once.
- One `key = value` per line. Keys that can repeat (`file`, `generic`, `plusarg`, `library_folder`, `map`) keep their order.
- Only whole lines starting with `#` (or `;`) are comments, so a value can hold `#` (`generic = MASK=16#FF#`, a regular expression).
- A value in double quotes keeps leading and trailing spaces; `file` paths with spaces are quoted (`file = "src/my file.vhd"`).
- Paths are relative to the directory of the project file unless absolute. Write forward slashes; they work on every platform.
- Unknown keys are kept, written back and reported as warnings, so a newer file still opens. Saving from the GUI writes the file in
  the canonical order shown above; comments of a hand-written file are not kept by a GUI save.

### `[project]`

| Key | Default | Meaning |
|-----|---------|---------|
| `name` | file name | shown in the GUI, the JUnit suite name |
| `vhdl` | `2008` | default VHDL standard: `93`, `2002`, `2008`, `2019` (also `1993` ... ) |
| `library_root` | `lvdsim_libs` | where libraries without a `path` are created (`<root>/<name>`); the compile state file lives here too |
| `library_folder` | | a folder of precompiled libraries (for example the output of Tools > Compile libraries); every library in it is visible |
| `map` | | `map = NAME DIR`: one more precompiled library |
| `jobs` | number of processors | regression runs at the same time |
| `timeout` | `600` | seconds per regression run |
| `wave` | `off` | record a waveform (FST) per run |
| `coverage` | `off` | collect coverage per run and merge it |
| `coverage_kinds` | every kind | `statement,branch,expression,toggle,fsm-state,functional` or letters such as `sbt` |
| `regression_dir` | `lvdsim_regression` | where the runs write their output |

### `[library NAME]`

| Key | Meaning |
|-----|---------|
| `path` | library directory (default `<library_root>/<NAME>`) |
| `file` | a source file, then options separated by spaces: `vhdl=STD`, `relaxed` (VHDL, nvc `--relaxed`, the effect of `vcom -explicit`), `define=NAME[=VALUE]` and `incdir=DIR` (Verilog, repeatable), `sv` / `verilog` / `vhdl` to force the language (else from the extension: `.v .vh` Verilog, `.sv .svh` SystemVerilog, everything else VHDL) |

List the files in the order you want them analysed; the compile step only changes that order where a file needs another one first
(see below). A library called `work` is fine: inside its own files `work` names it, and from other libraries it is reached by its name.

### `[test NAME]`

| Key | Meaning |
|-----|---------|
| `top` | `[library.]unit`; several top levels separated by spaces (all from one library). Without a library: `work` if the project has it, else the last library |
| `generic` | `NAME=VALUE` for nvc `-g` (repeatable); a path `/tb/u1/NAME=V` becomes `u1.NAME` |
| `plusarg` | `+name[=value]` (repeatable); nvc hands them to VHPI/VPI plug-ins |
| `do` | Tcl script run in the simulator (the command compatibility package is loaded first, so `run -all`, `quit` and friends work) |
| `commands` | inline Tcl commands instead of a script file (`commands = run 1 ms; quit`) |
| `seeds` | one run per seed: `seeds = 1 2 3` (nvc `--seed=N`) |
| `seed_generic` | also pass the seed as this generic (for random generators in VHDL test benches) |
| `timeout` | seconds, overrides the project |
| `expect` | regular expression (ECMAScript) the output must contain to pass |
| `stop_time` | without a script: end the run at this time (`1 ms`) |
| `wave`, `coverage` | `on`/`off`, override the project |

## Incremental compile

`lvdsim-cli compile demo.lvdproj` (GUI: **Compile**, F7) analyses what changed and what depends on it, one nvc call per file
(through the same translation as `vcom`/`vlog`), in dependency order:

```
> lvdsim-cli compile demo.lvdproj
-- Analysing util: src/util_pkg.vhd (changed)
-- Analysing core_lib: rtl/core.vhd (depends on src/util_pkg.vhd)
-- Analysing work: tb/tb.vhd (depends on rtl/core.vhd)
Analysed 3 of 3 files.
```

A file is analysed again when

- it is not in the compile state yet, or its library directory has no nvc library;
- its contents changed (Verilog: also an `` `include `` file), or its options changed (library, standard, defines, include directories);
- its last analysis failed;
- a file it depends on is analysed again.

A file that fails stops nothing else: files that depend on it are skipped, the others go on. `--all` analyses everything, file names
after the project analyse those files and their dependents, `--dry-run` only lists. `lvdsim-cli project status demo.lvdproj` shows the
state of every file without analysing. Exit status: 0 all analysed, 1 a file failed or was skipped, 2 the project could not be read.

**Dependencies** come from a small scanner (`tools/src/hdl_scanner.cpp`) that reads each file without comments and strings:

- VHDL: `entity`, `architecture X of E`, `package`, `package body`, `configuration`, `context` declare units; `lib.unit` anywhere
  (use clauses, context references, `entity lib.e` instances, selected names) for `work` or a library named in a `library` clause,
  component instances (`u : comp port map`), the entity of an architecture and the package of a body are references.
- Verilog/SystemVerilog: `module`, `interface`, `program`, `primitive`, `package` declare units; `pkg::` and module instances
  (`type #(...) name (`) are references; `` `include "file" `` is followed for the content hash.
- `work.x` means the file's own library; a name without a library (component, module, package import) is looked up in the file's
  library first, then in the other project libraries in project order. References outside the project (ieee, vendor libraries) are
  ignored.

Files are ordered so that every file comes after the files it depends on, otherwise in project order. A dependency cycle (rare;
usually a scanner miss) keeps project order and prints a warning.

**The compile state** is `<library_root>/.lvdsim-compile-state`, one line per file: `library|path`, time stamp, size, content hash,
options hash, ok or failed. A VHDL file whose time stamp and size did not change is not read again; Verilog files are read every time
because an include file may have changed. Deleting the file (or a library directory) makes the next compile analyse everything.

## Regressions

`lvdsim-cli regress demo.lvdproj -j 4 --junit results.xml` (GUI: **Regress**, Project > Run regression) first compiles what is out
of date (unless `--no-compile`), then runs every test, once per seed, as a batch nvc process, `-j` at a time:

```
Running 6 test runs with 4 jobs.
[1/6] PASS    smoke (0.08 s)
[2/6] FAIL    fail (0.07 s): exit code 1, 1 error - 10ns+0: y is not 43
    log: D:/proj/lvdsim_regression/fail/run.log
[3/6] TIMEOUT hang (3.04 s): timed out after 3 s
    log: D:/proj/lvdsim_regression/hang/run.log
[4/6] PASS    random@1 (0.08 s)
...
Coverage of 3 runs merged into D:/proj/lvdsim_regression/merged.ncdb: 87.5 % (35 of 40 items)
4 of 6 passed, 2 did not. Results: D:/proj/lvdsim_regression/results.txt
```

**Pass or fail.** A run passes when nvc started, ended within the time-out, exited with 0, printed no `** Error`, `** Failure` or
`** Fatal` message, and (with `expect`) its output matches the expression. The message counts matter because a scripted run (`do`,
`commands`) ends with status 0 even when an assertion of severity error fired. A run past its time-out is ended together with every
process it started.

**Output.** Each run has a directory `<regression_dir>/<test>` (with seeds `<test>_s<seed>`) with `run.log` (everything nvc printed),
`wave.fst` (with `wave = on`), `coverage.ncdb` (with `coverage = on`) and, for inline commands, `commands.do`. The regression writes
`results.txt` (`id<TAB>status<TAB>seconds<TAB>message` per run), and with coverage `merged.ncdb` (`nvc --cover-merge`) and
`merged.xml`. `--junit F` writes JUnit XML: one `testcase` per run (`name` is the run id, `test@seed` for seeds), `failure` for a failed
run, `error` with `type="timeout"` or `type="not-started"`, the last 200 lines of the log as `system-out`.

**Options.** `--test NAME` (repeatable; `NAME@SEED` for one seed), `--rerun-failed` (the runs that did not pass in `results.txt`;
the other lines of `results.txt` are kept), `--timeout S`, `--wave`, `--coverage`, `--out DIR`. Exit status: 0 all passed, 1 at least
one did not, 2 the project, the compile or the plan failed (nothing was run).

**How a run is started.** `nvc [-L ...] --std=STD --work=LIB:DIR --map=... [--seed=N] -e --jit --no-save [--cover=... --cover-file=...]
-gNAME=V ... TOP`, then `-r [--stop-time=T] [--wave=...] [+plusargs]` or `--do [--wave=...] lvdcompat.tcl SCRIPT`. The working
directory is the project directory (relative file names in test benches work as in the GUI). Each run gets `NVC_MAX_THREADS=1` and its
own directory as `TMP`/`TEMP`/`TMPDIR`.

## Importing a compile script

`lvdsim-cli project import compile.do -o demo.lvdproj` (GUI: Project > Import compile script) reads the script the way a simple Tcl
interpreter would: commands per line or separated by `;`, `\` continuation, `#` comments, `"..."` and `{...}` words, `set NAME VALUE`
variables used as `$NAME` / `${NAME}`, `do other.do` (followed), `-f` argument files. It understands

- `vlib DIR` (library named like the last part of DIR, at DIR), `vmap NAME DIR` (a library's directory; a mapped library that gets no
  files becomes a `map` entry),
- `vcom [-work L] [-93|-2002|-2008|-2019] [-explicit] files` and `vlog [-work L] [-sv] [+define+..] [+incdir+..] files`; the most
  common VHDL standard becomes the project's, other files get `vhdl=`,
- `vsim [-c] [-do script|"commands"] [-g/-G...] [-sv_seed N] [+plusargs] [lib.]top` as tests named after the top level.

`[Library]` mappings of `lvdsim.ini` next to the script (or `--ini`) give library directories and the external libraries. Commands it
does not understand (`if`, `foreach`, procedures, `cd`) are listed as warnings and left out. Paths are written relative to the project
file. It never overwrites an existing project.

## In the GUI

The **Project** menu: New, Open, Recent projects, Save, Save as, Close, Import compile script, Compile out-of-date (F7), Compile all,
Run regression, Rerun failed tests. Opening a project makes its directory the working directory, so library paths, the simulator's file
names and `lvdsim.ini` are relative to it, and every project library is visible to simulations started from the window (`-L`). The
docks are described in the tutorial ([Projects, compiling and regressions](tutorial.md#projects-compiling-and-regressions)).

Headless (tests and screenshots): `lvdsim-gui --project F.lvdproj [--project-compile] [--regress] [--project-select NAME]
[--project-touch FILE] [--import-do SCRIPT OUT.lvdproj] [--dock-width NAME W] --screenshot out.png`.

## Design decisions

- **Ini-like text, not TOML or XML.** One source file per line gives one-line diffs for added, removed and moved files; there is no
  nesting to get wrong, and the reader is about 300 lines of plain C++ in `tools/src/project.cpp` (TOML would need a third-party
  library outside the licence list, or a partial reader that people would expect to be complete). Only whole-line comments, so values
  can contain `#`.
- **Own unit scanner instead of `nvc --print-deps`.** `--print-deps` reports dependencies between units already analysed into a
  library: it cannot order a project that was never compiled, does not see a dependency added by an edit until the file is analysed,
  and names library files rather than source files. The scanner works on the sources before any analysis, in a few milliseconds per
  file. It can miss exotic forms (a component bound only by a configuration, a macro that expands to an instance); then the order falls
  back to project order, and nvc itself reports a unit that is out of date.
- **One nvc call per file.** Errors belong to one file, the state of each file is exact, and a failure only skips the files that depend
  on it. Costs about 50 ms per file for starting nvc.
- **Elaboration in memory for regressions (`--jit --no-save`).** Parallel runs of the same top level would otherwise write the same
  elaborated unit into the work library at the same time. JIT code can be slower than the default ahead-of-time code for long
  simulations.
- **One scheduler for the command line and the GUI.** `lvd::tools::regress::Runner` and `lvd::tools::build::Runner` never block; the
  command line loops over `step()`, the GUI calls it from a timer, so both behave the same.
