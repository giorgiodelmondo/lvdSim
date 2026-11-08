# altera_mf: Altera megafunctions and LPM from VHDL

`src/alt_demo.vhd` instantiates `lcell`, `lpm_add_sub`, `lpm_counter` and an `altsyncram` (single port RAM) with the
component packages `altera_mf.altera_mf_components` and `lpm.lpm_components`, writes a value, reads it back and prints `PASS`.
Nothing from Intel is in this repository: build the libraries from **your own Quartus installation** (tested with Quartus Pro 24.2):

```sh
scripts/libs/compile_libs.sh --quartus /d/cad/intelFPGA_pro/24.2 --out /d/libs/altera     # altera_mf, lpm, sgate, altera_primitives
cd examples/altera_mf
export LVDSIM_INI=/d/libs/altera/lvdsim.ini
./run.sh                 # compiles, then opens the GUI with sim.do; close the window to end the run
./run.sh --batch         # no window: prints the result lines
```

On Windows: `set LVDSIM_INI=C:\libs\altera\lvdsim.ini`, then `run.bat` or `run.bat --batch`. The scripts need `vlib`, `vcom` and
(`lvdsim-gui` or, for `--batch`, `vsim`) on `PATH`, see [../README.md](../README.md#1-set-up-the-shell).

Expected with `--batch`: `sum=61 cnt=3 ram=90` and `PASS`. The Quartus Pro `altsyncram` model only supports Arria 10 and Cyclone 10 GX,
hence `intended_device_family => "Arria 10"`. The same libraries can be built from the GUI: Tools > Compile libraries.
