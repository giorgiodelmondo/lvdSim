# lvdsim examples

Mixed-language designs (VHDL, Verilog, SystemVerilog) and library demos. The examples also show real-valued (analog-style) signals
next to digital logic in one simulation; there is no analog solver.

Every example folder has `run.sh` and `run.bat`. **With no argument they compile the design and open the lvdsim GUI with the example's
`sim.do`**: the waveform grows live in the window, and closing the window ends the run. **`--batch`** (`/batch` also works in `.bat`
files) runs the previous behaviour without a window: it simulates and prints the result lines given for each example below.
`--help` prints the usage. Details are in [section 1](#1-set-up-the-shell) and the table below.

| Folder | What it shows |
|--------|---------------|
| `mixed_language/` | VHDL analog source and 8-bit ADC, a Verilog moving-average filter, a SystemVerilog threshold monitor, VHDL testbench. No third-party code. |
| `vivado_unisim/` | A comparator on a real-valued input feeding Xilinx unisim primitives (IBUF, BUFG, FDRE) and a Verilog edge counter. Needs the unisim sources from your own Vivado installation. |
| `xpm_unisim/` | A VHDL design with Xilinx Verilog primitives (IBUF, BUFG, FDRE, LUT6, SRL16E) and XPM blocks (`xpm_cdc_single`, `xpm_cdc_gray`, `xpm_fifo_sync`), and a Verilog testbench with RAMB18E1. `glbl` as second top level. Needs Vivado's Verilog unisim and XPM sources. |
| `stress_10k/` | A stress test: 118 lanes of VHDL, Verilog, SystemVerilog and Xilinx unisim cells plus unimacro blocks, about 10 000 signals, 20 ms of simulated time at 50 MHz. Needs Vivado's unisim and unimacro sources. |

Two more folders: `altera_mf/` (Altera megafunctions and LPM from VHDL, libraries from your own Quartus installation, see
[altera_mf/README.md](altera_mf/README.md)) and `reports/` (reports of every severity and a Verilog `$display`, for the transcript links and the
event markers of [docs/gui-reports.md](../docs/gui-reports.md)).

All were run on Windows with the lvdsim build of this repository.

What the run scripts do (they change into their own folder first):

| Command | GUI (default) | `--batch` prints |
|---------|---------------|------------------|
| `mixed_language/run.sh` | `lvdsim-gui -do sim.do work.tb_mixed` | `events {"00000010"} avg {"00101110"}` |
| `reports/run.sh` | `lvdsim-gui -do sim.do work.tb_reports work.mon` | the reports of `tb_reports.vhd` and one Verilog `$display` line |
| `altera_mf/run.sh` | `lvdsim-gui -L altera_mf -L lpm -do sim.do work.alt_demo` | `sum=61 cnt=3 ram=90` and `PASS` |
| `vivado_unisim/run.sh` | `lvdsim-gui -L unisim -do sim.do work.tb_xilinx` | `count {"00001010"} above '0'` |
| `xpm_unisim/run.sh` | `lvdsim-gui -L xpm -L unisim_v -do sim.do work.xpm_demo work.glbl` (the VHDL design) | the `PASS` line of the VHDL design, then `PASSED` of the Verilog testbench |
| `stress_10k/run.sh [N_LANES] [RUN_TIME]` | `lvdsim-gui -L unisim -L unimacro -g N_LANES=118 -do sim.do work.stress_top`, a 200 us run | signal count, checksum line and wall time (20 ms) |

On Windows use `run.bat` in the same way (`run.bat --batch`). Without `lvdsim-gui` on `PATH` the `.sh` scripts fall back to plain
`vsim -do sim.do ...` (classic usage: the `vsim` copy of the tools starts the GUI); the `.bat` files stop with a message. If a tool cannot be found
the scripts say so and point to [section 1](#1-set-up-the-shell). `--batch` needs only the command line tools (`vlib`, `vlog`, `vcom`, `vsim`), not the GUI.

## 1. Set up the shell

The scripts call `vlib`, `vlog`, `vcom` and `vsim`. They are copies of `lvdsim-cli` that the build puts in
`build/mingw-debug/bin`. NVC is found through environment variables. In Git Bash, from the repository root:

```sh
B=$PWD/build/mingw-debug/bin
K=$PWD/kernel/nvc/build
export PATH="$B:/c/msys64/mingw64/bin:/c/Qt/6.11.1/mingw_64/bin:/c/Qt/Tools/mingw1310_64/bin:$PATH"
export LVDSIM_NVC=$K/bin/nvc.exe
export LVDSIM_NVC_LIBDIR=$K/lib
export LVDSIM_NVC_DLLDIR=C:/msys64/mingw64/bin
```

In a Windows command prompt:

```bat
set B=%CD%\build\mingw-debug\bin
set K=%CD%\kernel\nvc\build
set PATH=%B%;C:\msys64\mingw64\bin;C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%
set LVDSIM_NVC=%K%\bin\nvc.exe
set LVDSIM_NVC_LIBDIR=%K%\lib
set LVDSIM_NVC_DLLDIR=C:\msys64\mingw64\bin
```

`lvdsim-gui` is in the same folder, so the scripts find it with this `PATH`. With the portable package or the installer, the package folder
(or the installed folder) on `PATH` is enough; run the scripts from a copy of the example, because the examples are not part of the package.

**Extra GUI options from the environment.** The default (GUI) mode appends `LVDSIM_GUI_ARGS` to the `lvdsim-gui` command line. Use it for window
options or for a headless run that exits by itself, for example a picture of the finished run on a machine without a display:

```sh
QT_QPA_PLATFORM=offscreen LVDSIM_GUI_ARGS="--size 1500 850 --screenshot /tmp/mixed.png --settings /tmp/fresh.ini" examples/mixed_language/run.sh
```

```bat
set QT_QPA_PLATFORM=offscreen
set LVDSIM_GUI_ARGS=--size 1500 850 --screenshot C:\temp\mixed.png --settings C:\temp\fresh.ini
examples\mixed_language\run.bat
```

Paths with spaces are not supported there. `--settings` with a fresh file avoids the window size saved by an earlier run. The hook works
only when `lvdsim-gui` itself is started (not in the `vsim` fallback of the `.sh` scripts).

Build the tools first if `build/mingw-debug/bin` does not exist (`cmake --preset mingw-debug`, then
`cmake --build --preset mingw-debug`). The MSYS2 directory must come before Qt's MinGW directory.

## 2. mixed_language

Run it: the script compiles, then opens the GUI with `sim.do` (the signals are added, an analog trace shows the input, and the 2 us run
grows live). Close the window to end it:

```sh
examples/mixed_language/run.sh        # or examples\mixed_language\run.bat
```

Run it in batch mode (compiles, simulates 2 us without a window, prints the final values, writes `mixed.fst`):

```sh
examples/mixed_language/run.sh --batch        # or examples\mixed_language\run.bat --batch
```

Expected last line:

```
events {"00000010"} avg {"00101110"}
```

Open the GUI by hand after the script has compiled the design once (the `work` library exists):

```sh
cd examples/mixed_language
lvdsim-gui -do sim.do work.tb_mixed
```

To compile by hand, in this order (Verilog and SystemVerilog first, so that VHDL can instantiate them):

```sh
vlib work
vlog -work work src/avg_filter.v
vlog -sv -work work src/level_monitor.sv
vcom -2008 -work work src/adc_model.vhd src/tb_mixed.vhd
vsim -c work.tb_mixed
```

## 3. vivado_unisim

The Xilinx simulation libraries are under Xilinx's licence and are not in this repository. You need an
installed Vivado (or any copy of its `data/vhdl/src/unisims` folder).

1. Compile the unisim library once into `examples/vivado_unisim/unisim`. With no argument the script uses
   `$XILINX_VIVADO/data/vhdl/src/unisims`; you can also pass that folder yourself:

   ```sh
   export XILINX_VIVADO=/d/cad/Xilinx/Vivado/2022.2   # your installation
   examples/vivado_unisim/compile_unisim.sh
   ```

   On Windows use `compile_unisim.bat` and `set XILINX_VIVADO=C:\Xilinx\Vivado\2022.2`.
2. Run the design. The default opens the GUI with `sim.do` and a 2 us live run. With `--batch` there is no window: it prints `count` and
   `above` after 2 us and writes `xilinx.fst`:

   ```sh
   examples/vivado_unisim/run.sh                # GUI; or run.bat
   examples/vivado_unisim/run.sh --batch        # no window; or run.bat --batch
   ```

   Expected with `--batch`: `count {"00001010"} above '0'` (ten rising crossings of the 1.65 V threshold).
3. Open the GUI by hand (after the script has compiled the design):

   ```sh
   cd examples/vivado_unisim
   lvdsim-gui -L unisim -do sim.do work.tb_xilinx
   ```

The unisim package is large, so the script gives NVC a bigger heap (`LVDSIM_NVC_OPTS="-M 128m"`); without it
compilation stops with "memory exhausted". Everything is compiled as VHDL-2008, because Verilog units are always
analysed as 2008 and a design cannot mix standards. The unisim files are compiled with `-explicit`, which lets older
Xilinx files resolve ambiguous operators the way their authors expected (it maps to `nvc --relaxed`).

## 4. stress_10k

A large design to exercise the kernel, the live wave stream and the GUI. Each of the 118 lanes is a VHDL LFSR, a Verilog
accumulator, a SystemVerilog monitor, two Xilinx FDRE flip-flops and a LUT2. The top level adds a Xilinx IBUF and BUFG clock
path, a real-valued analog input with an ADC, a Xilinx `MULT_MACRO` and `BRAM_SINGLE_MACRO` (library unimacro) and a reduction tree.

1. Compile the Xilinx libraries once (unisim with its retarget layer, and unimacro). This took about 100 seconds here.
   This script takes the Vivado installation folder:

   ```sh
   export XILINX_VIVADO=/d/cad/Xilinx/Vivado/2022.2   # your installation
   examples/stress_10k/compile_libs.sh
   ```

   On Windows use `compile_libs.bat` and `set XILINX_VIVADO=C:\Xilinx\Vivado\2022.2`.
2. Run it. The default opens the GUI with a readable subset of the signals and a short run (200 us): the full design is heavy in the
   window (10 039 signals; an earlier debug build froze on a 20 ms live run). `--batch` runs the whole thing without a window:

   ```sh
   examples/stress_10k/run.sh                       # GUI, 118 lanes, 200 us
   examples/stress_10k/run.sh 20 1ms                # GUI, 20 lanes (lighter), 1 ms
   examples/stress_10k/run.sh --batch               # no window: 118 lanes, 20 ms, about 10 000 signals
   examples/stress_10k/run.sh --batch 500 20ms      # 500 lanes: about 40 000 signals
   WAVE=1 examples/stress_10k/run.sh --batch        # also write stress.fst
   ```

   The second argument reaches `sim.do` as the environment variable `LVDSIM_RUN`. On Windows use `run.bat` with the same arguments.
3. Open it in the GUI by hand with a readable subset of the signals (add them all with `add wave -r /*`):

   ```sh
   cd examples/stress_10k
   lvdsim-gui -L unisim -L unimacro -g N_LANES=118 -do sim.do work.stress_top
   ```

Measured on this machine (debug build of the tools, NVC release build):

| Case | Result |
|------|--------|
| Signals with 118 lanes | 10 039 (each lane adds about 80, the unimacro blocks about 1 500) |
| 20 ms at 50 MHz, no waveform | 92 s wall time |
| Same with a 1 us clock (20 000 clocks) | under 2 s |
| Waveform dump | about 8 MB and 9 s per simulated millisecond, so roughly 160 MB for the full 20 ms |
| GUI, all 10 039 signals added, 200 us run | about 5 s, headless |

The clock period and lane count are generics (`CLK_PERIOD`, `N_LANES`), set with `vsim -g`. `USE_MACROS=false` leaves out the
unimacro blocks.

Two pitfalls found while building it:

- A RAM address that is unknown makes the Xilinx RAM model crawl, and the run appeared to hang. The address now comes from a plain counter.
- The Xilinx XPM library (`xpm_cdc`, `xpm_fifo`) is not used here. It is SystemVerilog and refers to a global `glbl` module by hierarchical name;
  that works now (kernel changes formerly numbered 0006 to 0013, `kernel/PATCHES.md`), see `xpm_unisim/`.

## What was tested, and limits

- `mixed_language` was compiled, simulated and displayed in the GUI as described.
- `vivado_unisim` was run against the VHDL unisim library of Vivado 2022.2 (`data/vhdl/src/unisims`, the per-primitive layout, all
  primitives compile in about 15 seconds). It also works with an older single-file unisim copy. Other Vivado versions were not tried.
- `stress_10k` was run end to end against Vivado 2022.2 (batch run, a waveform run for 1 ms, and the GUI with 200 us).
- `vivado_unisim` and `stress_10k` use the VHDL Xilinx libraries (unisim, unimacro). The Verilog unisim primitives and XPM are in
  `xpm_unisim/` (run against Vivado 2022.2; coverage in `docs/verilog-coverage.md`).
- Signal paths start at the testbench instance: write `/clk`, not `/tb_mixed/clk` as other simulators would. The wave window
  names them `tb_mixed.clk`.
- `examine` cannot print `real` signals yet; look at them in the wave window. An analog row needs
  `add wave -format analog-step` or `analog-interpolated` (see `sim.do`), there is no `-analog` option.

## Vivado libraries for any project

The per-example `compile_libs` scripts above build only what their example needs, into the example folder. To build the Vivado
libraries once into a folder of your choice, with a `lvdsim.ini` for them, use `scripts/libs/compile_libs.sh` (or `.bat`); see
`docs/libraries.md`.
