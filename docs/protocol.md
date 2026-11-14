# GUI to kernel protocol

Status: JSON schema version 1 and the live waveform stream (format `LVW 1`) are implemented by the fork. Change sets 04 (port modes:
`describe -json` member `"mode"`, stream record `D`) and 05 (stream records the time each run reached, `run` counts from it) added to
both without a version change. Change set 06 (source line breakpoints, statement stepping, `stack`, `locals`) adds commands, members and
the run status `"stopped"`, also as schema version 1 (see "Source debugger"). Change sets 07 to 11 (2026-11-13, `kernel/PATCHES.md`)
keep schema version 1 and `LVW 1`: arrays in the stream carry only the elements that changed (07), Verilog memories are dumped word by
word with `--dump-arrays` (08), new commands `deposit` and `readmem` and array elements in signal names (09), the stream is flushed when a
process stops at a breakpoint and `breakpoint add` checks the line (11). Change set 12 (2026-11-14) brings back `restart` (reply status
`"restarted"`, also as the reply of a `run` or `step` interrupted by it) and adds the stream record `R`, still schema version 1 and `LVW 1`
(see "Restart"). Change sets 30 to 39 (2026-11-14) keep schema version 1 and `LVW 1`: 35 gives `describe` replies for Verilog signals
(`"type"` such as `"wire [3:0]"` or `"logic [7:0] [0:3]"`, the call aborted before), 36 adds the stop reason `"stop"` (Verilog `$stop`).
Change set 41 (2026-11-14) adds the run status `"interrupted"` (a run ended by a break request, see "Interrupting a run"), schema version 1.
Change set 42 (2026-11-14, pull request 10) adds `describe -location` (source locations of declarations, design units and instantiations, see
"Source locations"), schema version 1.

## Transport
- The GUI spawns `nvc -i <unit>` as a child process and keeps stdin/stdout.
- One Tcl command per request. The GUI appends a sentinel echo (`puts "<<lvd:<id>>>"`) after each
  command so replies can be framed without parsing the prompt.
- stderr is shown in the transcript and never parsed.

## Reply formats
- Native NVC commands: free text, parsed by `gui/sim/parsers/` with fixtures.
- Fork commands with `-json`: one JSON object on one line. Schema versioned by a `"v"` field.

## Waveforms
- Never requested over the pipe. The GUI starts nvc with `--format=live --wave=<temporary file>`; the kernel appends an
  append-only text stream (hierarchy first, then value changes; format described at the top of `src/rt/wave-live.h` in the fork)
  and flushes it when a `run` or `step` ends and whenever its buffer fills during a long run.
- `SimWindow` polls the file about every 60 ms with `LiveReader` (`libwave/include/lvdwave/live_reader.hpp`) and appends the new
  records to the in-memory store the wave pane draws from. A waveform therefore grows on screen while the simulation runs; there
  is no "simulate, write a file, load it" step. The whole design is streamed, so `add wave` only chooses rows.
- FST is not read while it is being written (the file is complete only when it is closed). FST is used for saving a trace in
  batch runs (`-lvdwave`).

### Live stream records (format `LVW 1`)

The header stays `LVW 1`; records were only added, and a reader skips a record it does not know (`LiveReader` counts it in
`badLines` and goes on). One record per line:

| Record | Meaning |
|--------|---------|
| `TS <exp>` | time unit 10^exp seconds (NVC: -15) |
| `S <scope type> <name>`, `U` | enter, leave a scope |
| `V <handle> <var type> <width> <kind> <name>` | a signal (kind `L`, `R`, `S`); a handle seen again is an alias, for example a port collapsed into the signal it is connected to |
| `D <dir>` | **port mode** of the entry of the preceding `V` record: `in`, `out`, `inout`, `buffer`, `linkage` or `internal`. Written after every `V` record by kernels with change set 04 (`kernel/PATCHES.md`); an alias has its own. Elements of a record port have the mode of the port; Verilog ports (`input`, `output`, `inout`) map to `in`, `out`, `inout`. Absent in older streams: the reader leaves `lvd::wave::PortDirection::Unknown` |
| `E` | end of definitions |
| `#<time>` | the following values happen at this time |
| `<handle> L <bits>`, `<handle> R <double>`, `<handle> S <text>` | a value change |
| `R <base>` | **restart** (change set 12): time goes back to zero; the values so far belong to an earlier run. Definitions are not repeated; from here on a value record gives the handle `h` of a `V` record as `h + base`. `base` grows by the number of handles at every restart (`R 2`, `R 4` for two handles). |

**Arrays** (change set 07): an array dumped as one entry per element (`--dump-arrays`: memories, arrays of integers, of enumeration
values) writes at each time only the elements whose value changed; the first time record after the definitions has all of them. Older
kernels wrote every element at every change of any element (a 64K word RAM written every clock gave 5.4 GB for 20 us instead of 5.8 MB).
A reader keeps the last value of each entry, so nothing changes for it.

**Verilog memories** (change set 08): `reg [7:0] m [0:N]` is dumped with `--dump-arrays` as one entry per word, `m[0][7:0]` ... `m[N][7:0]`
(`m[0]` without a packed range, one index per unpacked dimension), the same names as VHDL memories. Without `--dump-arrays` it is not
dumped (older kernels wrote one entry `m[7:0]` holding word 0).

**Stopped at a breakpoint** (change set 11): when a process stops at a source line (or a step ends inside a time step) the kernel writes
a `#<now>` record (the time step in progress) and flushes the stream, so the wave window shows everything up to the stop. Values of that
time step that are still to come follow later under the same time record; the time is the one `model_stop_time()` gives, never later
than the time actually reached.

**End of a run:** each `run` and `step` ends with a `#<time>` record for the time the simulation has reached, also when no value changed
then (change set 05). After `run 100 ns` on a design whose last event is at 40 ns the stream ends with `#100000000`; older kernels wrote the
time of the last event (`#40000000`). The store's `endTime()` follows the last time record, so the end of data in the wave window (the cut
after which nothing is drawn, `validEnd`) and Follow use the time the run reached. A run without a time limit (`run`) ends at the last event.

**Restart** (change set 12): after `restart` the stream goes on in the same file with `R <base>`, then `#0`, the values at time zero
and the new run. The handles are shifted so that a reader that does not know `R` skips it and then skips every value of the new run
(the handles are not defined for it): it keeps showing the old run and only sees the time records, it never mixes the two runs.
`LiveReader` (lvdsim) checks that `base` is the previous base plus the number of handles (else the record is a bad line and nothing
changes), clears every value of the store with `WaveStore::clearValues()` (scopes, signals, aliases and their ids stay, so rows of the wave
window and references stay valid) and sets `PollResult::restarted`. Every `Signal` has a `generation()` that `clearChanges()` increments;
`NumericIndex`, `ExpressionSignal`, `SignalCompare`, `ListIndex` and `MemoryImage` start again from the first change when it changed (also
when the new run is already longer than the old one when they next update). Decision: one store, values cleared, rather than a new
segment per run: the GUI keeps its rows, cursors and expressions, and nothing in libwave needs to know about runs. A waveform written as
FST or VCD (`--wave` without `--format=live`) is closed and written again from time zero at a restart.

## JSON replies, schema version 1

The `lvdsim` kernel branch (change set `03-interactive-mode`, `kernel/PATCHES.md`) adds a `-json` option to `run`, `step`, `find`, `examine`, `describe`, `processes` and
`breakpoint`. The reply is the Tcl result of the command: one line, one JSON object, UTF-8, no trailing spaces.
The shell prints it after the prompt like any other result, so `OutputFilter` frames it the same way.

Rules for every reply:
- `"v"` is always the first member and holds the schema version, currently `1`. A change that renames or
  removes a member, or changes its meaning, increments it. New members may appear without a version change,
  so readers must ignore members they do not know.
- Errors are not JSON. A failing command still raises a Tcl error, which the shell prints on stderr as
  `** Error: text`. A GUI request has succeeded when stdout carries an object.
- Times are integers in femtoseconds (`now`, `time`). They fit a double exactly up to 2^53 fs (about 9000 s).
- Without `-json` every reply is unchanged.

| Command | Reply |
|---------|-------|
| `run [-json] [time unit]` | `{"v":1,"now":N,"delta":D,"status":S}` and, when S is `breakpoint`, `"breakpoint":{"id":I,"time":T}`; when S is `stopped` (06), `"stop":{...}` (see "Source debugger") |
| `step [-delta\|-time\|-into\|-over\|-out] [-command script] [-json]` | same as `run` |
| `find signals\|regions [-r] [-json] pattern` | `{"v":1,"kind":"signals","names":["/a","/b/c"]}` |
| `examine [-radix r] [-json] name...` | `{"v":1,"values":[{"name":"/a","value":"5"}]}`; the value is the text NVC prints |
| `describe [-json] name...` | `{"v":1,"objects":[{"name":"x","path":"/x","kind":"signal","type":"INTEGER","mode":"internal"}]}`; regions have `"kind":"region"` and no `type` or `mode` |
| `describe [-json] -location name...` (42) | the same objects with the location members of "Source locations"; also accepts generics, constants and variables (`"kind":"generic"`, `"constant"`, `"variable"`) |
| `processes [-json] [glob]` | `{"v":1,"processes":[{"name":"p","path":"/uut/p","state":"waiting","file":"a.vhd","line":9}]}` |
| `breakpoint add -json t unit` | `{"v":1,"id":I,"time":T}` |
| `breakpoint add [-command script] -json file:line` (06) | `{"v":1,"id":I,"file":"tb.vhd","line":L}`; `file` as given. Since 11 `line` is the line where the breakpoint is set: when the requested line has no statement it is the next line with one and `"requested":R` gives the line asked for. A file that is not a source of the design, or no statement at or after the line, is an error |
| `breakpoint list -json` | `{"v":1,"breakpoints":[{"id":I,"time":T}]}` ordered by time then id, then (06) the source line breakpoints in creation order: `{"id":I,"file":"tb.vhd","line":L,"hits":H}` |
| `breakpoint delete -json id\|all` | `{"v":1,"deleted":N}`; ids of both kinds |
| `stack -json` (06, only while stopped) | `{"v":1,"frames":[F,...]}`, innermost frame first; F as below |
| `force -json signal value` (09) | `{"v":1,"name":"/tb/mem(5)","width":8}`: the name as given and the number of scalar elements set. Without `-json` the reply is empty, as before |
| `deposit -json signal value` (09) | the same as `force` |
| `readmem -json [-binary] file signal [start [end]]` (09) | `{"v":1,"name":"/tb/mem","words":N}`; without `-json` the reply is N |
| `locals [-frame n] -json` (06, only while stopped) | `{"v":1,"frame":n,"variables":[{"name":"count","kind":"variable","type":"INTEGER","value":"3"}]}` |

Meaning of the fields:
- `describe` signal objects carry a `"mode"` member (**port mode**, change set 04): `"in"`, `"out"`, `"inout"`, `"buffer"` or
  `"linkage"` for a port (also a port collapsed into the signal it is connected to, and every element of a record port; Verilog
  `input`, `output`, `inout` give `in`, `out`, `inout`), `"internal"` for a signal declared in the region. It is absent for a VHDL-2019
  mode view port and with older kernels: a reader must treat an absent member as "not known" (`lvd::gui::PortMode::Unknown`, a neutral
  icon). Only the `-json` reply has it; the plain `describe` reply is unchanged. Schema version stays 1 (a new member).
- `now`, `delta`: simulation time and delta cycle number after the command, the same values as the Tcl
  variables `$now` and `$deltas`.
- `run <time> <unit>` counts from the time the previous run reached (change set 05), not from `now`: after `run 100 ns` with the
  last event at 40 ns, a second `run 100 ns` runs to 200 ns. `now` itself still stays at the last event (see the breakpoint rule
  below); the live stream records the time reached.
- `status`: `paused` (stopped by the requested time or step, more to do), `finished` (no events left),
  `breakpoint` (a time breakpoint was reached and deleted), `stopped` (06: a process stopped at a source line, the run is
  still in progress; see "Source debugger"), `interrupted` (41: the run was ended by a break request before its next time step;
  see "Interrupting a run"). Readers written before 06 reject `stopped`, before 41 `interrupted`, as an unknown status.
- A time breakpoint is reached when every event up to its time has been processed. If nothing happens at that
  exact time, `now` stays at the last event before it and `breakpoint.time` gives the breakpoint time.
- `state` of a process: `ready` (queued to run in the current or next cycle), `delayed` (waiting for a
  timeout), `waiting` (waiting for a signal event or suspended for good). `file` and `line` are `null` when
  the location is unknown. `file` is the name as given to `nvc -a`.
- Signal names in `examine`, `force`, `noforce` and `deposit` (09) may name an array element: one index per level in parentheses,
  `/tb/mem(5)`, `/tb/mem(5)(3)`; `[5]` is accepted too but needs Tcl quoting. Indexes are values of the index range (`mem(10 downto 7)`
  takes `mem(9)`); out of range is an error. Verilog memories take one index per unpacked dimension (`/top/m(3)`). `examine` shows
  Verilog variables and words as binary strings of `0 1 z x` (older kernels stopped with a fatal error on Verilog variables).
- `deposit` writes the driving value in the next delta cycle of the next `run` (VHDL driving value deposit): `examine` shows it after the
  next `run`. It lasts until a driver of that element changes it; writes of a design to other elements do not undo it. `readmem` deposits
  the words of a `readmemh` (default) or `readmemb` (`-binary`) file. Both are refused while a process is stopped, like `force`.
- `step -delta` (default) runs one simulation cycle, which is a delta cycle or the first cycle of a time
  step. `step -time` finishes the current time step including its delta cycles, or the whole next time step
  if the current one has finished.

Events are not pushed. A breakpoint is reported in the reply to the `run` or `step` that reached it, so the
GUI learns about it from the reply to its own request. Without `-json`, the same event is the result text
`breakpoint N at T fs`. This also holds for source line stops (06): the stop is the reply of the `run` or `step` that
started or resumed the run.

## Source debugger (change set 06)

Commands (all native, in the fork's shell; `docs/nvc-tcl-commands.md` has the plain text forms):

| Command | What it does |
|---------|--------------|
| `breakpoint add [-command script] file:line` | stop every process that starts to execute the line (since 11: the file must be a source of the design and the line must have a statement or one must follow it; a line without one moves to the next statement, reported in the reply); `file` is the whole path known to the kernel (as given to `nvc -a`) or its last components (`tb.vhd`, `src/tb.vhd`); separators `/` and `\` are equal, case is ignored on Windows. The line number follows the last colon, so `C:/x/tb.vhd:12` works |
| `run [-json]` while stopped | resume; a time is refused while stopped |
| `step [-into] [-command script] [-json]` | run until a process starts a new source line, in any process, entering called subprograms. Default when stopped; also allowed when not stopped (then it runs until the first statement) |
| `step -over [...]` | run until the stopped process starts a new line in the current frame or a caller (calls are run through; after `wait` it stops when the process resumes) |
| `step -out [...]` | run until the stopped process is back in the caller of the current frame; in the process frame itself it is `-over` |
| `stack [-json]` | the call stack of the stopped process |
| `locals [-frame n] [-json]` | variables of frame `n` (0 = innermost) |

While a process is stopped: `force`/`noforce`/`deposit` with arguments, `readmem`, `run <time>`, `step -delta` and `step -time` are refused (error),
everything else works (`examine`, `describe`, `processes`, `breakpoint`, other Tcl). `exit` exits at once, as always.

Change set 40: a time breakpoint added or deleted while stopped moves the end of the suspended run (before, the run went on to the limit
it had when it started, for ever with `run -all` on a free-running clock, and reported the breakpoint afterwards). A `$stop` in a run
started without line breakpoints leaves compiled code without the debug hook, which cannot be installed in the middle of a run: there
the statement steps are refused (`cannot step from a $stop ...`, the prompt comes back) and `breakpoint add file:line` prints a note that
it applies from the next run.

**Stop reply** (the reply of the `run` or `step` that started or resumed the run; `-json` decides its form):

    {"v":1,"now":6000000,"delta":0,"status":"stopped","stop":{"reason":"breakpoint","id":1,"process":"/pulser",
     "frame":{"level":0,"kind":"procedure","name":"toggle","symbol":"WORK.TCLDBG1.TOGGLE(sJIIQS)","file":"/src/tcldbg1.vhd","line":30}}}

(one line). `reason` is `breakpoint` (with `id`), `step`, or `stop` (change set 36: the process called the Verilog `$stop`; the shell
waits at the prompt as for a breakpoint and `run` continues after the `$stop`). `process` is the path of the stopped process as `processes`
gives it. `frame` is frame 0 of `stack`. Without `-json` the reply is the text `breakpoint 1 in /pulser at /src/tcldbg1.vhd:30`,
`stopped in /pulser at ...` or `$stop in /initial#2#2 at tb.v:12`. The reason `stop` is a new value in schema version 1; a reader that
rejects unknown reasons must accept it (lvdsim's `parseStopReply` in `gui/sim/parsers/json_replies.cpp` reads it as `StopReason::Stop`
and also takes a `$stop` reply without a frame).

**Frame** `F`: `{"level":0,"kind":"procedure","name":"toggle","symbol":"WORK.TCLDBG1.TOGGLE(sJIIQS)","file":"...","line":30}`.
`kind` is `process` (always the outermost frame; `name` is the process path), `function`, `procedure` or `other` (for example a
Verilog task; `name` is then the kernel's unit name). `symbol` is the kernel's unit name, unique per overload. `file` and `line` are
`null` when unknown. A caller frame shows the line of the call.

**Variables** of `locals`: `name` (lower case), `kind` (`variable`, `constant` or `parameter`), `type` (as the kernel prints it), `value`
in VHDL literal syntax (`3`, `'1'`, `"0101"`, `"abc"`, `1.5`, `(a => 7, b => '1')`, `(3, 4, 5)`, `("01", "10")`) or `null` when the
shell cannot show the type (access, file and protected types and records or arrays containing them, multi-dimensional arrays; signal and
file parameters). Arrays the value printer has no form for are shown element by element, at most 256 elements followed by `...`.
What is listed:
- process frames: the process's variables;
- procedure frames of procedures that can wait (they keep their state): their variables and parameters;
- functions, and procedures that cannot wait: nothing (their variables live in machine registers in this version);
- Verilog: nothing (Verilog variables are signals: use `examine`).

**How the GUI learns about a stop.** The stop is printed as the reply of the command that let the simulation run, right after the
prompt that preceded that command, and the shell then prints a new prompt and reads commands while the run is suspended inside the
kernel. With `OutputFilter`'s rule (a reply belongs to the command sent after the prompt it follows) nothing changes:

    % run -json                        <- command 0
    {"v":1,...,"status":"stopped",...} <- reply of command 0: the run is suspended, the shell is ready
    % stack -json                      <- command 1
    {"v":1,"frames":[...]}             <- reply of command 1
    % run -json                        <- command 2: resumes
    {"v":1,...,"status":"finished"}    <- reply of command 2 (or "stopped" again)
    %

So `NvcSession::busy()` becomes false at a stop (a prompt was printed) and the GUI may send commands. A command evaluated by a
breakpoint's `-command` script does not change the form of the reply (the command typed at the prompt decides it). If standard input
ends while stopped, the shell finishes the current cycle and quits.

**Cost.** The kernel keeps a table of where each source line starts in the interpreter's code (one 16 byte entry per line and basic
block). Without line breakpoints and stepping nothing else changes: same intermediate code, same interpreter loop, same compiled code.
While a line breakpoint exists or a step is pending, the kernel runs all code in the interpreter (compiled functions are switched back to it
and tier-up is suspended; the compiled code is used again at the next `run` without line breakpoints) on copies of the code with a marker
at each line start, which calls the shell's hook (one line number comparison per breakpoint). Measured (`docs/kernel-tests.md`): no
difference without breakpoints; a loop-heavy design runs about 8 times slower with a line breakpoint set than with compiled code.

## Interrupting a run (change set 41)

A `run` (also `run` given while stopped at a source line, and `step -into`, which runs until a statement) can be ended from outside: the
front end sends SIGINT to nvc (Linux, macOS), or on Windows a console `CTRL_BREAK_EVENT`. The shell then ends the run before its next time
step, so the time reached is complete (all delta cycles of `now` are done; the live stream has it) and replies as usual:

    {"v":1,"now":3842735000000,"delta":1,"status":"interrupted"}      (plain: "interrupted at 3842735000000 fs")

- The run counts as ended at `now`: `run <time>` afterwards runs to `now` + time, `run` without a time goes on until no events are left, as
  after a time breakpoint. Time breakpoints after `now` stay. If the run reached a time breakpoint or the end of the events in the same
  step, that status wins.
- The commands the front end wrote while the run was going are read after the reply, in order.
- Not running (idle at the prompt, or a process stopped at a source line, where the shell reads commands): `nvc -i` ignores the request.
  A `--do` script outside a run ends nvc as Ctrl-C always did.
- A process that loops without `wait` never returns to the kernel, so the run cannot end. A second request before the run reached its next
  time step ends nvc at once (exit status 130; data written at exit, such as coverage counts, is lost). lvdsim never sends a second one: it
  asks the user whether to kill the process instead.
- Delivery on Windows: a console event reaches every process of a process group attached to one console. lvdsim starts nvc with
  `CREATE_NEW_PROCESS_GROUP` (its process id is then the group id) and either lets it share the GUI's console (when the GUI has one) and calls
  `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)`, or gives it a console of its own (`CREATE_NO_WINDOW`, the usual case for a GUI) and
  wraps the call in `AttachConsole(pid)` / `FreeConsole()`. `CTRL_C_EVENT` cannot be sent to a process group. Code:
  `NvcSession::interrupt()` in `gui/sim/nvc_session.cpp`; the GUI reads the status as `RunStatus::Interrupted`.
- Kernels before 41 die on SIGINT and on a console break (the GUI then reports that the simulation ended).

## Restart (change set 12)

`restart [-nobreakpoint] [-noforce] [-json]` starts the elaborated design again at time zero in the running process.

    {"v":1,"now":0,"delta":0,"status":"restarted","breakpoints":2,"forces":1}

| Member | Meaning |
|--------|---------|
| `now`, `delta` | always 0 |
| `status` | `"restarted"` |
| `breakpoints` | time and source line breakpoints kept (time breakpoints already reached were deleted when reached; `-nobreakpoint` deletes all) |
| `forces` | forces applied again: the values given with `force` that were still in effect, one entry per contiguous range (`-noforce`: none) |

- Kept: Tcl procedures and variables, breakpoints (line breakpoint hit counts start again at 0), shell forces, the waveform selection
  (the stream goes on, see "Restart" under Waveforms). Not kept: deposits, values in VHPI handles held in Tcl variables.
- **While a process is stopped at a source line** the kernel cannot drop the model in the middle of the process: `restart` returns at
  once with an empty reply and resumes; the rest of the cycle runs without stopping, then the `run` or `step` that was interrupted
  restarts and its reply is the restart reply above (status `"restarted"` instead of a run status). This is the same pattern as `run`
  given while stopped. Readers: `isRestartReply` / `parseRestartReply` (`gui/sim/parsers/json_replies.hpp`); check `isRestartReply` before
  `parseRunReply`, which rejects that status. Command helper: `restartCommand(keepForces, keepBreakpoints)` in `gui/sim/debug_commands.hpp`.
- Errors: `restart command is not supported` when the shell was started with VHPI plugins (`--load`) or without a design; a syntax error
  for unknown options.
- Cost (Windows, a mixed design of 2400 instances, 10 003 signals, `docs/kernel-tests.md`): about 22 ms, against 160 to 190 ms for a new
  `nvc -i` process up to its prompt (before the GUI has re-added its rows).
- Sources recompiled since the start are not seen: the kernel keeps the elaborated unit and the generated code it loaded. The GUI starts a
  new process in that case.

## Coverage (change sets 13 and 14)

**From the shell** (13): a design elaborated with `--cover` (`nvc -e --cover=... --cover-file=F top`) and run with `nvc -i` or `--do` now
loads the database like `nvc -r` and writes the run-time counts to `F` (default `<top>.ncdb`) when the shell ends: end of input, end of the
scripts, or `exit`. Before, the shell never wrote them (every count 0).

`coverage save [-json] [file]` writes the counts so far during a session (to `F`, or to `file`):

    {"v":1,"file":"top.ncdb","now":15000000}

`now` is the time the simulation has reached (fs). Plain reply: the file name. Errors: `coverage was not enabled when the design was
elaborated`; `cannot write coverage database <file>: <reason>`. Every write is complete (the counts of the session so far, never counted
twice), so the GUI can save, export and show the database while the simulation goes on. **Restart** keeps the coverage data: the counts of
all runs of the session add up, like two `nvc -r` runs on one database. For the counts of one run only, start a new process (elaborate
again).

**Export** (14): `nvc --cover-export --format=json [--relative=DIR] [-o FILE] DB...` writes one JSON object on one line, schema version 1
(`"v"`; a member that changes meaning or goes away increases it, new members may be added):

| Member | Meaning |
|--------|---------|
| `mask` | nvc's coverage mask (`cover_mask_t` bits: 1 statement, 2 branch, 4 toggle, 8 expression, 16 FSM state, 32 functional, higher bits are options) |
| `kinds` | the kinds the mask enables: `statement`, `branch`, `expression`, `toggle`, `state`, `functional` |
| `files` | source file names (relative to `--relative` when given); other members refer to them by index, -1 unknown |
| `scopes` | the scope tree in depth-first order, the same scopes in the same order as the XML export: `name`, `parent` (index, -1 only for the first, the library), `block` (`ENTITY-ARCHITECTURE`, instances only), `file`, `line` (0 unknown) |
| `bins` | every bin, in the order of the XML export: `scope` (index), `kind`, `hier` (full name, e.g. `WORK.TOP.U1._P0._S0._B0.BIN_TRUE`), `file`, `line`, `column` (1-based, 0 unknown; the location of the item), `hits`, `threshold` (hits needed, the item's `atleast`), `flags` (nvc bin flags), `excluded`, `user_excluded`, `unreachable` (a toggle that cannot happen: `hits` is 0) |

Strings escape `"`, `\` and control characters; names are ISO-8859-1 and bytes above 0x7e are written as `\u00XX`, so extended identifiers
(`\odd\name\`, `\y"q\`) give valid JSON. lvdsim reads it with `lvd::tools::cov::parseExportJson` (or `parseExport`, which takes either
format); `exportArgs(dbs, file, ExportFormat::Json)` plans the call. A kernel without change set 14 answers `unknown format 'json'`: use
`ExportFormat::Xml` then.

## How the GUI reads replies
- `OutputFilter` cuts every line that starts with `{"v":` out of the transcript text and reports it with the number of the
  prompt that precedes it. `NvcSession::send` returns the command number, so a reply belongs to the command with index
  prompt - 1. A command that fails has no reply; its handler is dropped when the shell is idle again.
- `gui/sim/parsers/json_replies.{hpp,cpp}` read the objects with QJsonDocument and return an error text for invalid
  JSON, a non-object, a missing or unknown `"v"`, or a missing member. `SimWindow` shows such a reply in the transcript.
- The shell only takes whole numbers for times (`breakpoint add 2.5 ns` is an error); the GUI converts decimals first.
- The command compatibility layer (`tcl/compat/lvdcompat.tcl`) passes `-json` through `run`; `step`, `breakpoint` and `processes` are native.
- The prompt (number) a reply follows is counted without the prompts on its own line before it: output of Tcl `puts` (the stop
  probe) comes right after the prompt, before the shell's echo, and may arrive in the same read as that prompt.
- The GUI session sets `::auto_noexec`: in the interactive shell Tcl would run an unknown command word as a program with the shell's
  standard input (`pause`, `sort`, `more`), which then reads the GUI's commands and the shell never answers again.
- `LVDSIM_SHELL_TRACE=<file>` makes `NvcSession` append every chunk written to and read from the shell, with a time stamp in
  microseconds and control bytes escaped (`\x01\x02%\x01\x02 ` is the prompt); off when unset.

## Port mode
The Objects pane draws an icon for the kind of every signal (in, out, inout, buffer, internal; see the tutorial). Where the modes come from:
- the **live stream** (no request needed): the `D` record after each `V` record (see "Live stream records"). `LiveReader` stores it per
  entry (`lvd::wave::Signal::direction()`, aliases included) and `ObjectsPane` shows it. FST files carry the same information
  (`fstVarDir`) and `loadFst` reads it; FST "implicit" stays Unknown because every producer writes it when it has no direction.
- `describe -json` member `"mode"` (on demand, for other clients). The GUI does not send it; `ObjectsPane::setPortModes` still takes such
  modes and they win over the store.
- `linkage` has no icon of its own: the pane shows the neutral one.
- Not shown: ports that NVC does not dump. A pure Verilog submodule's ports are collapsed into the parent's nets and have no entry of
  their own (neither in the stream nor in the shell); an input record port collapsed into the parent's record has none either.

## Source locations
`describe -location` (change set 42, `kernel/upstream/10-shell-describe-locations`) tells where things are declared. The GUI uses it for View
Declaration (Objects, Wave, List, Locals), View Definition and View Instantiation (Sim); see the tutorial.

```
% describe -json -location /uut/clk /uut/
{"v":1,"objects":[{"name":"clk","path":"/uut/clk","kind":"signal","type":"BIT","mode":"in","file":"src/sub.vhd","line":"3","column":"12"},
 {"name":"uut","path":"/uut/","kind":"region","unit":"work.sub(rtl)","unit_file":"src/sub.vhd","unit_line":"8","inst_file":"top.vhd","inst_line":"53"}]}
```

- Objects (signals, ports, Verilog nets and variables, and the new kinds `generic`, `constant`, `variable`): `"file"`, `"line"`, `"column"` of the
  declaration (line and column 1 based; a tab counts as one column). A port collapsed into the signal of an outer region gives the port
  declaration of the region asked about (`/uut/clk`: the port of `sub`, not the `clk` of the top level it is merged with); through a component
  instance it is the component's port. An element of a record or array signal gives the declaration of the whole signal.
- Regions: `"unit"` is `"lib.entity(arch)"` for a VHDL instance (a component instance gives the bound unit), `"lib.module"` for a Verilog
  instance (lower case), the label for a block or a generate iteration; `"unit_file"`/`"unit_line"` the start of that architecture, module or
  statement; `"inst_file"`/`"inst_line"` the statement in the parent that instantiates it (a generate iteration: the generate statement). The top
  level has no `inst_*` members. A record or array signal that shows up as a region has `"file"`, `"line"`, `"column"` instead.
- Paths: signals and regions as for `describe`; generics, constants and shared variables of a region as `<region>/<name>` (`/uut/width`),
  constants and variables of a process as `<region>/<process>/<name>` (`/uut/reg/count`, `/p/v` for a process of the top level). Variables of
  subprograms and Verilog parameters are not found.
- File names are as nvc knows them: as given at analysis, often relative to the folder the analysis ran in.
- As every value of the `describe -json` reply, the location values are JSON strings (`"line":"3"`); `parseDescribeReply` takes strings and
  numbers. The plain reply (a Tcl dictionary) has them too: `dict get [describe -location /uut/clk] line`.
- Without `-location` nothing changes. A kernel without the option refuses it (`describe` usage error), an older lvdsim kernel without the
  members answers without them: `DescribedObject::hasLocation()` is false.

How the GUI uses it (`gui/app/declaration_locator.*`, `gui/app/sim_window_source.cpp`):
- At the first prompt of a session it sends `describe -json -location {/}` once: a reply with `"unit_file"` means the kernel reports locations;
  otherwise (no members, or the option refused) every View Declaration / View Definition / View Instantiation entry is disabled with a tool tip
  that says so. Without a running simulation they are disabled too ("Needs a running simulation").
- Each path is asked on demand (`describe -json -location {<path>}`, sent like the `stack` and `locals` queries) and kept for the session; a new
  session or a restart clears the cache.
- Store paths map to shell paths by dropping the top-level name (`t.uut.clk` -> `/uut/clk`, scope `t.uut.g(0)` -> `/uut/g(0)/`, `t` -> `/`) and
  a trailing index range (`t.mem[3]` -> `/mem`).
- Relative file names are looked up in the folder the simulation started in, the project folder, the library folders (`--work`/`--map` of the
  session, the libraries of the Library pane, the library folders setting) and their parents, the coverage folders and the current folder; a file
  found nowhere is reported in the transcript with the folders searched.

## Open items
- Native command list: see `docs/nvc-tcl-commands.md`.
- Runtime dump control (add and remove signals from the live stream while running) is not implemented.
- `restart` does not load a design recompiled after the start: the GUI starts a new process when sources changed.
- Source debugger (06) limits: no parameters or variables of functions (and of procedures that cannot wait), no conditions on
  breakpoints (a `-command` script can call `run` to go on), stops only in processes and what they call (not in resolution or
  conversion functions run outside a process), `run <time>` is refused while stopped.
