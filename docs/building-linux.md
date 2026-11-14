# Building lvdsim on Linux

This page builds the NVC fork and lvdsim from source on Linux, runs the tests and the examples, and makes the self-contained bundle and
the `.run` installer. It was written and verified on **Debian 13 (trixie) in WSL** (GCC 14, Qt 6.8.2, LLVM 19, glibc 2.41, x86_64).
Other distributions should work with the equivalent packages but were not tried.

If you only want to use lvdsim, install the `.run` file instead ([README.md](../README.md#install-on-linux)).

## 1. Packages

One line (`sudo apt install ...`):

```sh
sudo apt install build-essential cmake ninja-build pkg-config flex bison autoconf automake libtool check llvm-dev libffi-dev \
  libzstd-dev zlib1g-dev libncurses-dev tcl-dev libreadline-dev git xz-utils \
  qt6-base-dev qt6-base-private-dev qt6-shadertools-dev qt6-svg-dev libgl-dev libvulkan-dev patchelf
```

What each group is for:

| Packages | Needed for |
| --- | --- |
| `build-essential cmake ninja-build pkg-config git xz-utils` | everything |
| `flex bison autoconf automake libtool check llvm-dev libffi-dev libzstd-dev zlib1g-dev libncurses-dev tcl-dev libreadline-dev` | the NVC fork (`libreadline-dev` is required by `--enable-tcl`; `check` is the unit test framework of `make check`) |
| `qt6-base-dev qt6-base-private-dev qt6-shadertools-dev libgl-dev libvulkan-dev` | the GUI. `qt6-base-private-dev` provides `Qt6GuiPrivate` (the GPU renderer uses the RHI classes), `qt6-shadertools-dev` compiles the GPU shaders |
| `qt6-svg-dev` | its run-time library `libqt6svg6` and the SVG plugins, which the bundle copies; the GUI itself compiles without the dev files |
| `patchelf` | only `scripts/package-linux-appdir.sh` (bundle and installer) |

Not needed: `qt6-tools-dev`, `squashfs-tools`, `python3-pil` (the icon is a checked-in file; nothing generates images). Only the tools and
tests of lvdsim (no GUI) need just `build-essential cmake ninja-build`.

Qt 6.8 or newer is required (Qt 6.11 on Windows is the development version; the Linux code uses calls that exist in 6.8).

## 2. Build the NVC fork

lvdsim runs the simulator as a separate program, so this step is independent of the GUI. The kernel is the branch `lvdsim` of the
fork `https://github.com/giorgiodelmondo/nvc.git` (upstream NVC `4e41ba459` plus the lvdsim changes, see `kernel/PATCHES.md`). Nothing is
patched: clone the branch and build it. **Use that branch**: an older kernel crashes or hangs when the Tcl shell quits (the shell `exit`
fix is on the branch) and truncates Verilog time units.

Do not build inside the checkout if it lives on a Windows drive (`/mnt/d`). Clone the branch into the Linux file system (from the
repository root; the clone of `kernel/nvc` is local, no network is needed, or use the URL of the fork):

```sh
git clone -b lvdsim /path/to/lvdsim/kernel/nvc ~/build/nvc-src      # or: kernel/build/fetch-nvc.sh --dest ~/build/nvc-src
cd ~/build/nvc-src
./autogen.sh
mkdir -p ~/build/nvc-linux && cd ~/build/nvc-linux
~/build/nvc-src/configure --enable-tcl
make -j"$(nproc)"
make -j"$(nproc)" check       # optional, a few minutes
```

The result is `~/build/nvc-linux/bin/nvc` and `~/build/nvc-linux/lib`. Check the version line with `~/build/nvc-linux/bin/nvc --version`; it
contains the abbreviated commit of the branch (`git -C ~/build/nvc-src log -1 --format=%h`).

Test results of the fork on Linux (all 1525 regression entries pass, unit tests 0 failures) are in
[kernel-tests.md](kernel-tests.md#linux-wsl-debian).

In this repository's own verification (2026-11-08) the nvc build was `~/build/nvc-fork-linux-s1` (source clone `~/build/nvc-fork-src-s1`,
commit `754af76cc`, built exactly as above; the merge `53d41eeae` was checked the same way in `~/build/nvc-fork-linux-s2`). Replace `~/build/nvc-linux` by that name if you reuse it.

## 3. Build lvdsim

From the lvdsim repository root (the checkout may stay on `/mnt/d`; only the build directory should be in the Linux file system):

```sh
cmake --preset linux-release -DLVDSIM_BUILD_GUI=ON
cmake --build --preset linux-release -j"$(nproc)"
```

The build directory is `~/build/lvdsim/linux-release` (preset `linux-debug`: `~/build/lvdsim/linux-debug`, with
`-DLVDSIM_BUILD_GUI=ON` too). The GUI is off by default, so the flag is needed. To use another build directory, add
`-B <dir>` to the configure line and use `cmake --build <dir>` and `ctest --test-dir <dir>` afterwards. The release preset also builds
the benchmarks. Programs are written to `<build dir>/bin`: `lvdsim-gui`, `lvdsim-cli` and its copies `vlib vmap vcom vlog vsim`.
Warnings are errors (`-Wall -Wextra -Werror`).

## 4. Run the tests

```sh
export QT_QPA_PLATFORM=offscreen
export LVDSIM_NVC=$HOME/build/nvc-linux/bin/nvc
export LVDSIM_NVC_LIBDIR=$HOME/build/nvc-linux/lib
ctest --preset linux-release
```

(With `-B <dir>`: `ctest --test-dir <dir> --output-on-failure`.) `QT_QPA_PLATFORM=offscreen` lets the Qt tests run without a display.
Without the two `LVDSIM_NVC*` variables the integration tests report "skipped" instead of running. Expected: all tests pass
(142 of 142 in this verification, release build with `-B`, Debian 13; `test_gpu_renderer` skips its own cases, headless GPU rendering is only set
up for Direct3D 11 and Metal, which counts as passed).

## 5. Run the examples

```sh
export PATH=$HOME/build/lvdsim/linux-release/bin:$PATH
cd examples/mixed_language
sh run.sh --batch
```

The last line must be `events {"00000010"} avg {"00101110"}`. Without `--batch`, `run.sh` opens the GUI with `sim.do` instead (it needs a display,
section 7; add `LVDSIM_GUI_ARGS="--screenshot /tmp/out.png --size 1500 850"` and `QT_QPA_PLATFORM=offscreen` for a headless picture). `run.sh` writes `work/` and `mixed.fst` into the example folder; delete
them when you are done. Without the exports of section 4 `vsim` cannot find nvc: set `LVDSIM_NVC` and `LVDSIM_NVC_LIBDIR` (the
variables are explained in [../examples/README.md](../examples/README.md)).

The GUI needs a display (section 7), or the headless screenshot of the tutorial:

```sh
QT_QPA_PLATFORM=offscreen lvdsim-gui --settings /tmp/s.ini -do sim.do work.tb_mixed --screenshot /tmp/out.png
```

## 6. The bundle and the `.run` installer

```sh
NVC_BUILD=$HOME/build/nvc-linux OUT_DIR=$HOME/build/lvdsim/appdir scripts/package-linux-appdir.sh --skip-build
```

`--skip-build` reuses the build of section 3 in `~/build/lvdsim/linux-release` (the script's fixed build directory; without it the script
configures and builds that preset itself). `NVC_BUILD` must be a build of the branch `lvdsim` of the kernel (section 2). The output is
`$OUT_DIR/lvdsim-0.1.5-linux-x86_64/` (the bundle, run it with `AppRun`) and `lvdsim-0.1.5-linux-x86_64.run` (the installer). The script
never overwrites: pick a new `OUT_DIR` or move the old outputs away first. `COMPRESS=gz` avoids xz.

What is inside and what comes from the host:

- Inside: the lvdsim programs, nvc and its libraries, Qt (Core, Gui, Widgets, Network, Svg and plugins), LLVM, libffi, zstd, Tcl,
  readline and the other libraries nvc needs, the Tcl compat package, licences. Binaries are stripped and get an `RPATH` to the bundle with
  `patchelf`.
- From the host: glibc (`libc`, `libm`, `libresolv`, the loader) and the graphics stack (`libGL`, `libEGL`, `libOpenGL`, `libGLX`, `libvulkan`,
  `libdrm`, `libgbm`, `libudev`, `libsystemd`). They must match the GPU driver, so they are never bundled.
- **Minimum glibc is 2.38** (the highest symbol version the bundled binaries need, built on Debian 13). Older systems (Debian 12,
  Ubuntu 22.04) cannot run the bundle; build from source there instead.

Details of the layout, the installer options and the verification are in [packaging.md](packaging.md#linux).

## 7. Running the GUI

On a Linux desktop run `lvdsim-gui` as usual. **Under WSLg** (Windows 11 or recent Windows 10) a window opens on the Windows desktop;
both back ends work:

```sh
QT_QPA_PLATFORM=xcb lvdsim-gui -do sim.do work.tb_mixed
QT_QPA_PLATFORM=wayland lvdsim-gui -do sim.do work.tb_mixed
```

`xcb` may need `libxcb-cursor0` (install it if Qt reports that the xcb plugin could not be loaded). In WSL there is no usable GPU
rendering path in the way lvdsim uses it, so the status bar shows `Renderer: QPainter (GPU not available: ...)`. The GPU renderer is
untested on Linux: it is the default, falls back to QPainter by itself, and `LVDSIM_RENDERER=cpu` forces QPainter (section 8).

## 8. Troubleshooting

- **CMake: `Qt6GuiPrivate` not found** (or `Could not find a package configuration file provided by "Qt6GuiPrivate"`): install
  `qt6-base-private-dev`. On Debian 13 `Qt6::GuiPrivate` is part of the `Qt6Gui` package, but the private headers come from that dev
  package; the RHI renderer cannot be compiled without them.
- **CMake: `Qt6ShaderTools` not found**: install `qt6-shadertools-dev` (it also provides the `qsb` shader compiler).
- **CMake: GL or Vulkan headers not found**: install `libgl-dev` and `libvulkan-dev`.
- **The tools build but no `lvdsim-gui`**: you forgot `-DLVDSIM_BUILD_GUI=ON`.
- **`vsim` hangs or crashes when it quits, with a crash inside LLVM**: the nvc build is older than the branch (it lacks the shell `exit` fix).
  Rebuild the kernel from the branch `lvdsim` (section 2).
- **`configure`: readline or a compatible library is required when TCL is enabled**: install `libreadline-dev`.
- **`./autogen.sh` fails with `fatal: not a git repository`**: the tree is not a git clone (an exported archive); clone the branch as in section 2, or run `autoreconf --force --install -I m4` there.
- **Tests print `skipped`**: `LVDSIM_NVC` and `LVDSIM_NVC_LIBDIR` are not set (section 4).
- **Qt: `could not connect to display` or `xcb plugin` errors**: set `QT_QPA_PLATFORM=offscreen` (tests, screenshots) or install a display
  stack (`libxcb-cursor0`; under WSLg check that `echo $WAYLAND_DISPLAY $DISPLAY` print something).
- **Boxes instead of letters in screenshots**: no fonts are installed (`sudo apt install fonts-dejavu-core`); set `QT_QPA_FONTDIR` to a
  folder of fonts only if you want specific ones.
- **GPU problems** (blank or garbled wave pane, errors from the graphics driver): GPU rendering on Linux is not tested. Force the
  QPainter renderer with `LVDSIM_RENDERER=cpu` or View > Waveform renderer > QPainter only. Offscreen, minimal and VNC platforms
  always use QPainter.
- **`.run` installer: `xz: not found`**: install `xz-utils` on the installing machine, or build the installer with `COMPRESS=gz`.
