# NVC interactive shell: command inventory

Captured 2026-11-06 from `kernel/nvc` at upstream commit 386a23083, the base before the fork branch `lvdsim` existed (nvc 1.24-devel, LLVM 23.1.5),
built with `--enable-tcl`. Start with `nvc [-L libdir] -i [unit]`. The shell is standard Tcl 8.6 plus the
commands below. Names are relative to the elaborated top unit, so the root is `/` (for example `/cnt`).

## Native commands

| Command | Notes |
|---------|-------|
| `run [time]` | Start or resume. `run 100 ns` works. |
| `restart` | Listed in help, but replies "restart command is not currently supported". |
| `force [sig value]` / `noforce sig` | Enum literal, integer or bit-string value. No args lists forces. |
| `examine` / `exa` `[-radix type \| -hex ...] name...` | Prints current value. `-hex` gives `0xa`. |
| `find signals\|regions [-r] pattern` | Recursive wildcard search. `find signals -r /*` lists all. |
| `describe name...` | One line: name, path, kind, type. |
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

Everything marked none above is added by patches 0003 to 0005 except source level breakpoints.

## Command mapping status

| Command | NVC today | Status |
|--------|-----------|--------|
| run | `run` | native |
| force / noforce | same | native |
| examine | `examine` | native |
| find | `find signals` (different syntax) | wrapper |
| describe | `describe` | native |
| restart | present but unsupported | fork-needed |
| quit | none | wrapper alias |
| add wave / log | none, no dumper in shell | fork-needed (first patch) |
| bp (time only) | `breakpoint add <t> <unit>` | fork (0005), name differs, no source breakpoints |
| step | `step [-delta\|-time]` | fork (0005), simulation cycle, not source line |
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

Run with `build/mingw-release/gui/bench_paint.exe [signals] [changes] [width] [height]`.
Headless screenshots: `lvdsim-wave-demo.exe trace.fst --screenshot out.png [--cursor T] [--cursor2 T]
[--view START END] [--analog NAME] [--radix NAME RADIX]`; set `QT_QPA_PLATFORM=offscreen` and
`QT_QPA_FONTDIR=C:\Windows\Fonts` (without fonts the offscreen platform draws text as boxes).
