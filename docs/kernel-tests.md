# NVC kernel regression tests

The kernel in `kernel/nvc` (branch `lvdsim` of the NVC fork, see [kernel/PATCHES.md](../kernel/PATCHES.md)) has NVC's own test suite:
`test/regress` driven by `bin/run_regr` (1525 entries in `test/regress/testlist.txt` at commit `754af76cc`) and the check based
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
