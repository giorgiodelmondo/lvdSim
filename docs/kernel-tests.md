# NVC kernel regression tests

The kernel in `kernel/nvc` (branch `lvdsim` of the NVC fork, see [kernel/PATCHES.md](../kernel/PATCHES.md)) has NVC's own test suite:
`test/regress` driven by `bin/run_regr` (1525 entries in `test/regress/testlist.txt` at commit `53d41eeae`, 1571 with the working tree change sets 04 to 39, 1575 with 40 and 41) and the check based
`bin/unit_test`. This page says how to run them on this machine and what the results are. `docs/verilog-coverage.md` covers the
Verilog side of the work.

## Running the suite (Windows 11, MSYS2 MINGW64)

All commands run in the MSYS2 MINGW64 environment, started from Git Bash or PowerShell like the kernel build:

    C:/msys64/usr/bin/env.exe MSYSTEM=MINGW64 CHERE_INVOKING=1 C:/msys64/usr/bin/bash.exe -lc "<commands>"

Do not call `make` from Git Bash directly: the compiler then fails with "Cannot create temporary file in C:\Windows".

1. Build the kernel as usual (`kernel/build/build-nvc-msys2.sh`). This gives `kernel/nvc/build/bin/nvc.exe`.
2. Build the test programs and plugins without running anything. From `kernel/nvc/build`:

       make -j16 check TESTS=

   This builds `bin/run_regr.exe`, `bin/unit_test.exe`, `bin/fstdump.exe` and the VHPI test plugin `lib/vhpi_test.so.exe`. Without this
   step the `vhpi*` tests fail (no plugin) and `run_regr` stops at the first `wave` test (no `fstdump`).

   **On the branch as it is, `test/run_regr.c` does not compile with MinGW** (`static declaration of 'strndup' follows non-static
   declaration`: the file defines its own `strndup` under `__MINGW32__` and MSYS2's `string.h` declares one). It is a harness-only
   problem; `nvc` itself builds. The kernel tree is not edited for this. Build `run_regr.o` from a copy of the file in which the local
   definition is renamed, then run `make` again (it keeps the object because it is newer than the source):

       sed 's/^static char \*strndup(/static char *lvd_unused_strndup(/' ../test/run_regr.c > /some/scratch/run_regr_mingw.c
       gcc -DHAVE_CONFIG_H -I. -I.. -I../thirdparty -I../src -I./src -Wall -mpopcnt -fms-extensions -flto=auto \
           -IC:/msys64/mingw64/include -D_FILE_OFFSET_BITS=64 -g -O2 -c -o test/run_regr.o /some/scratch/run_regr_mingw.c
       make -j16 check TESTS=

   (a compiler warning "defined but not used" for the renamed function is expected). The upstream-style fix is the `#ifndef
   HAVE_STRNDUP` guard around the function; it belongs on the fork branch and is the owner's decision.
3. Run the suite from `kernel/nvc/build` with the environment that `make check` sets:

       export PATH=/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH
       export BUILD_DIR=. NVC_LIBPATH=$PWD/lib EXEEXT=.exe
       bin/run_regr.exe -s > regr.log 2>&1

   Single tests or groups: `bin/run_regr.exe ivtest2 vtimescale1`, or `vlog`, `wave`, `vhpi`, `psl`, `cover`, `tcl` (group names select
   by flag). A name ending in a digit must match exactly, anything else is a substring match. Tests are run one after another
   (`run_regr` has no job option); the whole suite takes about 4 minutes.
4. Failures print as `name : failed` followed by the command line and the output. The exit status is 1 if any test failed. `-s` prints
   timing statistics at the end; on MinGW the numbers are wrong (a unit mix-up in the harness), ignore them. `stdenv2` fails if an earlier
   aborted run left `build/logs/stdenv2/tmp` behind ("remove tmp directory first"); move that directory away and rerun.

The shell `exit` fix on the branch (the shell waits for JIT workers, Windows hang) has no regress test because the hang needs a JIT compile
in flight at `exit`. Check it by hand with the packaged `vsim` in a clean environment (`env -i PATH=/usr/bin:/c/Windows/System32:/c/Windows`
plus `TEMP`): `vsim -c -do "run 2 us; quit" work.tb_mixed` on `examples/mixed_language` must exit, 20 times in a row.

Unit tests: `bin/unit_test.exe` builds, but on MinGW the `check` library has no fork support, so it prints
`WARNING: skipping unit tests due to CK_FORK=no` and runs nothing (exit status 0). The unit tests give no information on this machine;
they run on Linux (below).

## Linux (WSL Debian)

Packages (Debian, LLVM 19): `build-essential flex bison autoconf automake libtool pkg-config check llvm-dev libffi-dev
libzstd-dev zlib1g-dev libncurses-dev tcl-dev libreadline-dev`. `libreadline-dev` is required by `--enable-tcl`.

Do not build from `/mnt/d`. Clone the branch from the local kernel clone into the Linux file system and build out of tree:

    git clone -b lvdsim /mnt/d/D/lvdsim/kernel/nvc ~/build/nvc-src
    cd ~/build/nvc-src && ./autogen.sh
    mkdir ~/build/nvc-linux && cd ~/build/nvc-linux
    ~/build/nvc-src/configure --enable-tcl && make -j"$(nproc)"
    make -j"$(nproc)" check

## Results

### Change set 42 (2026-11-14, PR 10: `describe -location`)

New tests `descloc1` (tcl, VHDL), `descloc2` (shell, Verilog) and `descloc3` (shell, mixed); 1578 entries. Windows (`kernel/nvc/build`,
incremental `make`, 0 warnings): `run_regr` 1492 ok, 86 skipped (`disabled on windows`, `descloc2` and `descloc3` among them; both run by hand
in MSYS2 and pass), 0 failed. Linux (fresh clones of `0b2837794`, GCC 14, LLVM 19.1.7): 01 to 10 (`~/build/nvc-fork-linux-loc3`) 0 warnings,
`run_regr` 1578 ok, `-r` 1578 ok, unit tests 24 suites, 1182 checks, 0 failures; 03 + 07 + 10 (`loc3p`) 1544 ok, 1179 checks; 03 + 10 (`loc3v`)
1517 ok, `descloc2` and `descloc3` fail as expected (the Verilog `describe` is part of 07). lvdsim against `loc3` (WSL, `~/build/lvdsim-g14`, GUI
on): ctest 334 of 334, `test_view_declaration` runs the real nvc. Details: `kernel/upstream/VERIFY.md`, "2026-11-14, pull request 10".

### Change set 41 (2026-11-14, in PR 07: Ctrl-C / console break ends a shell run)

New tests `tclbreak1` (tcl, `!windows`: it sends SIGINT with `kill`) and `shellbreak1` (shell); 1575 entries. Windows (`kernel/nvc/build`,
incremental `make`): `run_regr` 1491 ok, 84 skipped (`disabled on windows`, the two new tests among them), 0 failed. Linux (fresh clone of
`0b2837794` + 01 to 09, `~/build/nvc-fork-linux-brk`, LLVM 19.1.7, GCC 14): 0 warnings (build and check build), `run_regr` 1575 ok, `-r` 1575
ok, unit tests 24 suites, 1182 checks, 0 failures. With the kernel before 41 (`~/build/nvc-fork-linux-hang40c`) `tclbreak1` ends with status
130 (killed by the SIGINT) and `shellbreak1` fails (no `interrupted` reply). The Windows console break (`CTRL_BREAK_EVENT`, process group, shared
or attached console) has no kernel regress test: run_regr cannot send it to one test alone. It is covered by the lvdsim test
`test_break_run` (real nvc, both console cases) and was checked on the real Windows desktop with `lvdsim-gui` (toolbar Break through UI
Automation: status bar "Interrupted at 8187585 ns"). Details: `kernel/upstream/VERIFY.md`, "2026-11-14, change set 41 (in 07)".

### Change set 40 (2026-11-14, in PR 07: time breakpoints set while stopped, `step` after `$stop` without the debug hook)

New tests `tcldbg4` (tcl) and `vstop3` (shell); 1573 entries. Windows (`kernel/nvc/build`, incremental `make`): `run_regr` 1491 ok, 82 skipped
(`disabled on windows`), 0 failed (`stdenv2` failed once on a `logs/stdenv2/tmp` left by an older run; ok after moving it away). Linux (fresh
clone of `0b2837794` + 01 to 09, `~/build/nvc-fork-linux-hang40c`): 1573 ok, `-r` 1573 ok, unit tests 24 suites, 1182 checks, 0 failures, 0
warnings. Details: `kernel/upstream/VERIFY.md`, "2026-11-14, change set 40 (in 07)".

### Working tree on `53d41eeae` with change sets 04 to 39 (2026-11-14, night, not committed)

Change sets 30 to 39 (`$stop`, `$isunknown`, time-zero events on driven nets, input ports kept apart from variables, task output arguments
after a wait, `describe` of Verilog signals, `$stop` at the shell prompt, Verilog timing checks, SDF back-annotation, `-g` of a physical subtype)
add the regress tests `vstop1`, `visunknown1`, `vzeroev1`, `vportdrv1`, `vtaskout1`, `vsetuphold1`, `genphys1` and the `shell` tests `vdescribe1`,
`vstop2`, `sdf1`, `vsdf1` (1571 entries), and the unit tests `test_values1`, `test_values2` (suite `sdf`). No node layout change: `kernel/nvc/build`
was rebuilt with `make` (no `make bootstrap`); libraries compiled with the change sets up to 29 stay valid.

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13) |
|---|---|---|
| build | `kernel/nvc/build-k9` (fresh `configure --enable-tcl --disable-silent-rules`), only the MinGW `%z` warnings of untouched lines; `kernel/nvc/build` rebuilt at the end | fresh clone of `0b2837794` + the thirty-nine patches, `~/build/nvc-fork-linux-s9`, 0 warnings; each new patch on exactly its prerequisites in `~/build/nvc-fork-linux-s9p30` ... `s9p39` (`s9p32b`, `s9p33b`, `s9p36b` after the corrections below), 0 warnings each |
| regress run and passed | 1490 | 1571 (single patch trees: 30, 31, 32b, 34, 39: 1514; 29+32+33: 1516; 35: 1523; 36b: 1530; 37: 1518; 38: 1524) |
| regress failed | 0 | 0 |
| regress skipped | 81, all `disabled on windows` (77 + `vdescribe1`, `vstop2`, `sdf1`, `vsdf1`; these four were run by hand in MSYS2 and pass) | 0 |
| `run_regr -r` | not run | 1571 ok |
| unit tests | not run (CK_FORK=no) | 24 suites, 1182 checks, 0 failures, 0 errors (38 alone: 1178; 35, 36b alone: 1179; others 1176) |

Corrections found by the single patch trees: `vzeroev1` first also checked an open `tri1` (needs 29), now it does not; `vportdrv1` needs the
time-zero events of 32, so 33 needs 29 and 32; and patch 12 needs 07 to compile (`wave_dumper_restart_shell()` frees `fst_data_t.last` of
07), so the tree of 36 includes 07 (`s9p36` failed to build, `s9p36b` passes). The full Linux tree was built with the first `vzeroev1`
(it passed there because 29 is in). Logs: `D:/tmp/lvdsim-s9/win-regr-4.log` (and `-1` to `-3` of the intermediate runs; the first had six
failures of a first version of change set 32, see `kernel/upstream/32-*/PR.md`), `D:/tmp/lvdsim-s9/linux_all*.log`, Linux
`~/build/nvc-fork-linux-s9*/*.log`.

lvdsim against this kernel: Windows `build/mingw-debug-k9` (GUI and tools on, `LVDSIM_NVC` at `kernel/nvc/build-k9`) 316 of 316 (one
intermediate run had `loadFst reads hierarchy, aliases and all value kinds` failing once under `-j 8`; it passed alone and in the full rerun);
Linux `~/build/lvdsim-s9` (GUI on, `LVDSIM_NVC` at `~/build/nvc-fork-linux-s9`, offscreen) 316 of 316. New Catch2 case "vsim SDF options become
nvc elaboration options".

Vendor libraries before (the kernel of the previous session, copied to `D:/tmp/lvdsim-s9/k-before`) and after (`build-k9`),
`D:/tmp/lvdsim-s9/scans.sh`: `scripts/libs/compile_libs.sh` with the tools of `mingw-debug-k8`, model scans with `nvc` directly, smoke tests:

| | before | after |
|---|---:|---:|
| `unisim_v` files compiling (of 428) | 428 | 428 |
| `unisim_v` models that elaborate with `glbl` and run 1 us | 318 | 318 |
| `220model.v` models that elaborate and run 1 us | 33 of 33 | 33 of 33 |
| `altera_mf.v` models that elaborate and run 1 us | 108 of 113 | 113 of 113 (`$stop`, `$isunknown`) |
| `twentynm_atoms.v`, `cyclone10gx_atoms.v` (models that run 1 us) | 19 of 66 each | 19 of 66 each |
| `altera_primitives.v`, `sgate.v` | 39 of 39, 11 of 19 | 39 of 39, 11 of 19 |
| `tennm_atoms.sv`, `altera_lnsim.sv` | fail | fail (unchanged) |
| library smoke tests: `examples/xpm_unisim` (two testbenches), `examples/altera_mf`, XPM block memory, unisim `BUFHCE`, Verilog `lpm_ram_dp` + `altsyncram` (`tb_alt_mem.v`) | 5 of 6 (`altsyncram` reads zeros: 8 errors) | 6 of 6 |

### Working tree on `53d41eeae` with change sets 04 to 29 (2026-11-14, not committed)

Change sets 23 to 29 (hierarchical task and function calls, rise/fall/turn-off delays on continuous assignments, `x`/`z` delays as zero,
unpacked array `localparam`, single-instance generate blocks, parameter dependent port ranges, `tri0`/`tri1` pulls) add the `verilog` tests
`vhiercall1`, `vassigndel1`, `vxdelay1`, `vlparray1`, `vgenitem1`, `vportrange1`, `vtripull1` (1560 entries) and the unit test `test_hiercall1`
(suite `vlog`). Change sets 23, 24 and 26 change the node layout: `make bootstrap` was run in `kernel/nvc/build-k8` and `kernel/nvc/build`, and
libraries compiled before must be compiled again.

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13) |
|---|---|---|
| build | `kernel/nvc/build-k8` (fresh `configure --enable-tcl --disable-silent-rules`); `kernel/nvc/build` rebuilt at the end (`make`, `make bootstrap`) | fresh clone of `0b2837794` + the twenty-nine patches, `~/build/nvc-fork-linux-s8b`, 0 warnings; each new patch alone (24 with 01) in `~/build/nvc-fork-linux-s8p23` ... `s8p29`, 0 warnings each |
| regress run and passed | 1483 | 1560 (each patch alone: 1514, 01 + 24: 1517) |
| regress failed | 0 | 0 |
| regress skipped | 77, all `disabled on windows` | 0 |
| `run_regr -r` | not run | 1560 ok |
| unit tests | not run (CK_FORK=no) | 24 suites, 1180 checks, 0 failures, 0 errors (23 alone: 1177; each other patch alone: 1176) |

Logs: `D:/tmp/lvdsim-s8/win-regr-2.log`, `D:/tmp/lvdsim-s8/linux_all*.log`, Linux `~/build/nvc-fork-linux-s8*/*.log`.

lvdsim against this kernel: Windows `build/mingw-debug-k8` (GUI and tools on, `LVDSIM_NVC` at `kernel/nvc/build-k8`) 315 of 315; Linux
`~/build/lvdsim-s8` (GUI on, `LVDSIM_NVC` at `~/build/nvc-fork-linux-s8b`, offscreen) 315 of 315.

Vendor libraries before (`build-k8` without the change, `D:/tmp/lvdsim-s8/libs-before`, `scan-before`, `alt-before`) and after (`libs-final`,
`scan-final`, `alt-final`), `scripts/libs/compile_libs.sh` with the tools of `mingw-debug-k8`, model scans with `nvc` directly
(`D:/tmp/lvdsim-s8/scan_unisim.sh`, `elab_unisim.sh`, `scan_altera.sh`, `elab_altera.sh`):

| | before | after |
|---|---:|---:|
| `unisim_v` files compiling (of 428) | 427 | 428 |
| `unisim_v` models that elaborate with `glbl` and run 1 us | 316 | 318 (`DCIRESET`, `DCM_SP`) |
| Altera `lpm_ver` (`220model.v`), `altera_mf_ver` (`altera_mf.v`) | fail | compile |
| `220model.v` models that elaborate and run 1 us | 0 | 33 of 33 |
| `altera_mf.v` models that elaborate and run 1 us | 0 | 108 of 113 |
| Verilog device atoms `twentynm_atoms.v`, `cyclone10gx_atoms.v` (analysis; models that run 1 us) | fail | analyse; 19 of 66 each |
| `tennm_atoms.sv`, `altera_lnsim.sv` | fail | fail (other constructs, `docs/libraries.md`) |
| library smoke tests: `examples/xpm_unisim` (two testbenches), `examples/altera_mf`, XPM block memory, unisim `BUFHCE`; new Verilog `lpm_ram_dp` | 5 of 5; new test not possible | 5 of 5; `lpm_ram_dp` passes, `altsyncram` (Verilog) does not (reads zeros) |

### Working tree on `53d41eeae` with change sets 04 to 22 (2026-11-14, not committed)

Change sets 15 to 22 (Verilog gaps of the vendor simulation libraries: timing check limits, empty arguments, gate arrays, hierarchical names through
generate blocks, `defparam`, specparams, `$realtobits`/`$bitstoreal`, `time` function types and `const`) add the `verilog` tests `vspecmtm1`,
`vemptyarg1`, `vgatearr1`, `vgenhref1`, `vdefparam1`, `vspecparam1`, `vrealbits1`, `vfunctime1` (1553 entries). Change set 17 changes the node
layout: `make bootstrap` was run in `kernel/nvc/build-k7` and `kernel/nvc/build`.

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13) |
|---|---|---|
| build | `kernel/nvc/build-k7` (fresh `configure --enable-tcl --disable-silent-rules`); `kernel/nvc/build` rebuilt at the end (`make`, `make bootstrap`) | fresh clone of `0b2837794` + the twenty-two patches, `~/build/nvc-fork-linux-s7`, 0 warnings; each new patch alone (19 with 18) in `~/build/nvc-fork-linux-s7p15` ... `s7p22`, 0 warnings each |
| regress run and passed | 1476 | 1553 (each patch alone: 1514, 18 + 19: 1515) |
| regress failed | 0 | 0 |
| regress skipped | 77, all `disabled on windows` | 0 |
| `run_regr -r` | not run | 1553 ok |
| unit tests | not run (CK_FORK=no) | 24 suites, 1179 checks, 0 failures, 0 errors (each patch alone: 1176) |

`vgenhref1` was changed after the first runs (it no longer uses hierarchical names as `assign`/`force` targets, which need change set 02); the new
version passes on Windows and in the Linux full tree in both modes. Logs: `D:/tmp/lvdsim-s7/win-regr-1.log`, Linux `~/build/nvc-fork-linux-s7*/*.log`,
`D:/tmp/lvdsim-s7/linux-alone*.log`.

lvdsim against this kernel: Windows `build/mingw-debug-k7` (GUI and tools on, `LVDSIM_NVC` at `kernel/nvc/build-k7`) 315 of 315, nothing skipped;
`scripts_libs_compile` passes. Linux `~/build/lvdsim-s7` (GUI on, `LVDSIM_NVC` at `~/build/nvc-fork-linux-s7`, offscreen): 315 of 315.

Vendor libraries before (`build-k6`) and after (`build-k7`), `scripts/libs/compile_libs.sh` with the tools of `mingw-debug-k7`:

| | before | after |
|---|---:|---:|
| `unisim_v` files compiling (of 428) | 418 | 427 (only `DCIRESET` fails) |
| `unisim_v` models that analyse, elaborate with `glbl` and run 1 us (`nvc` directly, `D:/tmp/lvdsim-s7/scan-*`) | 303 | 316 |
| `xpm`, `altera_mf`, `lpm`, `sgate`, `altera_primitives`, `altera_ver`, `sgate_ver` | all compile | all compile |
| `altera_lnsim.sv`, `220model.v`, `altera_mf.v` | fail | fail (other constructs, `docs/libraries.md`) |
| library smoke tests: `examples/xpm_unisim` (two testbenches), `examples/altera_mf`, XPM `xpm_memory_sdpram` and `xpm_fifo_sync` with block memory, unisim `BUFHCE` | 3 of 6 | 6 of 6 |

### Working tree on `53d41eeae` with change sets 04 to 14 (2026-11-14, not committed)

Change sets 13 (the Tcl shell writes coverage, `coverage save`) and 14 (`--cover-export --format=json`) add the `shell` tests `covshell1` and
`covjson1` (gold file `gold/covjson1.json`), 1545 entries.

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13) |
|---|---|---|
| build | `kernel/nvc/build-k6` | fresh clone of `0b2837794` + the fourteen patches, `~/build/nvc-fork-linux-s6c`, 0 warnings; patch 14 alone `~/build/nvc-fork-linux-s6p14`, 0 warnings |
| regress run and passed | 1468 | 1545 (14 alone: 1514) |
| regress failed | 0 (first run: `stdenv2`, the leftover `logs/stdenv2/tmp` of the previous run, moved to `D:/tmp/lvdsim-s6/stdenv2-tmp-*`; passes) | 0 |
| regress skipped | 77, all `disabled on windows` (75 + `covshell1`, `covjson1`) | 0 |
| `run_regr -r` | not run | 1545 ok (14 alone: 1514) |
| unit tests | not run (CK_FORK=no) | 24 suites, 1179 checks (14 alone: 1176), 0 failures, 0 errors |

`covshell1.sh` and `covjson1.sh` were run by hand on Windows (the JSON differs from the gold file only by the CR of the line end). Logs:
`D:/tmp/lvdsim-s6/win-regr-2.log`, Linux `~/build/nvc-fork-linux-s6c/*.log`, `~/build/nvc-fork-linux-s6p14/*.log`.

lvdsim against this kernel: Windows `build/mingw-debug-k6` 291 of 291, nothing skipped (`coverage with nvc: the interactive shell writes run-time
counts` now runs and passes); new Catch2 cases in `tools_tests` (`coverage export: the JSON export gives the database of the XML export` and four
more). Linux `~/build/lvdsim-s6` (GUI on, `LVDSIM_NVC` at `~/build/nvc-fork-linux-s6c`, offscreen): 291 of 291, the coverage shell case included.

### Working tree on `53d41eeae` with change sets 04 to 12 (2026-11-14, not committed)

Change set 12 (`restart` in the shell, `kernel/upstream/12-shell-restart`) adds the regress tests `restart1` (`tcl`) and `restart2` (`shell`,
skipped on Windows; it also compares an FST with the gold file `gold/restart2.dump`) and enables the restart lines of upstream's `tcl2.tcl`
again (1543 entries).

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13) |
|---|---|---|
| build | separate directory `kernel/nvc/build-k6` (fresh `configure --enable-tcl --disable-silent-rules`), the GUI work on `kernel/nvc/build` not disturbed; warnings only in untouched files (`jit-code.c` unused function, `lz4.c` visibility) | fresh clone of `0b2837794` + the twelve patches of `kernel/upstream/`, `~/build/nvc-fork-linux-s6`, 0 compiler warnings |
| regress run and passed | 1468 | 1543 |
| regress failed | 0 | 0 |
| regress skipped | 75, all `disabled on windows` (74 + `restart2`) | 0 |
| `run_regr -r` (reheat mode) | not run | 1543 ok, 0 failed |
| unit tests | not run (CK_FORK=no) | 24 suites, 1179 checks, 0 failures, 0 errors |

`restart2.sh` was run by hand on Windows in MSYS2 (passes; the FST after `restart` is identical to the one of a fresh run) and a VCD restart
was checked by hand. The first full Windows run passed in one go. Logs: `D:/tmp/lvdsim-s6/win-regr-1.log`, Linux
`~/build/nvc-fork-linux-s6/{regr,reheat,unit}.log`. During development one crash was found and fixed before these runs: the debug hook of change
set 06 stayed installed after a run, so the initialisation of the new model called it while the shell rebuilt its tables (the hook is now
removed during a restart).

Cost of a restart (`D:/tmp/lvdsim-s6/bench`, Windows, build-k6, a VHDL top with 2000 VHDL LFSR cells and 400 Verilog counters, 10 003 signals;
`restart` timed with `clock microseconds` after `run 1 us`, five times): 20.6 to 22.8 ms without waveform, 22.6 to 30.3 ms with the live stream.
A new process `echo exit | nvc -i bench` (load the elaborated design, start the shell, exit): 160 to 188 ms, with the live stream 171 to 189 ms.
The GUI's relaunch adds the start of the session and re-adding the wave rows on top of that.

lvdsim against this kernel: Windows `build/mingw-debug-k6` 283 of 283 (`coverage with nvc: the interactive shell writes run-time counts` skipped:
it waits for the kernel to write coverage from the shell); new Catch2 cases in `libwave_tests` (restart record, a frozen stream of the kernel,
bad restart records, incremental readers after a clear) and `TestJsonReplies::restartReplies`.

### Working tree on `53d41eeae` with change sets 04 to 11 (2026-11-13, not committed)

Change sets 07 (arrays: only changed elements in the dump), 08 (Verilog memories word by word), 09 (shell: array elements, `deposit`,
`readmem`, three model fixes), 10 (generic override paths, `--generic-default`) and 11 (debugger follow-up: stream flushed at a stop,
`breakpoint add` checks lines) add the regress tests `wavemem1`, `livemem1`, `vwavemem1`, `vwavemem2`, `tclmem1`, `tclmem2`, `genpath1`,
`genpath2`, `tcldbg3`, `livedbg1` (1541 entries; `livemem1`, `tclmem2`, `genpath2`, `livedbg1` are `shell` tests, skipped on Windows), the unit test
`test_force2` (suite `shell`), and change the gold files of `issue1362` and `issue1406` (redundant rewrites of enum array elements removed).

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13, 48 cores) |
|---|---|---|
| build | separate directory `kernel/nvc/build-k5` (configured like `kernel/build/build-nvc-msys2.sh`), so that the GUI work on `kernel/nvc/build` was not disturbed; `kernel/nvc/build` rebuilt at the end; only the MinGW `%z` and `lz4` warnings of untouched code | fresh clone of `0b2837794` + the eleven patches of `kernel/upstream/`, `~/build/nvc-fork-linux-s5b`, 0 compiler warnings (files identical to the working tree except `nvc.1`, a man page hunk added to patch 10 after the build) |
| regress run and passed | 1467 | 1541 |
| regress failed | 0 | 0 |
| regress skipped | 74, all `disabled on windows` (70 + `livemem1`, `tclmem2`, `genpath2`, `livedbg1`) | 0 |
| `run_regr -r` (reheat mode, whole suite) | not run | 1541 ok, 0 failed |
| unit tests | not run (CK_FORK=no) | 24 suites, 1179 checks (1178 + `test_force2`), 0 failures, 0 errors |

Windows: the first full run had `issue1362`, `issue1406` (gold files not yet updated) and `tclmem1` (VHDL-93 `std_logic_vector` in `parse_value()`)
failing; both were fixed. The second full run failed only `stdenv2` because of the `logs/stdenv2/tmp` left by the first run (step 4 above; moved to
`D:/tmp/lvdsim-s5/stdenv2-tmp-*`), which then passed. Logs: `D:/tmp/lvdsim-s5/win-regr-1.log`, `win-regr-2.log`; Linux `D:/tmp/lvdsim-s5/logs`.
Pull request trees with only their prerequisites: `kernel/upstream/VERIFY.md`, last section.

Measurement of change set 07 (`D:/tmp/lvdsim-s5/bench`, Windows, a 64K word RAM of 32 bit words written every 10 ns, `--stop-time=20us
--dump-arrays`, best of three): without waveform 71 / 67 ms; FST 1589 ms / 386 ms, 18326 / 18325 bytes (`fstdump` identical); live stream 21493 ms
/ 348 ms, 5354823364 / 5838090 bytes (before / after). The two live streams carry the same 75538 effective value changes.

lvdsim against this kernel: Windows `build/mingw-debug-k5` 266 of 266 (`tools_integration_tests` 11 test cases, 106 assertions, not skipped;
`test_memory_window` passes; an intermediate run had `test_console_session` failing while the GUI work rewrote that test, it passes in the final
run); Linux `~/build/lvdsim-s5` (GUI on, `LVDSIM_NVC` at `~/build/nvc-fork-linux-s5b`) 266 of 266, `tools_integration_tests` 11 test cases, 106
assertions.

### Working tree on `53d41eeae` with change sets 04, 05 and 06 (2026-11-13, not committed)

Change set 06 (source line breakpoints, statement stepping, `stack`, `locals`; `kernel/upstream/06-shell-source-debugger`) adds the regress
tests `tcldbg1` (`tcl`) and `tcldbg2` (`shell`, skipped on Windows), 1531 entries, and the JIT unit tests `test_debug1`, `test_debug2`.

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13, 48 cores) |
|---|---|---|
| build | `kernel/build/build-nvc-msys2.sh` (rebuilt in place); only the MinGW `%z` format warnings of untouched upstream lines (`jit-core.c` 102, 105 and the other files listed above) | fresh clone of `nickg/nvc` at `0b2837794` + the six patches of `kernel/upstream/` (all 3484 files byte-identical to the working tree), `~/build/nvc-fork-linux-s4f`, 0 compiler warnings |
| `nvc --version` | `nvc 1.24-devel (53d41eeae) (Using LLVM 23.1.5)` | `nvc 1.24-devel (1.23.0.r65.g0b2837794) (Using LLVM 19.1.7)` (built from upstream, so the version names the upstream commit) |
| regress run and passed | 1461 (1460 + `tcldbg1`) | 1531 |
| regress failed | 0 | 0 |
| regress skipped | 70, all `disabled on windows` (69 + `tcldbg2`) | 0 |
| `run_regr -r` (reheat mode, whole suite) | not run | 1531 ok, 0 failed |
| unit tests | not run (CK_FORK=no) | 24 suites, 1178 checks (1176 + `test_debug1`, `test_debug2`), 0 failures, 0 errors |

Windows: `stdenv2` fails when an earlier run left `build/logs/stdenv2/tmp` (step 4 above); the directory was moved to `D:/tmp/stdenv2-tmp-<time>`
before the final run, which then passed in one go. Logs: `D:/tmp/lvdsim-s4/win-regr-final4.log`, Linux `D:/tmp/lvdsim-s4/linux-f/`. The fork branch on
GitHub is still at `754af76cc` (the merge `53d41eeae` was not pushed), so the Linux tree was made from upstream plus the patches; an earlier run on a clone of
the fork branch (`754af76cc` + patches 04, 05, 06 of an intermediate version, `~/build/nvc-fork-linux-s4`) also passed (1531, reheat 1531, 1177 checks).

Cost of change set 06 (`kernel/upstream/VERIFY.md`, last section; Linux, a loop-heavy design, 200 us, best of 7): compiled run 99 ms with and without
the change; interpreter only (`NVC_JIT_THRESHOLD=1000000000`) 560 ms without, 571 ms with (repeated baseline runs gave 556 to 561 ms); with a line
breakpoint that is never hit 808 ms, because the whole design then runs in the interpreter.

lvdsim against this kernel: Windows `build/mingw-debug-k4` 261 of 261 (`tools_integration_tests` 11 test cases, 95 assertions, not skipped;
`test_json_replies` 28 cases with the new source debugger readers); Linux `~/build/lvdsim-s4` (GUI on, `LVDSIM_NVC` at `~/build/nvc-fork-linux-s4f`) 261 of 261, `tools_integration_tests` 11 test cases, 95 assertions.
An intermediate run on Windows had one failure, `test_memory_window` (Not Run: a test file being written by the GUI work in parallel did not compile
yet); it passes in the final run.

### Working tree on `53d41eeae` with change sets 04 and 05 (2026-11-13, not committed)

Change sets 04 (port modes in `describe -json` and in the live stream) and 05 (the live stream records the time a run reached, `run` counts
from it) are in the working tree of `kernel/nvc`, not committed (`kernel/PATCHES.md`). They add the regress tests `json2`, `live1`, `tcl7`,
`live2` (1529 entries) and change the expected reply of `json1`. `live1` and `live2` are `shell` tests (a `.sh` script that runs `nvc --do` with
`--format=live`), which `run_regr` skips on Windows.

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13, 48 cores) |
|---|---|---|
| build | `kernel/build/build-nvc-msys2.sh` (rebuilt in place); the only warnings are the MinGW `%z` format warnings of untouched upstream code (`model.c`, `vhpi-model.c`, `jit-core.c`), printed because `model.c` was recompiled | fresh clone `~/build/nvc-fork-src-s3` plus `git diff` of the working tree, `~/build/nvc-fork-linux-s3`, 0 compiler warnings |
| `nvc --version` | `nvc 1.24-devel (53d41eeae) (Using LLVM 23.1.5)` | `nvc 1.24-devel (53d41eeae) (Using LLVM 19.1.7)` |
| regress run and passed | 1460 (1458 + `json2`, `tcl7`) | 1529 |
| regress failed | 0 | 0 |
| regress skipped | 69, all `disabled on windows` (67 + `live1`, `live2`) | 0 |
| `run_regr -r` (reheat mode, whole suite) | not run | 1529 ok, 0 failed |
| unit tests | not run (CK_FORK=no) | 24 suites, 1176 checks, 0 failures, 0 errors |

Logs: `D:/tmp/lvdsim-s3/win-regr.log`, `D:/tmp/lvdsim-s3/win-build.log`, WSL `~/build/nvc-fork-s3-check.log`, `~/build/nvc-fork-s3-reheat.log`. The
`stdenv2` leftover directory of the previous run was moved to `D:/tmp/lvdsim-s3/stdenv2-leftover-logs` before the run. The pull request trees
(upstream plus the patches of `kernel/upstream/`) are in `kernel/upstream/VERIFY.md`.

lvdsim against this kernel: Windows `build/mingw-debug-k3` and Linux `~/build/lvdsim-s3` (GUI on, `LVDSIM_NVC` at `~/build/nvc-fork-linux-s3`):
217 of 218 each. The one failure is `test_sim_window`, in the dock layout cases `dockTabBarsAreInsideTheirAreasAndNotDuplicated` (both) and
`defaultLayoutOnFirstStart(small)` (Windows), which count dock tab bars and widths: the List window being added to `sim_window` in parallel
adds a tab group; they do not involve the kernel. The new cases `objectsShowThePortModesTheKernelStreams` and
`theDataEndsWhereTheRunEndedNotAtTheLastEvent` pass on both and fail against the kernel before the change (`D:/tmp/nvc-build-pre-merge`).

### Commit `53d41eeae` (2026-11-12)

The branch after the merge of upstream `0b2837794` (nine upstream commits: Cobertura and coverage hierarchy changes, "Ensure locally static
constant values are folded", the unit test `issue1722` in `test/lower`) into `754af76cc`. The merge was clean. The regress list still has 1525
entries (the new upstream test is a unit test).

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13, 48 cores) |
|---|---|---|
| build | `kernel/build/build-nvc-msys2.sh` (rebuilt in place), 0 warnings | `~/build/nvc-fork-src-s2` (fresh clone), `~/build/nvc-fork-linux-s2`, 0 compiler warnings |
| `nvc --version` | `nvc 1.24-devel (53d41eeae) (Using LLVM 23.1.5)` | `nvc 1.24-devel (53d41eeae) (Using LLVM 19.1.7)` |
| regress run and passed | 1458 | 1525 |
| regress failed | 0 | 0 |
| regress skipped | 67, all `disabled on windows` | 0 |
| `run_regr -r` (reheat mode, whole suite) | not run | 1525 ok, 0 failed |
| unit tests | not run (CK_FORK=no) | 24 suites, 1176 checks (1175 before: `issue1722`), 0 failures, 0 errors |

Windows: the first full run had two failures that were not caused by the merge: `stdenv2` (the leftover `build/logs/stdenv2/tmp` of the copied
build, see step 4 above) and `bounds6` (a character `'\377'` printed through a pipe in a run started from a background subshell: "no match line 1";
it passes on every rerun). After moving the `tmp` directory away the full run gave 1458 ok, 67 skipped, 0 failed. Logs:
`D:/tmp/lvdsim-s2/win-regr.log`, `D:/tmp/lvdsim-s2/win-build.log`, WSL `~/build/nvc-fork-s2-check.log`, `~/build/nvc-fork-s2-reheat.log`. The build of
the previous commit was kept as `D:/tmp/nvc-build-pre-merge`.

lvdsim against this kernel: `ctest` on the existing `build/mingw-debug` (binaries of 2026-11-07, not rebuilt because the GUI sources were being edited):
193 of 193, `tools_integration_tests` 10 test cases and 76 assertions (not skipped). `examples/mixed_language/run.sh --batch` prints
`events {"00000010"} avg {"00101110"}`; `examples/reports/run.sh --batch` ends with the expected `Failure` at 2 us (exit status 1) and its output is identical to the
one of the pre-merge build.

### Commit `754af76cc` (2026-11-08)

Both builds are of the branch `lvdsim` at `754af76cc` (the three change sets of `kernel/PATCHES.md` on upstream `4e41ba459`); nothing
else is applied.

| | Windows (MSYS2 MINGW64) | Linux (WSL Debian 13, 48 cores) |
|---|---|---|
| build | `kernel/build/build-nvc-msys2.sh`, `kernel/nvc/build` | `~/build/nvc-fork-src-s1` (fresh clone), `~/build/nvc-fork-linux-s1`, 0 compiler warnings |
| entries in `testlist.txt` | 1525 | 1525 |
| regress run and passed | 1458 | 1525 |
| regress failed | 0 | 0 |
| regress skipped | 67, all `disabled on windows` | 0 |
| unit tests | not run (CK_FORK=no, skipped by the harness) | 24 suites, 1175 checks, 0 failures, 0 errors |

On Windows the harness was built with the `strndup` workaround of step 2 above (renamed copy outside the tree). Logs: `D:/tmp/lvdsim-s1/win-regr.log`,
`~/build/nvc-fork-s1-check.log`.

### lvdsim against this kernel (2026-11-08)

Windows, `ctest --preset mingw-debug`: 193 of 193 (and `mingw-release`: 193 of 193). `tools_integration_tests` really runs (10 test cases, 76
assertions) with `kernel/nvc/build` as the kernel; no Qt Test binary (18 of them, 373 checks) reported a skip. Linux, fresh build directory
`~/build/lvdsim-s1` (Debug, GUI on), `LVDSIM_NVC` and `LVDSIM_NVC_LIBDIR` at `~/build/nvc-fork-linux-s1`: 193 of 193, `tools_integration_tests` 10 test
cases and 76 assertions, no skip.

### History

Until 2026-11-07 the kernel was upstream `386a23083` plus sixteen patches applied by a script (`kernel/patches`, retired 2026-11-08); the
numbers of that period (1454 Windows, 1521 Linux, unit tests 1175 checks) are in the old copy of this page that was moved to `D:/tmp/kernel-patches-old-<timestamp>/kernel-tests.md.old`. Differences between that tree
and the branch, found when the strategy changed: the branch has no MinGW `HAVE_STRNDUP` guard in `test/run_regr.c`, has the extra test
`mixed_open1`, a stronger `nba_deassign1` test (it records the time `q` rises), no `NEWS.md` entries, lines wrapped to 80 columns, `#ifdef
ENABLE_TCL` around the shell wave helpers in `src/nvc.c`, the NVC copyright line in `src/rt/wave-live.[ch]`, and no IEEE section comment in
`format_time()`.

## Known concerns

- Failures show up only when the full suite runs; run it after changing any file under `kernel/nvc/src`.
- A change of the object format (new fields in the Verilog nodes, as in change set 1) needs `make bootstrap`; libraries analysed before must be
  analysed again ("serialised format digest" error). An older library keeps the old (truncating) `$time` division until it is reanalysed.
- Tests that need several libraries (library lookup of Verilog modules) have no regress test (`run_regr` has one work library); they run in
  `tools_integration_tests`.
