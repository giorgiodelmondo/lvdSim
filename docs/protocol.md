# GUI to kernel protocol

Status: JSON schema version 1 and the live waveform stream are implemented by the fork.

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
| `run [-json] [time unit]` | `{"v":1,"now":N,"delta":D,"status":S}` and, when S is `breakpoint`, `"breakpoint":{"id":I,"time":T}` |
| `step [-delta\|-time] [-json]` | same as `run` |
| `find signals\|regions [-r] [-json] pattern` | `{"v":1,"kind":"signals","names":["/a","/b/c"]}` |
| `examine [-radix r] [-json] name...` | `{"v":1,"values":[{"name":"/a","value":"5"}]}`; the value is the text NVC prints |
| `describe [-json] name...` | `{"v":1,"objects":[{"name":"x","path":"/x","kind":"signal","type":"INTEGER"}]}`; regions have `"kind":"region"` and no `type` |
| `processes [-json] [glob]` | `{"v":1,"processes":[{"name":"p","path":"/uut/p","state":"waiting","file":"a.vhd","line":9}]}` |
| `breakpoint add -json t unit` | `{"v":1,"id":I,"time":T}` |
| `breakpoint list -json` | `{"v":1,"breakpoints":[{"id":I,"time":T}]}` ordered by time then id |
| `breakpoint delete -json id\|all` | `{"v":1,"deleted":N}` |

Meaning of the fields:
- `now`, `delta`: simulation time and delta cycle number after the command, the same values as the Tcl
  variables `$now` and `$deltas`.
- `status`: `paused` (stopped by the requested time or step, more to do), `finished` (no events left),
  `breakpoint` (a time breakpoint was reached and deleted).
- A time breakpoint is reached when every event up to its time has been processed. If nothing happens at that
  exact time, `now` stays at the last event before it and `breakpoint.time` gives the breakpoint time.
- `state` of a process: `ready` (queued to run in the current or next cycle), `delayed` (waiting for a
  timeout), `waiting` (waiting for a signal event or suspended for good). `file` and `line` are `null` when
  the location is unknown. `file` is the name as given to `nvc -a`.
- `step -delta` (default) runs one simulation cycle, which is a delta cycle or the first cycle of a time
  step. `step -time` finishes the current time step including its delta cycles, or the whole next time step
  if the current one has finished.

Events are not pushed. A breakpoint is reported in the reply to the `run` or `step` that reached it, so the
GUI learns about it from the reply to its own request. Without `-json`, the same event is the result text
`breakpoint N at T fs`.

## How the GUI reads replies
- `OutputFilter` cuts every line that starts with `{"v":` out of the transcript text and reports it with the number of the
  prompt that precedes it. `NvcSession::send` returns the command number, so a reply belongs to the command with index
  prompt - 1. A command that fails has no reply; its handler is dropped when the shell is idle again.
- `gui/sim/parsers/json_replies.{hpp,cpp}` read the objects with QJsonDocument and return an error text for invalid
  JSON, a non-object, a missing or unknown `"v"`, or a missing member. `SimWindow` shows such a reply in the transcript.
- The shell only takes whole numbers for times (`breakpoint add 2.5 ns` is an error); the GUI converts decimals first.
- The command compatibility layer (`tcl/compat/lvdcompat.tcl`) passes `-json` through `run`; `step`, `breakpoint` and `processes` are native.

## Open items
- Native command list: see `docs/nvc-tcl-commands.md`.
- Runtime dump control (add and remove signals from the live stream while running) is not implemented.
- Source level breakpoints and `restart` need further kernel work.
