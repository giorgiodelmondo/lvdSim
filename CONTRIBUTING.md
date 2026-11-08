# Contributing to lvdsim

Thanks for your interest. lvdsim is a front end (Qt GUI, waveform viewer, command line tools) on top of a small
fork of the NVC simulator. This page tells you how to build it, where things are, and which rules a change has
to follow. Please also read the [Code of Conduct](CODE_OF_CONDUCT.md).

## Build and test

You need CMake 3.25 or newer, Ninja and a C++20 compiler. Catch2 3.7.1 and zlib 1.3.1 are downloaded by CMake
(FetchContent) on the first configure, so the first build needs network access.

**Windows (the maintainers' setup).** The presets `mingw-debug` and `mingw-release` use Qt 6.11 and its MinGW 13.1
compiler installed under `C:/Qt` (see `CMakePresets.json`; edit a copy as `CMakeUserPresets.json` if your Qt lives
elsewhere). Put the MinGW and Ninja directories on `PATH`, then:

```
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

**Linux.** The preset `linux-debug` builds into `~/build/lvdsim/linux-debug`. The GUI is off by default;
add `-DLVDSIM_BUILD_GUI=ON` once the Qt 6.8+ development packages are installed.

```
cmake --preset linux-debug -DLVDSIM_BUILD_GUI=OFF
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Package lists, the GUI build, running the examples and troubleshooting are in
[docs/building-linux.md](docs/building-linux.md). Without the GUI only `libwave` and the tools are built; that is
enough for most changes outside `gui/`. The example scripts (`examples/*/run.sh`, `run.bat`) open the GUI by default; pass `--batch` to run
them without a window, which is what to use over SSH, in CI and in the checks (see [examples/README.md](examples/README.md)).

**The kernel.** The integration tests and the examples need an `nvc` built from our fork (branch `lvdsim` of
`giorgiodelmondo/nvc`). NVC is not in this
repository; run `kernel/build/fetch-nvc.sh` and follow [docs/getting-the-kernel.md](docs/getting-the-kernel.md).
Tests that need `nvc` report "skipped" when it is missing, so a plain `ctest` is still meaningful. Set
`LVDSIM_NVC` and `LVDSIM_NVC_LIBDIR` to run them (see [docs/building-linux.md](docs/building-linux.md#4-run-the-tests)).

GUI tests run headless with `QT_QPA_PLATFORM=offscreen`.

## Making a release

`scripts\make-windows-release.bat` and `scripts/make-linux-run.sh --portable` (or `scripts\make-linux-run.bat` from Windows) build, package and verify the
release files in a new folder each time. Options, outputs and what is checked: [docs/packaging.md](docs/packaging.md#making-a-release).
The version is set only in `project(lvdsim VERSION ...)` of `CMakeLists.txt`; the installers are not code signed.

## Layout

| Folder | Content |
| --- | --- |
| `libwave/` | Waveform core: store, FST and live-stream readers, level-of-detail index, display model. Plain C++20, no Qt. |
| `gui/` | Qt 6 Widgets application, waveform widget, renderers (QPainter and QRhi), the session that drives `nvc`. |
| `tools/` | The command line front end (`lvdsim-cli` and its copies `vlib vmap vcom vlog vsim`). |
| `tcl/compat/` | Command package loaded into the NVC Tcl shell. Pure Tcl. |
| `kernel/` | Build and fetch scripts for NVC, `PATCHES.md` (the fork branch) and `upstream/` (pull request material). |
| `third_party/` | Vendored fstapi (MIT). |
| `packaging/`, `scripts/` | Installers, bundle scripts, library compile scripts, `check-headers.py`. |
| `examples/`, `docs/` | Example designs and documentation. |

## Rules for code

- C++20, built with `-Wall -Wextra -Wpedantic -Werror`. Namespaces `lvd::wave` (libwave), `lvd::gui`, `lvd::tools`.
  Files are `snake_case.cpp`, types `PascalCase`, functions and variables `camelCase`, constants `kPascalCase`.
- No exceptions across the renderer interface or the `nvc` process boundary; return `lvd::Result<T>`.
- Formatting comes from `.clang-format` (run it on files you change) and `.editorconfig`. Do not reformat code you
  are not changing.
- **Every change needs tests.** Catch2 for `libwave` and `tools`, QTest for GUI widgets. Performance-sensitive
  code (store, level-of-detail index, renderers) also gets a benchmark entry in `libwave/bench` or `gui/bench`.
- Every source file starts with `SPDX-License-Identifier: GPL-3.0-or-later`. `python3 scripts/check-headers.py`
  reports missing lines, `--fix` adds them.
- Qt Test binaries print nothing under `ctest`; run one directly with `-o file,txt` to see each case.

## Rules about structure and licence

These are not style preferences; a pull request that breaks one will not be merged.

- **NVC is never linked into the GUI or the tools.** All interaction goes through the `nvc` program: the command
  line for compile and elaborate, the Tcl shell (`nvc -i`) over standard input and output, and waveform files.
- **`libwave` has no Qt dependency** and builds and tests without Qt.
- **Qt Widgets only, LGPL modules only** (Core, Gui, Widgets, Network, Test). No Qt Charts, Qt DataVisualization,
  QML or Qt Quick.
- Drawing goes through the `IRenderer` interface. The QPainter renderer must always work; the QRhi (GPU) renderer
  is an optional accelerator with a run-time fallback.
- **Compatibility means command names and option syntax.** Write every command from the behaviour it must have.
  Never copy source code, documentation text, scripts or help strings from another vendor's simulator, and do not
  paste them into issues or pull requests either. Do not use the product names of other simulators in code,
  documentation or commit messages; "familiar simulator commands" is how we describe it.
- A new third-party dependency needs a note in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and a
  GPL-3.0-or-later compatible licence. Today the allowed code is NVC, fstapi, Qt (LGPL), Catch2 and zlib.
- When you implement a command, add a row to `docs/command-compat.md`.

## Changes to the NVC kernel

The kernel is the branch `lvdsim` of the fork `giorgiodelmondo/nvc`: upstream NVC plus a few commits, nothing is patched at
build time. Every change is a commit on that branch with its own pull request folder `kernel/upstream/NN-name/` (the material for
mainline), and a row in `kernel/PATCHES.md` (title, commit, PR folder, upstream status, and the regression test it adds under
`test/regress`). `kernel/PATCHES.md` says how to add a change step by step.

- Write small upstream-style C commits that follow NVC's own style (`contrib/STYLE.md` in the NVC source). Do not
  reformat or restructure code outside the lines the change needs, and never run `clang-format` over `kernel/`.
- One topic per commit; add the PR folder and the `PATCHES.md` row in the same change; run NVC's own tests
  (`docs/kernel-tests.md`) on Windows and Linux.
- **Line endings:** keep the NVC tree LF (the autotools scripts do not run with CRLF in MSYS2).
- Most fixes are worth sending to NVC itself. How we prepare and submit them is in
  [docs/upstream-contribution.md](docs/upstream-contribution.md).

## Licence of contributions

lvdsim is licensed under **GPL-3.0-or-later** ([LICENSE](LICENSE)). By submitting a pull request you state that
you wrote the change (or have the right to submit it), and that it may be distributed under that licence. Code
that is not yours must say where it came from and be under a compatible licence. We do not require a
`Signed-off-by` line or a contributor agreement: the project is small and the statement above is enough. If that
changes, this page will say so.

## Reporting bugs and asking for features

Use the issue forms. A good bug report has the operating system, the lvdsim version and package type, the
renderer shown in the status bar, the output of `nvc --version`, a minimal HDL example and the commands to run
it, what you expected and what happened, and the transcript as text. For a security problem do **not** open an
issue; see [SECURITY.md](SECURITY.md).

## AI-assisted contributions

You may use AI tools. You are responsible for the result as if you had typed it: read and understand every line,
run the tests, and make sure it contains no text or code copied from sources whose licence does not allow it
(see the compatibility rule above). **Disclose the use in the pull request description**: which tool, and for
what part (code, tests, documentation). Pull requests that the author cannot explain, or that are bulk
generated without review, will be closed.

## Pull request checklist

The pull request template lists it. In short: it builds warning free, tests are added and `ctest` passes,
documentation is updated, kernel changes are in `kernel/PATCHES.md`, no vendor code or names, the AI disclosure
is filled in, and you agree to the licence.
