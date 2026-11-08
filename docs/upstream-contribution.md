# Sending the lvdsim kernel patches to mainline NVC

This is a step by step guide for the person who will submit the patches to https://github.com/nickg/nvc. It assumes you
know a little git and nothing about how NVC takes contributions. Nothing has been pushed, no pull request has been opened
and nobody has been contacted: every step below is yours to do.

The ready made material is in `kernel/upstream/`: three folders, one per pull request, each with a plain `.patch` file and a `PR.md`
with the title and the text to paste, plus `README.md`, `series-order.txt` and `VERIFY.md` (the commands and results of the builds and tests).
There is no scratch repository with branches any more: you apply a patch to a fresh clone of NVC yourself (section 7).

## 1. State of upstream when this was prepared

The patches are made against upstream `master` `4e41ba45965e2bc203835ed6c7cc63575c9cac3d` ("Fix Verilog XOR/XNOR reduction for arguments > 64 bit",
2026-11-08). The base was not fetched from GitHub when the patches were rebuilt (the Windows git of this machine has no network); it is the
commit present in the local clone of the fork, which holds upstream master as of that day. An earlier read only check with
`git ls-remote https://github.com/nickg/nvc.git` (run in WSL, 2026-11-07) showed master moving from `386a23083` (the base of the lvdsim fork) through
`ad4242988` (VHPI), `3133c7ec4` (Verilog UDP `b` symbol) and `851dfd445` (user function call crash) to the current base; the latest release tag at that time was
`r1.23.1` (`fab54a9e96964b431050d837e163c22282a1df6c`). The four commits since `386a23083` touch code unrelated to the patches, and none of the changes
was fixed upstream in the meantime (each reproducer in the `PR.md` files was run on `4e41ba459` and fails there).

Since 2026-11-08 the lvdsim kernel is the branch `lvdsim` of the fork `giorgiodelmondo/nvc`, which is on this base and contains the three change sets as commits
(`kernel/PATCHES.md`); the single patch series `kernel/patches` is retired. Before you push, check again (section 10); if master has moved, rebase first.

## 2. What is in the folder

| Folder | What | retired lvdsim patches (mapped in `kernel/PATCHES.md`) | Tests |
|--------|------|------------------------------------|-------|
| `01-verilog-time-delay-timeformat` | `timescale` aware delays and `$time` (rounded to the nearest unit, ties up), new `$realtime`, `$timeformat` and `%t`, fix of the unit `s`; node layout change | 0007, 0015, 0016 | `vtimescale1`, `vtimescale2`, `vtimeformat1`; gold file `ivtest2` changes |
| `02-bug-fixes` | reheat of all top levels (`-e top glbl` then `-r`), `always @(*)` with procedural `assign` or real constant, hierarchical reference as `assign`/`force`/`release` target, non-blocking assignment after `deassign`, VHDL `string`/`open` generics for Verilog parameters, Verilog module lookup across libraries, shell `exit` waits for asynchronous work | 0006, 0008, 0009, 0010, 0011, 0012, 0013, 0014 | `vtops1`, `mixed_str1`, `mixed_open1`, `sens_assign1`, `href_lval1`, `nba_deassign1` |
| `03-interactive-mode` | shell waveform options, live waveform stream (two new source files), `-json` replies, `processes`, `step`, `breakpoint` | 0001 to 0005 | `json1`, `procs1`, `step1` |

Patch 0017 of the rebased series (a MinGW only guard for `strndup` in `test/run_regr.c`) is a harness fix for the lvdsim Windows build and is not part of any pull request.

The three patches are independent: each applies to the base alone, and all three apply together in any of the six orders to the same tree (checked with
`git apply --check` and `git apply` one after the other; `series-order.txt`, `VERIFY.md`). To make that true:

* the test entries are inserted at three different places of `test/regress/testlist.txt` (after `timescale1`, after `deassign1`, after `cmdline23`, at least six
  unchanged lines apart), so the patches contain the `testlist.txt` change and no separate list is needed;
* `NEWS.md` is not touched by any patch (it is the file that conflicts most). If the maintainer asks for a NEWS line, add it yourself on top of "Unreleased changes";
* `src/tcl/tcl-shell.c` and `src/rt/model.c` are edited by both PR 2 and PR 3, in different functions (include list and `shell_cmd_exit()` against the new commands;
  `model_cycle()` against the new `model_step_cycle()`).

Moved between the pull requests compared with the task as first described: nothing needed to move (patch 0014, the shell `exit` fix, is in PR 2 because its hunks
do not overlap with PR 3). Dropped as already fixed upstream: nothing.

### Differences between the pull request patches and the retired `kernel/patches` series

The patches here were not byte for byte the retired lvdsim patches (the fork branch now contains the pull request versions, apart from the MinGW test harness note in `kernel/PATCHES.md`). The audit changed:

| Item | lvdsim patches | pull request patch | Why |
|------|----------------|--------------------|-----|
| NEWS.md | one entry per patch | none | see above |
| `testlist.txt` | entries appended at the end by every patch | entries at three places | no conflicts between the pull requests |
| `nba_deassign1` test (0012) | passed on the unfixed base (the check at 36 was itself the scheduled event that flushed the update) | records the time `q` rises with `always @(q)`, must be 35 | a regression test must fail before the fix; it does (`VERIFY.md`) |
| tests of 0009 | `mixed_str1` only | plus `mixed_open1` (an `open` generic, which the `mixed_str1` test does not cover) | every changed behaviour has a test that fails on the base |
| C comment in `format_time()` (0015) | cites section numbers of the IEEE 1800-2017 standard | no citation | the wording of the standard was not checked, so no section is cited |
| `src/rt/wave-live.[ch]` (0002) | `Copyright (C) 2026  lvdsim contributors` | `Copyright (C) 2026  Nick Gasson` and the same notice as the neighbouring files | see section 4: a decision for you |
| Long lines (0001, 0002) | five lines over 80 columns (`opt_set_int(OPT_DUMP_ARRAYS, ...)`, the `WV` macro comment, two `EmitVariableLengthValueChange` calls, one comment in `wave-live.h`) | wrapped | `contrib/STYLE.md` |
| Commit message | lvdsim log style, `Co-Authored-By` line | one suggested message per pull request in upstream style (wrapped at 72 columns), no `Co-Authored-By`/`Signed-off-by` | your decision on attribution (section 6) |

The fork branch and the pull request patches are the same changes (section 10 says how the branch follows upstream).
The fork and the pull request patches behave the same for `$time` (both round to the nearest unit of the module, ties up) and for `%t`.

### Style audit (contrib/STYLE.md)

The added lines of all C and header files of the three patches were checked with scripts (80 column limit, no tabs, no trailing whitespace, no `/* */` comments;
`git diff --check` is clean) and by reading the code (three space indent, braces, `snake_case`, helper macros such as `LOCAL` and `xmalloc`). The only line with
`/*` is a Tcl help string (`/uut/*`). No "lvdsim" wording and no Windows only assumption is in any patch; the word "Windows" appears only in the comment and the text
about the shell `exit` fix, where the bug is Windows specific. No change under `src/` adds a copyright line except the two new files of PR 3 (section 4).
The usage text of `nvc --help` and the manual were not updated for the shell options of PR 3 (`--format=live` and the use of `--wave`, `--gtkw`, ... with `-i`
and `--do`); do that if the maintainer wants the options documented.

## 3. Verification

`kernel/upstream/VERIFY.md` has the exact commands and the real results (WSL Debian, GCC, LLVM 19.1.7, out of tree build in a new directory per tree). In short:
each patch alone on the base, then the three together, with a full build, the unit tests and the whole regression suite (`make check`), the new tests by hand in
normal and reheat mode, the full `run_regr -r` for PR 2 and the combination, and each new test on the unpatched base with only the test files added.
Not run: the Windows (MSYS2) build of the pull request trees, the MSVC build, builds without `--enable-tcl`, `make check` with sanitizers, or any other LLVM version.

## 4. Copyright and licence

* NVC is GPL-3.0-or-later; everything here is yours to contribute under that licence. The lvdsim patches are GPL-3.0-or-later as well, so there is no licence conflict.
  Confirm that you are the author or have the right to submit each change.
* The maintainer writes in README.md ("Contributing"): "I will not accept patches that add new copyright owners under `src/`". Significant contributors are listed in
  `THANKS.md` instead. PRs 1 and 2 only edit existing files that already carry `Copyright (C) ... Nick Gasson`, so no header is touched or added. Do not add your name
  to a header in `src/`.
* If you want to be credited, add an entry to `THANKS.md` (an optional extra commit; ask first or let the maintainer decide).
* The commit author is the credit in git history; set your identity in the repository you commit from (section 7).
* New files: PR 3 adds `src/rt/wave-live.c` and `src/rt/wave-live.h`. In the lvdsim fork they start with `Copyright (C) 2026  lvdsim contributors`, which is
  exactly what the maintainer says he will not accept. In the PR 3 patch they carry the header of his neighbouring files (`//  Copyright (C) 2026  Nick Gasson`
  followed by the GPL-3-or-later notice) so that the patch does not name a new owner. **That is a decision between you, as the person who holds the rights to those
  files, and him**; it is not something this repository can decide or assume, and no assignment or licence text has been written. Ask in the issue (section 12) before
  sending PR 3, and change or drop the two files if he answers differently. The tests under `test/regress/` carry no header (existing Verilog tests have none).

## 5. Prerequisites

1. A GitHub account.
2. Fork `https://github.com/nickg/nvc` to your account (web: "Fork" button; CLI: `gh repo fork nickg/nvc --clone=false`).
3. Git installed, and your own identity configured **inside the repository you submit from**:
   `git config user.name "Your Real Name"` and `git config user.email you@your-real-address` (use the address tied to your GitHub
   account; GitHub can also give a noreply address).
4. Read, once: README.md section "Contributing", `contrib/STYLE.md`, `AGENTS.md` (also describes how the maintainer builds and tests).
5. A Linux or WSL build of NVC to rerun the tests (section 6). On Windows use MSYS2 as the lvdsim kernel build does.

## 6. What you must do yourself before submitting

1. **Read every diff.** You are responsible for what you send; the maintainer reviews it as yours.
2. **Run the tests yourself** on the final tree. In a Linux or WSL shell, in a clone of your fork with one patch applied (section 7):

   ```
   ./autogen.sh && mkdir build && cd build
   ../configure --enable-tcl && make -j"$(nproc)"
   make check QUICK=1                  # unit tests only
   bin/run_regr vtimescale1            # a single regression test
   bin/run_regr -r vtops1              # reheat mode (the vtops1 test of PR 2 is only exercised by -r)
   make check                          # everything, a few minutes
   ```

   `QUICK=1` makes `run_regr` return at once, so `make check QUICK=1` runs only the unit tests. After applying PR 1 a `make bootstrap` is needed (node layout change);
   when you switch between trees that differ in the node layout run `make clean-libs` first, otherwise tests fail with "serialised format digest".
   Note that `autogen.sh` uses `git rev-parse` and the build uses `.git/logs/HEAD`, so build inside a git clone, not in an unpacked archive.
3. **Decide about disclosure of AI assisted development.** State this plainly: these patches were developed with AI assistance (an AI coding assistant wrote and revised the code,
   tests and texts, directed by the lvdsim project; the code was built and tested on Linux and Windows). What was found about upstream policy: the words AI, LLM, Copilot,
   ChatGPT, Claude, "AI-assisted" and "machine generated" were searched for in `README.md`, `THANKS.md`, `AGENTS.md`, `contrib/`, `www/`, `.github/` and the other
   documents of the upstream tree at an earlier base (`851dfd445`; not repeated for `4e41ba459`). The only related file is `AGENTS.md` at the top of the repository: it
   contains instructions for AI coding agents working on NVC (how to build, test and keep edits minimal). It is not a contribution policy and says nothing about disclosure. The README
   states no AI policy. No commit message of the repository mentioned AI assistance. So there is no rule to follow and none to break; this is your decision. Options, none chosen for you:
   ask the maintainer in a short issue or email before the first pull request, or state it in the pull request text. Each `PR.md` contains the line `AI-DISCLOSURE: decide before submitting`:
   replace it with your statement or delete it. Whatever you decide, the human who submits takes responsibility for the contribution: its correctness, its licence and answering review.
4. Check that nobody else already fixed the same thing upstream (`git fetch upstream` and search the log, section 10).
5. Do not push the `lvdsim` branch of the fork to NVC upstream: only your own pull request branches.

## 7. Applying a patch and your identity

The patches carry no identity and no sign-off. For each pull request, in your clone of your fork:

```
git config user.name "Your Real Name"
git config user.email "you@your-real-address"
git checkout -b verilog-time 4e41ba459                  # or the current master of nickg/nvc, see section 10
git apply /path/to/lvdsim/kernel/upstream/01-verilog-time-delay-timeformat/01-verilog-time-delay-timeformat.patch
git add -A
git commit -s                                           # paste "Suggested commit message" of PR.md; -s adds Signed-off-by from your identity
```

Use `git apply --3way` or `patch -p1` if the base is newer. Do the same for `02-bug-fixes` (branch `bug-fixes`) and `03-interactive-mode` (branch `interactive-mode`), each started from the same
base: the patches do not depend on each other. Check the result with `git log --format='%h %an <%ae> %s' -1` and `git show --stat`. The disclosure line of `PR.md` is not in the commit message
unless you add it.

## 8. The order of the pull requests, and why

They are independent, so any order works technically. A suggested one, by risk:

| # | Folder | Lines (src/test) | Risk | Why here |
|---|--------|------------------|------|----------|
| 2 | `02-bug-fixes` | src +89 -23, test +222 | low to medium | small, separately explained fixes; item 4 touches the scheduler loop and item 6 is a design question |
| 1 | `01-verilog-time-delay-timeformat` | src +402 -9, test +183 -12 | medium | changes the output of existing designs (`$time`, `%t`, `ivtest2` gold file), node layout change, and carries the rounding question |
| 3 | `03-interactive-mode` | src +1163 -73, test +271 | highest | new features and two new source files; ask first (section 12) |

The maintainer has little review time: smaller, clearly correct changes first build trust. Open one pull request at a time or a few at a time. Because no patch touches `NEWS.md` and the test
lists are edited at different places, a merge of one does not make the next one conflict; after a merge you still do `git fetch upstream && git rebase upstream/master` on the others.

## 9. Submitting

### With GitHub (web and CLI)

```
# once: fork and add the fork as a remote
gh auth login
gh repo fork nickg/nvc --clone=false          # or press "Fork" on the web page
git remote add fork https://github.com/YOUR-USER/nvc.git

# per pull request (example: PR 1, branch verilog-time)
git push fork verilog-time
gh pr create --repo nickg/nvc --base master --head YOUR-USER:verilog-time \
    --title "Verilog: scale delays and \$time by the timescale, add \$realtime, \$timeformat and %t, fix the unit s" \
    --body-file pr-description.txt            # the text of the "Pull request description" section of PR.md
```

Without the CLI: push as above, open `https://github.com/YOUR-USER/nvc`, press "Compare and pull request" for the branch, check that the base is `nickg/nvc` `master`, paste the title and
the description from `PR.md`.

### By email (git send-email)

The README says patches may be sent as pull requests or with `git send-email` (https://git-send-email.io/ explains the one time setup of the SMTP server). The address the README gives for the
maintainer is `nick@nickg.me.uk` (the mailto link of "Reporting bugs" uses `nick+nvc@nickg.me.uk`; check README.md before you send).

```
git format-patch -1 -o /some/dir               # after the commit of section 7
git send-email --to=nick@nickg.me.uk --annotate /some/dir/0001-*.patch
```

Paste the motivation and the verification of `PR.md` into the cover text. Do not send the `.patch` files of `kernel/upstream/` directly: they have no author and no commit message.

## 10. Keeping the patches current while upstream moves

```
git fetch upstream                          # read only
git ls-remote upstream master               # compare with 4e41ba45965e2bc203835ed6c7cc63575c9cac3d (the base of the patches)
git checkout verilog-time
git rebase upstream/master                  # conflicts are most likely in test/regress/testlist.txt; keep both sides
# rebuild; after a node layout change in upstream or in PR 1: make clean-libs && make bootstrap
make check QUICK=1 && bin/run_regr <the new tests of this branch> && make check
git push --force-with-lease fork verilog-time    # only your own PR branch
```

How the lvdsim fork follows: the branch `lvdsim` is the source of truth for lvdsim builds (`kernel/PATCHES.md`). After upstream merges a change (or a release
contains it), rebase or merge the `lvdsim` branch of the fork on the new upstream commit; git drops the commit that is already upstream, and its row in
`kernel/PATCHES.md` gets the status `merged` and its folder in `kernel/upstream` is removed. If the maintainer asks for changes, the branch changes to the same code, so keep the fork behaviour and the
pull request in step, or document the difference as in the table of section 2.

### Rebuilding from the patches

Clone `https://github.com/nickg/nvc.git` (or your fork), check out `4e41ba459`, and apply `kernel/upstream/NN-*/*.patch` as in section 7. Each patch applies to the base alone without offsets (`git apply -v`); on a newer commit
use `git apply --3way` and fix the context of `test/regress/testlist.txt` by hand if needed.

## 11. Likely reviewer questions and answers

| PR / item | Question | Answer |
|-----------|----------|--------|
| 1 | Why does `$time` round (ties up)? | `$time` is rounded to the nearest unit of the module's time unit and a tie rounds up: half a unit is added before the integer division, so 6.5 ns under a 1 ns unit gives 7 (`$realtime` keeps the fraction). The project owner decided this on 2026-11-07 on the evidence of a measurement: Vivado 2022.2 `xsim` gives, under `1ns/1ps`, 0.4 ns to 0, 0.5 to 1, 16.4 to 16, 16.5 to 17, 16.6 to 17, 17.5 to 18, and under `10ns/1ns` 1.5 units to 2, 1.6 to 2, 2.4 to 2, 4.1 to 4, 5.6 to 6 (the files and the command are in `kernel/upstream/01-verilog-time-delay-timeformat/PR.md`; NVC with the patch prints the same eleven lines). **The wording of the IEEE standard was not read, so do not say that the standard requires rounding and do not cite section numbers**; the claim is "matches the behaviour of the reference simulator xsim 2022.2 (measured), standard wording not checked". The PR keeps the question open. Truncating again is one line in `scale_time_function()` plus the expected values of `vtimescale1` and two lines of the `vtimeformat1` gold file. |
| 1 | Why divide in the parser and not in the VPI function? | The unit is a property of the `timescale` directive at the call site, known in the parser. `%t` and `vpi_get(vpiTimeUnit, call)` use the unit stored on the call node, which is why the node layout changes. |
| 1 | Why is the gold file `ivtest2` changed? | It printed `$time` in femtoseconds, which only matched the bug, and `%0t` of it in the raw count. |
| 1 | Why change the node layout? | `%t` needs the time unit of the module that contains the call and the precision of the design. They are stored in `I_IVAL` of `V_MODULE`, `V_INST_BODY` and `V_SYS_TCALL`. Libraries have to be rebuilt, which `make bootstrap` does. |
| 1 | Why a global `time_format` in `vpi-systf.c`? | `$timeformat` sets one global format for all later `%t` conversions (the wording of the standard was not checked). |
| 1 | Why 20 as the default minimum field width? | It is what the code implements for `%t` without a width and for `$timeformat` without arguments. That this matches the standard was not verified, so do not cite a section number; ask the maintainer to confirm the value. |
| 2.1 | Why a new testlist option and not a new test mode? | The CI already runs `run_regr -r` (reheat mode); `top=` only adds a second argument to `-e`. The test is only meaningful in reheat mode and passes in the normal mode with and without the fix. |
| 2.2 | Should a procedural `assign` be in the sensitivity list at all? | `always @(*)` is sensitive to everything read by the statement; the `assign` right hand side is read like in the other assignments. Real constants read nothing. |
| 2.3 | Why is a hierarchical reference not checked as a net? | `vlog_check_net_lvalue()` runs before elaboration, when the referenced object is unknown. If you want the check afterwards, `href_lval1` shows the cases that must pass. |
| 2.4 | Is `next_is_delta` the right place? Does this change VHDL? | Only when `nonblockq` was not empty (Verilog) and a heap still has entries after it ran. VHDL never fills that queue. The full regression suite passes. |
| 2.5 | Why only `integer` and `string`? | Only these map to Verilog parameters (an untyped parameter holding a packed string); other types still give the existing error. A non literal string expression is a fatal error: the value must be known at elaboration. |
| 2.6 | Why this lookup order? Why no test? | Library of the instantiating unit first, then the other loaded libraries, like a VHDL component without binding. `run_regr` has one work library, so there is no regression test; the reproducers are in `PR.md`. Tell me which order you prefer. |
| 2.7 | Why not fix the Windows exit handlers? | The deadlock is in the C runtime exit lock and not in NVC; draining the async queue first is what `fatal_exit()` already does. There is no test because it needs a compilation in flight. |
| 3 | Why JSON, and why not parse the text replies? | Parsing the human readable replies of a pipe is fragile. `-json` is opt-in and does not change the text replies. Tell me if you prefer other command names or a different reply format. |
| 3 | Why a new waveform format? | An FST file cannot be read before it is closed. The live stream is an append only text format. If you would rather make FST readable during the run (hierarchy written up front, `fstWriterFlushContext` after each `run`), drop item 2 of the patch (see section 12, draft B). |
| all | Does `make check` pass? | See `kernel/upstream/VERIFY.md` for the numbers per tree. |

## 12. Larger feature proposals: open an issue first

PR 3 is a set of new features, not bug fixes, and PR 2 contains one design choice (item 6, library lookup). The maintainer has limited time and may have other plans (for example his own scripting
interface); open an issue first and ask. Suggested texts follow; adapt and sign them yourself.

### Issue draft A: machine readable replies and run control in the interactive shell

> **Title:** Would you accept machine readable replies and `step`/`breakpoint`/`processes` commands in `nvc -i`?
>
> Hello, I am building a GUI front end for NVC (an open source waveform viewer and simulation control program). It never links NVC: it starts `nvc -i` as a child process and talks to the Tcl
> shell over stdin and stdout, and reads waveforms from files. To do this reliably I added a few things to the shell in a fork, and I would like to know whether you would accept them upstream.
>
> 1. A `-json` option on `run`, `find`, `examine` and `describe` that returns the result as one line JSON object with a version field, for example `run -json 10 ns` gives
>    `{"v":1,"now":10000000,"delta":0,"status":"paused"}`. Without the option nothing changes.
> 2. `processes [-json] [pattern]` which lists the processes with path, state (ready, delayed, waiting) and source file and line.
> 3. `step [-delta|-time]` which runs one delta cycle or one time step, and `breakpoint add|list|delete` for breakpoints on simulation time that `run` honours. This needs one new function in
>    `rt/model.c` (run one cycle).
> 4. `--wave[=file]`, `--format`, `--gtkw`, `--include`, `--exclude` for `nvc -i` and `nvc --do`, the same options as `nvc -r`, so that a waveform can be written from a shell session.
>
> There is a patch with regression tests (`tcl` tests in `test/regress`) against current master. Would you accept these, and if so in which form (one pull request or several, different command
> names, a different reply format)? If you prefer a different approach for any of them I would rather change my side than keep a fork. I can send the patch as a pull request or with `git send-email`.

### Issue draft B: a waveform stream that can be read while the simulation is running

> **Title:** Reading waveforms while a simulation is running (FST cannot be read before it is closed)
>
> Hello, my GUI needs to show waveforms while `nvc -i` is still running a session. An FST file written by `fstapi` cannot be read before it is closed because the hierarchy is written at the end,
> so I added a small append only text format (`--format=live`: header, hierarchy first, then `#time` and value lines, flushed after every `run`) and a reader in the GUI. I do not expect you to
> want my format in NVC. Questions: (1) is there an existing or planned way to read an FST file (or VCD) while it is written, for example a periodic flush of the hierarchy, (2) would you accept a
> change in `wave.c` that makes the FST output readable during the run (writing the hierarchy up front, calling `fstWriterFlushContext` after each `run`), which would remove the need for my
> format, and (3) if you do want an additional format, how should new source files and their copyright be handled given that you do not accept new copyright owners in `src/`? The code is in
> two new files `src/rt/wave-live.c` and `.h` (about 250 lines) plus a dispatch layer in `wave.c`.

(If the answer to the second question is yes, the useful upstream change is in `wave.c` only and the lvdsim reader changes to read FST incrementally; that is a design change of lvdsim, not a patch
to prepare today. In that case PR 3 is sent without the live stream: remove `src/rt/wave-live.[ch]`, the `Makemodule.am` line, and the `WV` dispatch from `wave.c`.)

### Issue draft C: module lookup across libraries (item 6 of PR 2)

> **Title:** Where should the elaborator look for Verilog modules instantiated from VHDL components and from other Verilog modules?
>
> A VHDL component bound to a Verilog module that lives in another library is reported as "ULIB.LEAF is not a VHDL design unit", and a Verilog module inside a library other than `work` looks for its
> children only in `work`. My patch looks in the library of the instantiating unit for the first, and in the other loaded libraries (`--map`) when the module is not found in the parent's library for
> the second. Is the lookup order acceptable, or should it follow the rules for VHDL (library clauses) or the SystemVerilog `uselib`/config rules? I can write the regression tests if you tell me how
> to get two libraries into `run_regr`.

## 13. After you send: answering review

* Reply on the pull request; push fixes to the same branch (`git commit --fixup` then `git rebase -i --autosquash upstream/master`, and `git push --force-with-lease fork <branch>`). The maintainer
  prefers a clean history of one topic per commit; if he asks for PR 1 or PR 2 to be split, the items are independent and the lvdsim patch numbers in section 2 show the natural split.
* If he asks for a different design, change the code in the branch and tell us in the lvdsim repository: the lvdsim patch should get the same change or be dropped when upstream merges.
* If he rejects a patch, mark the row in `kernel/PATCHES.md` as `rejected-keep-local` and keep the commit on the `lvdsim` branch.
* Be patient: he says himself that reviewing is a spare time activity.

## 14. One page checklist

- [ ] Account, fork of `nickg/nvc`, `gh auth login` done
- [ ] Read README "Contributing", `contrib/STYLE.md`, `AGENTS.md`
- [ ] `git ls-remote https://github.com/nickg/nvc.git master` compared with `4e41ba45965e2bc203835ed6c7cc63575c9cac3d` (the base of the patches); if different: fetch and rebase
- [ ] Decided about AI disclosure (ask the maintainer, or state it in the PR text); line `AI-DISCLOSURE: decide before submitting` replaced or removed in every `PR.md` text you paste
- [ ] Identity set in the repository you commit from; `git log --format='%an <%ae>' -1` shows your name
- [ ] Decided about the header of `src/rt/wave-live.[ch]` (section 4) before PR 3
- [ ] Every diff read by you; Linux build and `make check` run by you on the final branch
- [ ] PR 1: `make bootstrap` after applying; rounding question of `$time` kept in the description
- [ ] Issues A, B, C posted before PR 3 and before item 6 of PR 2 (nothing opened as a PR until answered)
- [ ] After merge: mark rows `merged` in `kernel/PATCHES.md`, rebase the fork's `lvdsim` branch
