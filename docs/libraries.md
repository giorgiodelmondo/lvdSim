# Vendor simulation libraries (Vivado and Quartus)

The first part is about the Xilinx Vivado libraries, "Altera (Intel Quartus) libraries" below about Quartus and "Compile libraries from the GUI" about the menu helper.

## Xilinx Vivado

`scripts/libs/compile_libs.sh` (and `compile_libs.bat`) compile the Vivado simulation libraries with the lvdsim tools
(`vlib`, `vmap`, `vcom`, `vlog`) into one output directory and write a `lvdsim.ini` there that maps them. After that any
project can use `-L unisim -L unimacro -L xpm` without per-example scripts.

Licence: the Xilinx sources are never copied into this repository and never redistributed. The scripts read them from your own
Vivado installation (read only) and write compiled libraries only into the output directory you choose. Check Xilinx's licence
before sharing the compiled libraries. Encrypted IP (IEEE 1735 `secureip`) is out of scope.

Tested only with **Vivado 2022.2** on Windows 11. Other versions were not tried.

## Use

```sh
# Git Bash; tools on PATH as in examples/README.md section 1 (the lvdsim tools must come before any other vcom/vlog)
export XILINX_VIVADO=/d/cad/Xilinx/Vivado/2022.2
scripts/libs/compile_libs.sh --out /d/libs/vivado                       # default set: unisim + unimacro
scripts/libs/compile_libs.sh --out /d/libs/vivado --only unisim_verilog,xpm
```

```bat
set XILINX_VIVADO=C:\Xilinx\Vivado\2022.2
scripts\libs\compile_libs.bat --out C:\libs\vivado
scripts\libs\compile_libs.bat --out C:\libs\vivado --only unisim_verilog,xpm
```

Re-running into the same directory is fine (the mapping is updated, files are recompiled). Running the opt-in set into a directory that
already has unisim and unimacro adds to it.

| Option | Meaning |
|--------|---------|
| `--vivado DIR` | Vivado installation. Default `$XILINX_VIVADO`. |
| `--quartus DIR` | Quartus installation (see the Altera section). Default `$QUARTUS_ROOTDIR`, then `$ALTERA_ROOT`. |
| `--out DIR` | Output directory. Default `./vivado_libs` (`./altera_libs` for an Altera only run). Gets one folder per library, `lvdsim.ini` and `logs/`. |
| `--only LIST` | Comma list of `unisim`, `unimacro`, `unisim_verilog`, `xpm` (Xilinx) and the Altera names below. Default `unisim,unimacro`; with only `--quartus` given, the Altera default set. `secureip-skip` is accepted and changes nothing: encrypted secureip models are never compiled. |
| `--quiet` | Print only the summary. |
| `--jobs N` | Accepted and ignored: files of one library are compiled one after the other (the compiles share one library directory). |

In a Windows prompt an unquoted comma splits arguments; the `.bat` file accepts `--only unisim_verilog,xpm` anyway.

Use the libraries from a project folder: copy or merge the written `lvdsim.ini` into it, or point `LVDSIM_INI` at it
(`set LVDSIM_INI=C:\libs\vivado\lvdsim.ini`), or map single libraries (`vmap unisim C:/libs/vivado/unisim`). Then:

```sh
vlib work
vcom -2008 -explicit -L unisim -L unimacro design.vhd
vsim -c -L unisim -L unimacro -do "run -all" work.top
```

Libraries are visible to a design only with `-L` (see `docs/command-compat.md`). The compiled libraries depend on the NVC build that made
them: after rebuilding or updating `nvc` (a changed library format gives "serialised format digest ... should be reanalysed") run the
script again.

Rebuild Verilog libraries after updating `nvc` even when they still load. The rounding of `$time` to the module's time unit (to the nearest
unit, a tie rounds up; older builds truncated) is built into the analysed unit when the Verilog source is parsed. A library analysed by an
older `nvc` keeps truncating `$time`: the serialised format digest did not change, so such a library still loads and runs, only the behaviour
differs. To get the rounding, recompile Verilog libraries (for example the Verilog `unisim`) with `scripts/libs/compile_libs`.

## What the libraries contain

| Library | Source in Vivado | Default | Content |
|---------|------------------|---------|---------|
| `unisim` | `data/vhdl/src/unisims` | yes | VHDL unisim: `unisim_VPKG`, `unisim_VCOMP`, `unisim_retarget_VCOMP` (a superset component package, compiled before the primitives), the 198 files of `primitive/` and the 864 files of `retarget/`, all VHDL-2008 with `-explicit`. 1064 files. |
| `unimacro` | `data/vhdl/src/unimacro` | yes | `unimacro_VCOMP` and the 12 `*_MACRO.vhd` (BRAM, FIFO, MULT, MACC, counters, ...). Needs `unisim`. |
| `unisim_v` | `data/verilog/src/unisims` | `--only unisim_verilog` | The 428 Verilog models, one `vlog` per file. |
| `xpm` | `data/ip/xpm` | `--only xpm` | `xpm_cdc.sv`, `xpm_memory.sv`, `xpm_fifo.sv` (`vlog -sv`) and `xpm_VCOMP.vhd` (components, `vcom -2008`). |

Verilog designs that use these models need `glbl`, which the models read by hierarchical name. Compile it from your Vivado and simulate it as
a second top level:

```sh
vlog -work work "$XILINX_VIVADO/data/verilog/src/glbl.v"
vsim -c -L xpm -L unisim_v -do "run -all" work.xpm_demo work.glbl
```

Order and flags are the proven ones: `LVDSIM_NVC_OPTS="-M 128m"` is set when you have not set it (`unisim_VCOMP` is huge), the retarget package
before the primitives and `retarget/*.vhd` after them. Files that contain a `` `protect `` block are skipped with a message and counted.
A failing file never stops the run; the summary lists library, files ok, failed and skipped, and the names of failed files
(`logs/failed.txt`, per-library logs in `logs/<library>.log`).

Exit status: 0 when `unisim` and `unimacro` (if requested) have no failed file; 1 when one of them fails or a source folder is missing;
2 for usage errors. Failures in `unisim_v` and `xpm` are reported but do not change the exit status (some Verilog models are known not to compile).

## Measured (Vivado 2022.2, Windows 11, debug build of the tools, release build of NVC)

| Library | Files ok | Failed | Time |
|---------|---------:|-------:|-----:|
| unisim | 1064 | 0 | 113 to 117 s |
| unimacro | 13 | 0 | 1 to 2 s |
| unisim_v | 418 | 10 | 21 to 28 s |
| xpm | 4 | 0 | under 1 s |

Default set about 2 minutes, everything about 2.5 minutes. Most of the time is process start-up per file.

Verified against these libraries: `examples/stress_10k` (10 039 signals, 20 ms run in 94 s, builds against `unisim` and `unimacro`),
`examples/xpm_unisim` (`run.sh --batch`: both testbenches print PASS and PASSED, `-L xpm -L unisim_v`), and a small design that uses only a copied
`lvdsim.ini` with `-L unisim`. A test with a made-up Vivado-like tree (`scripts/libs/test_libs.sh`, ctest `scripts_libs_compile`)
checks the summary, the encrypted-file skip, continuing after a failing file, the exit status and the written `lvdsim.ini`.

## Known failures

- `unisim_v`: ten of the 428 Verilog models do not analyse: DCIRESET, DCM_ADV, DCM_SP, DSP48E1, FRAME_ECCE3, GTXE2_CHANNEL, SIM_CONFIGE2,
  SIM_CONFIGE3, SYSMONE1, SYSMONE4 (reasons in `docs/verilog-coverage.md`). Of the 418 that compile, 303 elaborate and run; the others
  are wrappers of encrypted `SIP_*`, `B_*` and `BM_*` secureip modules (gigabit transceivers, PCIe, hard IP) and cannot be simulated by design,
  or use `defparam` and similar (see the coverage report). No file of the Vivado VHDL or Verilog unisim folders contains a `` `protect `` block;
  the encrypted code lives in the secureip folders, which the script never reads.
- `xpm_memory_*` and `xpm_fifo_*` with `block` or `auto` memory type do not elaborate (`docs/verilog-coverage.md`).
- Behaviour is only checked for the primitives and XPM blocks in the examples; "compiles" does not mean "simulates correctly".

## Altera (Intel Quartus) libraries

`compile_libs.sh --quartus DIR` (and `.bat`) compile the simulation sources of a Quartus installation, found in `<quartus>/eda/sim_lib`.
`DIR` is the version folder (`D:/cad/intelFPGA_pro/24.2`) or its `quartus` subfolder (what `QUARTUS_ROOTDIR` usually holds);
the default is `$QUARTUS_ROOTDIR`, then `$ALTERA_ROOT`. The sources are only read, never copied into this repository, and the compiled
libraries go to the output folder only. Check Intel's licence before sharing them.

```sh
scripts/libs/compile_libs.sh --quartus /d/cad/intelFPGA_pro/24.2 --out /d/libs/altera      # altera_mf, lpm, sgate, altera_primitives
scripts/libs/compile_libs.sh --quartus /d/cad/intelFPGA_pro/24.2 --out /d/libs/altera --only altera_lnsim,twentynm
```

```bat
scripts\libs\compile_libs.bat --quartus D:\cad\intelFPGA_pro\24.2 --out D:\libs\altera
```

Library names are the ones designs use: `library altera_mf; use altera_mf.altera_mf_components.all;`, `library lpm; use lpm.lpm_components.all;`,
`library sgate`, `library altera_primitives`, `library altera_lnsim`. Use them with `-L`, for example
`vcom -2008 -explicit -L altera_mf -L lpm design.vhd` and `vsim -c -L altera_mf -L lpm work.top` (`examples/altera_mf` is a small design that does
exactly this).

| Name in `--only` | Library | Sources (in `eda/sim_lib`) and order | Default |
|------------------|---------|--------------------------------------|---------|
| `altera_mf` | `altera_mf` | `altera_mf_components.vhd`, `altera_mf.vhd` (altsyncram, altpll, altfp_*, ...; 52 891 lines) | yes |
| `lpm` | `lpm` | `220pack.vhd`, `220model.vhd` | yes |
| `sgate` | `sgate` | `sgate_pack.vhd`, `sgate.vhd` (`-L lpm`) | yes |
| `altera_primitives` | `altera_primitives` | `altera_primitives_components.vhd`, `altera_primitives.vhd` (lcell, dffeas, ...) | yes |
| `altera_lnsim` | `altera_lnsim` | `altera_lnsim_components.vhd`, `altera_lnsim.sv` | no |
| `cyclone10gx`, `twentynm`, `tennm` | same name | `<family>_atoms.vhd`, `<family>_components.vhd` (`-L altera_lnsim`, so build `altera_lnsim` first; atoms before components) | no |
| `altera_verilog` | `altera_ver`, `sgate_ver`, `lpm_ver`, `altera_mf_ver` | `altera_primitives.v`, `sgate.v`, `220model.v`, `altera_mf.v` | no |

All VHDL is compiled with `-2008 -explicit`. Verilog and SystemVerilog units are compiled by `vlog`, which forces 2008 as everywhere else.
The order was found by experiment: the component package before the entities in `altera_mf` (either order analyses), `220pack` before `220model`,
`sgate_pack` before `sgate`, and for the device families the atoms before the components (`<family>_components.vhd` uses `work.<family>_atom_pack`, declared in the atoms file).

Encrypted files are skipped with a message and counted: names ending in `_ncrypt.*` and files with a `` `protect `` block. The `*_hssi_atoms`, `*_hip_atoms`
and `*_ncrypt.sv` transceiver and hard IP models are never read (some are hundreds of megabytes and encrypted). None of the files the script compiles is encrypted in Quartus Pro 24.2.

Exit status: 0 unless one of the default four fails (1) or a source is missing (1); failures in the opt-in libraries are reported only. 2 for usage errors.

### Measured (Quartus Pro 24.2, Windows 11, debug build of the tools, release nvc, fresh output folders under `D:/tmp`)

| Library | Files ok | Failed | Time |
|---------|---------:|-------:|-----:|
| altera_mf | 2 | 0 | 1 s |
| lpm | 2 | 0 | under 1 s |
| sgate | 2 | 0 | under 1 s |
| altera_primitives | 2 | 0 | 1 s |
| altera_lnsim | 1 | 1 (the `.sv`) | under 1 s |
| cyclone10gx, twentynm, tennm | 2 each | 0 | under 1 s each |
| altera_ver (`altera_primitives.v`), sgate_ver | 1 each | 0 | under 1 s |
| lpm_ver (`220model.v`), altera_mf_ver (`altera_mf.v`) | 0 | 1 each | under 1 s |

The default set takes 3 to 4 seconds in all (sh and bat give the same table); everything listed 3 to 4 seconds as well.

### What fails and why

- `altera_lnsim.sv`: `function time get_time_value;` (a function with return type `time`) does not parse in the fork's SystemVerilog front end; the file is a single
  unit, so the whole `.sv` fails (20 errors, all follow from that line). The VHDL component package of `altera_lnsim` compiles. IP generated by Quartus that
  instantiates `altera_lnsim` modules cannot be simulated until that construct is supported.
- `220model.v` and `altera_mf.v`: a call of a function of another module through a hierarchical name (`eva.GET_PARAMETER_VALUE(...)`, `dev.FEATURE_FAMILY_...(...)`)
  stops the analysis with `cannot handle V_METHOD_CALL in vlog_check_expr`. Use the VHDL libraries `lpm` and `altera_mf` instead.
- The VHDL `altsyncram` of Quartus Pro 24.2 supports only Arria 10 and Cyclone 10 GX (`intended_device_family => "Arria 10"`): for other families it reports an error
  assertion at time 0 and asks for `altera_syncram` (a SystemVerilog model, which falls under the `altera_lnsim` limitation).
- Only "compiles and the basic megafunctions behave" is checked: `lcell`, `lpm_add_sub`, `lpm_counter` and `altsyncram` in `examples/altera_mf` (`run.sh --batch` prints `sum=61 cnt=3 ram=90` and `PASS`; without `--batch` it opens the GUI with `sim.do`).
  The compiled libraries must be rebuilt after a change of the `nvc` library format.
- `scripts/libs/test_libs.sh` (ctest `scripts_libs_compile`) also runs a made-up Quartus tree (no Intel sources): default set, a design using the libraries, an encrypted `.sv` that is skipped, and the missing-folder error.

## Compile libraries from the GUI

`lvdsim-gui`: **Tools > Compile libraries...** opens a (modeless) dialog that runs the script of this platform (`compile_libs.bat` through `cmd.exe /d /s /c` on Windows,
`compile_libs.sh` through `sh` on Linux) as a child process:

- **Vendor**: Xilinx Vivado or Altera Quartus. Each vendor keeps its own installation folder and library choice.
- **Installation folder**: an editable list. It offers `XILINX_VIVADO`, or `QUARTUS_ROOTDIR` and `ALTERA_ROOT`, and the version folders found one or two levels below
  `C:/Xilinx`, `D:/Xilinx`, `D:/cad/Xilinx`, `C:/intelFPGA_pro`, `C:/intelFPGA`, `D:/cad/intelFPGA_pro` and similar (on Linux `/opt`, `/tools`, `~`), newest first. Browse picks any folder;
  Start checks that it contains `data/vhdl/src/unisims` or `eda/sim_lib`.
- **Output folder**: default `<user data>/lvdsim/libraries` (on Windows `%LOCALAPPDATA%\lvdsim\libraries`), remembered. Both vendors can share it.
- **Libraries**: check boxes with the defaults of the scripts.
- **Start** runs the script with the lvdsim tools directory first on `PATH` (the program directory; a reduced `PATH` gets `System32` appended) and the nvc location the GUI found
  (`LVDSIM_NVC*` only if you did not set them). The window never blocks; the log shows the script's output live (errors on stderr in red), a progress bar counts finished libraries and
  the line above it names the library being compiled.
- **Cancel run** (and Close while running) kills the whole process tree: `taskkill /PID <pid> /T /F` on Windows, `SIGTERM` to the process group (`setsid`) on Linux, with a hard kill after 5 s. Nothing is
  ever stopped by image name.
- The last line reads the script's table: `4 libraries (4 ok, 0 with failed files): 8 files ok, 0 failed, 0 skipped; exit code 0.` The exit code and everything the script printed stay in the log.
- **Use these libraries in simulations** (checked by default): when the script ended with libraries compiled, the output folder is added to the **library folders**.

Library folders are kept in the GUI settings (`libraries/folders`). Every simulation started from the window gets `-L <name>` for each library (a directory holding `_NVC_LIB`) found directly in one of them, and the
library is mapped to its path without a `lvdsim.ini` entry (an entry of your own `lvdsim.ini` wins; a `-L` or `-work` of your own for the same name is not repeated). The Library dock lists these libraries (right-click a unit and choose Simulate to start it; double-click only expands), and the
dialog shows the folders with a Remove button. So after compiling, `vsim work.top` started from the GUI sees `altera_mf`, `lpm`, `unisim` and the others at once; designs still have to be analysed with `vcom -L ...` as usual.

If the script is not found (the GUI looks in `<program dir>/scripts/libs`, `<program dir>/../scripts/libs` and then upwards through the source tree) the dialog says so and Start stays disabled.
`lvdsim-gui --show-dialog compile-libs [--dialog-vendor altera] [--dialog-run] --screenshot out.png` renders the dialog headless; `--open-menu Tools` adds the open menu to a window screenshot.
Pure parts (detection, argument list, script location, output parsing) are in `tools/src/vendor_libs.*` with Catch2 tests; the dialog has a QTest with a fake script.

## OSVVM and UVVM

Deferred. They are not part of this script and no flow for them exists yet (planned, not implemented).
