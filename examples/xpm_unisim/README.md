# xpm_unisim: Xilinx Verilog primitives and XPM blocks

A VHDL design that instantiates the Verilog simulation models of Xilinx primitives (IBUF, BUFG, FDRE, LUT6, SRL16E) and XPM
blocks (`xpm_cdc_single`, `xpm_cdc_gray`, `xpm_fifo_sync`), plus a Verilog testbench for the primitives (including RAMB18E1).
Nothing from Xilinx is in this repository: the scripts compile the models from **your own Vivado installation** (tested with 2022.2).
To build these libraries (and unisim, unimacro) into any folder with a `lvdsim.ini`, see `scripts/libs` and `docs/libraries.md`.

What it shows, and what the fork needed for it (`kernel/PATCHES.md`, formerly patches 0006 to 0013):

- Xilinx models read a global module by hierarchical name (`glbl.GSR`, released after 100 ns). `glbl` is compiled from
  `data/verilog/src/glbl.v` and simulated as a **second top level**: `vsim work.xpm_demo work.glbl`.
- XPM is SystemVerilog in library `xpm`; `xpm_VCOMP.vhd` from the same installation declares it as VHDL components. VHDL string generics
  (`FIFO_MEMORY_TYPE => "distributed"`) reach the Verilog parameters.
- A Verilog parameter is a VHDL integer generic, so a `LUT6` with its 64 bit `INIT` cannot be configured from VHDL. `src/lut6_and.v` is a
  small Verilog wrapper that sets it. `FDRE`, `SRL16E` (16 bit INIT) and the others take integer generics directly.
- `library unisim_v;` in the VHDL file loads the Verilog unisim library so that the components bind to it.

## Run it

Set up the shell as described in `examples/README.md` (section 1), then, from this folder or the repository root:

```sh
export XILINX_VIVADO=/d/cad/Xilinx/Vivado/2022.2      # your installation
examples/xpm_unisim/compile_libs.sh                  # once, about a minute: libraries xpm and unisim_v
examples/xpm_unisim/run.sh                           # compile the designs, open the VHDL design in the GUI (sim.do)
examples/xpm_unisim/run.sh --batch                   # compile, run both testbenches without a window
```

On Windows use `compile_libs.bat`, `run.bat` (`run.bat --batch`) and `set XILINX_VIVADO=C:\Xilinx\Vivado\2022.2`. Without `--batch` the script
opens `lvdsim-gui` on the VHDL design (`work.xpm_demo work.glbl`) and waits until you close the window; the Verilog testbench is run by
`--batch` only.

Expected output of `--batch` (times and noise lines may differ):

```
== VHDL design: ...
** Note: 2555ns+0: FIFO words read: 8, counter 15, gray crossing 0
** Note: 2555ns+0: PASS
== Verilog testbench: ...
PASSED
```

`compile_libs` prints which unisim models it skipped (10 of 428 do not compile with this NVC yet, see `docs/verilog-coverage.md`).

Open the VHDL design in the GUI by hand (this is what `run.sh` starts; the designs must be compiled once):

```sh
cd examples/xpm_unisim
lvdsim-gui -L xpm -L unisim_v -do sim.do work.xpm_demo work.glbl
```

## Limits

- Signals of the second top level (`glbl`) are simulated but not visible in the shell or the wave window.
- `xpm_memory_*` and `xpm_fifo_*` with `FIFO_MEMORY_TYPE`/`MEMORY_PRIMITIVE` of `block` or `auto` fail to elaborate (hierarchical references into
  generate blocks); `distributed` works.
- `vsim -c` scripts here end without `quit`: with this design `quit` (the Tcl `exit`) does not return. See `docs/verilog-coverage.md`.
- Compile order for Verilog that instantiates a unisim model: the models must be in a library given with `-L` at `vsim` time.
