# Verilog and XPM coverage of the lvdsim kernel

Evaluation of the Xilinx Verilog simulation models and XPM with the lvdsim fork of NVC (1.24-devel, evaluated on the former base commit 386a23083; the kernel is now the `lvdsim` fork branch on 4e41ba459
plus the changes of `kernel/PATCHES.md`; numbers such as 0007 name the retired patches, mapped there under "Old patch numbers"). Run on Windows 11 with Vivado 2022.2 (`data/verilog/src/unisims`, `data/verilog/src/glbl.v`,
`data/ip/xpm`). Nothing from Xilinx is copied into this repository; every test compiled into a temporary directory.

## Method

Per model (428 files in `unisims/`, one module each):

1. **analyse**: `nvc -a FILE.v` into one library together with `glbl.v`.
2. **elaborate**: `nvc -e MODEL glbl`, the model as top level next to `glbl` (a second top level, as `vsim work.top work.glbl`).
3. **run**: `nvc -r MODEL --stop-time=1us` with every input left unconnected.

"ok" therefore means the model is accepted and does not stop the simulator; it says nothing about behaviour. Behaviour was checked by hand
only for the primitives in the second table and for the XPM blocks.

## Result for the 428 unisim models

Updated 2026-11-14 with the kernel change sets 23 to 29 (`kernel/PATCHES.md`): all 428 models analyse, 318 analyse, elaborate and run (section
"Change sets 23 to 29"). Updated earlier the same day with the change sets 15 to 22: 427 models analyse, 316 analyse, elaborate and run. The tables below
are the state before them; "Change sets 15 to 22" further down lists what changed.

| Result | Models | Reason |
|--------|-------:|--------|
| ok (analyse, elaborate, run 1 us) | 303 | LUT1 to LUT6 and LUT6_2, FDRE/FDCE/FDPE/FDSE, LDCE/LDPE, MUXF7/8/9, MUXCY/XORCY, CARRY4/8, all BUFG variants, IBUF/OBUF/IOBUF families, IDDR/ODDR, ISERDES(E1), OSERDES, IDELAY/ODELAY, SRL16E/SRLC16E/SRLC32E, RAM32/64/128/256/512 distributed RAMs, RAMB18/36 (E1, E2, E5), FIFO18/36 (E1, E2), URAM288, DSP48E2 and the DSP58/DSP_* family, MMCM and PLL (see below), XADC, STARTUPE2, ... |
| does not analyse | 10 | see next table |
| does not elaborate: needs `SIP_*` | 74 | The model is a wrapper around an IEEE 1735 encrypted `SIP_*` module (gigabit transceivers, PCIe, hard IP, clocking and I/O blocks of UltraScale+ and Versal). Those are in Vivado's secureip folder and encrypted; encrypted IP is out of scope |
| does not elaborate: needs `B_*` or `BM_*` | 35 | Same, secureip wrappers (ISERDESE2, OSERDESE2, GTHE2/GTPE2/GTXE2, PCIE_2_1/3_0, NoC blocks, DDRMC) |
| does not elaborate: `defparam` | 2 | BUFHCE, OSERDESE1: "this Verilog statement is not currently supported" (`defparam inst.PARAM = ...`) |
| does not elaborate: needs an unit that does not analyse | 2 | ICAPE2, ICAPE3 need SIM_CONFIGE2/3 |
| fails when run | 2 | DSPFP32: `$realtobits` is not registered; STARTUPE3: `V_HIER_REF in vlog_get_dim` (hierarchical reference as array dimension) |

Models that do not analyse:

| Model | Message |
|-------|---------|
| DCIRESET | `unexpected ;` while parsing a primary |
| DCM_ADV, DCM_SP | `min:typ:max` triple in a `$setuphold` timing check |
| DSP48E1, SIM_CONFIGE2, SIM_CONFIGE3 | an array of gate instances (`buf b[3:0] (...)`) |
| FRAME_ECCE3 | an array of pull gate instances |
| GTXE2_CHANNEL | a range in a specify terminal descriptor |
| SYSMONE1, SYSMONE4 | `unexpected )` while parsing an expression |

## Change sets 15 to 22 (2026-11-14)

Same method, Vivado 2022.2, kernel `kernel/nvc/build-k7` (fork working tree with change sets 04 to 22), scripts `D:/tmp/lvdsim-s7/scan_unisim.sh` and
`elab_unisim.sh`:

| Result | Before | After |
|--------|-------:|------:|
| analyse | 418 | 427 |
| ok (analyse, elaborate with `glbl`, run 1 us) | 303 | 316 |
| does not elaborate: needs an encrypted `SIP_*`, `B_*` or `BM_*` module | 109 | 110 (`GTXE2_CHANNEL` now analyses and needs `B_GTXE2_CHANNEL`) |
| does not analyse | 10 | 1: `DCIRESET` (`assign #(100000, 0) LOCKED = ...`: rise and fall delays on a continuous assignment) |
| fails when run | 2 | 1: `DCM_SP` (`illegal negative delay -1 fs`: `assign #(period_50)` with `period_50` still `x`; the standard treats an unknown delay as zero) |

Newly working: `DCM_ADV` (min:typ:max timing check limits, change set 15), `SYSMONE1`, `SYSMONE4` (empty last argument of `$display`, 16), `DSP48E1`,
`FRAME_ECCE3`, `SIM_CONFIGE2`, `SIM_CONFIGE3`, `ICAPE2`, `ICAPE3` (arrays of gate instances, 17), `STARTUPE3` (bit select of a hierarchical name, 18),
`BUFHCE`, `OSERDESE1` (`defparam`, 19), `DSPFP32` (`$realtobits`, 21). `DCM_SP` and `GTXE2_CHANNEL` now analyse (15).

XPM: `xpm_memory_sdpram` with `MEMORY_PRIMITIVE("block")` writes and reads back eight words, `xpm_fifo_sync` with `FIFO_MEMORY_TYPE("block")` passes
eight words in order (references into generate blocks, 18; `force`/`release` of `gen_rd_b.doutb_reg` also needs change set 02, which the fork has).
Both failed before with `cannot find 'gen_rd_b' in hierarchical identifier`.

## Change sets 23 to 29 (2026-11-14, later)

Same method, kernel `kernel/nvc/build-k8` (fork working tree with change sets 04 to 29), scripts `D:/tmp/lvdsim-s8/scan_unisim.sh` and
`elab_unisim.sh`, results in `D:/tmp/lvdsim-s8/scan-before` and `scan-final`:

| Result | Before | After |
|--------|-------:|------:|
| analyse | 427 | 428 |
| ok (analyse, elaborate with `glbl`, run 1 us) | 316 | 318 |
| does not elaborate: needs an encrypted `SIP_*`, `B_*` or `BM_*` module | 110 | 110 |
| does not analyse | 1 (`DCIRESET`) | 0 |
| fails when run | 1 (`DCM_SP`) | 0 |

`DCIRESET` (`assign #(100000, 0) LOCKED = RST ? 1'b0 : 1'b1;`) needs rise and fall delays on a continuous assignment (change set 24); `DCM_SP`
(`assign #(period_50) ...` while `period_50` is still `x`) needs an unknown delay to count as zero (25). Neither model's behaviour was checked
beyond the 1 us run with open inputs.

## The primitives asked for

Tested with `glbl` as second top level, once from a Verilog testbench (`examples/xpm_unisim/src/tb_unisim.v`, self checking) and once from
a VHDL design (`examples/xpm_unisim/src/xpm_demo.vhd` and scratch designs).

| Primitive | Verilog top | VHDL top | Notes |
|-----------|-------------|----------|-------|
| FDRE | works | works | Q follows D after one clock once `glbl.GSR` is released (100 ns); `INIT` is an integer generic in VHDL. Needed patches 0007 (GSR delay was 100 ps), 0012 (output half a clock late) |
| LUT6 | works | works with a wrapper | the 64 bit `INIT` cannot be an integer generic: `lut6_and.v` sets it. LUT1 to LUT5 take the generic directly |
| BUFG | works | works | |
| IBUF | works | works | |
| SRL16E | works | works | output delayed by the address (checked: four clocks for address 3) |
| RAMB18E1 | works | works | write on port B, read back on port A (`7SERIES`, 18 bit). A read of the address that is written on the other port prints the model's collision message |
| MMCME2_ADV / MMCME2_BASE | partial | not tried | analyses, elaborates, `LOCKED` rises after 1.3 us for a 100 MHz input, but the outputs are not right: with CLKFBOUT_MULT_F=8 and CLKOUT1_DIVIDE=16 (expected 20 ns period) CLKOUT1 gave 11 edges in 855 ns after lock with one spacing of 200 ns, and the model warns that the measured CLKIN1 period differs from the attribute. Needed `$realtime` (0007) and the sensitivity fix (0010). Not usable for timing yet; use the VHDL unisim MMCME2_ADV (`examples/vivado_unisim`) |

All other primitives of the first table were only smoke tested (no inputs driven).

## XPM

`xpm_cdc.sv`, `xpm_fifo.sv` and `xpm_memory.sv` compile with `vlog -sv`; `xpm_VCOMP.vhd` (component package) compiles with `vcom -2008`.
All instances below were run from a VHDL top with `glbl` as second top level.

| Block | Result |
|-------|--------|
| xpm_cdc_single | works (4 stage synchroniser, `SRC_INPUT_REG`) |
| xpm_cdc_gray | works |
| xpm_cdc_array_single | works |
| xpm_cdc_pulse | works |
| xpm_cdc_handshake | works (external handshake) |
| xpm_cdc_async_rst, xpm_cdc_sync_rst | work (elaborate and run, reset passes through) |
| xpm_fifo_sync, `FIFO_MEMORY_TYPE` "distributed", std and fwft read mode | works: eight words written and read back in order, empty and data_valid as expected |
| xpm_fifo_sync, "block" or "auto" | failed to elaborate before change set 18 (`cannot find 'gen_rd_b' in hierarchical identifier`); "block" works since (eight words in order) |
| xpm_memory_sdpram (block) | same; works since change set 18 (write and read back) |
| xpm_fifo_async, xpm_fifo_axis and other FIFOs, xpm_memory_tdpram/spram/sprom/dprom | compile; not run |

## What had to change in the fork

| Problem seen | Patch |
|--------------|-------|
| `cannot find 'glbl' in hierarchical identifier`: NVC already elaborates `nvc -e top glbl`, but running the result (`-r` after `-e`) crashed on the first reference to the second top level | 0006 |
| `#(ROC_WIDTH)` in glbl.v was taken as femtoseconds (GSR released after 100 ps instead of 100 ns); `$time` returned femtoseconds; `$realtime` missing | 0007 |
| VHDL component bound to a Verilog module in another library: `is not a VHDL design unit`; Verilog children looked up in the wrong library | 0008 |
| VHDL string generics (`FIFO_MEMORY_TYPE => "distributed"`) rejected; unmapped generics with a default crashed | 0009 |
| `always @(*)` with a procedural `assign` (xpm_memory) or a real constant (MMCM) stopped with a fatal error | 0010 |
| `assign glbl.PLL_LOCKG = ...` and `force gen_rd_b.x = ...`: "target cannot be driven by continuous assignment" | 0011 |
| every unisim flip-flop updated half a clock late (`assign Q_out = INIT` / `deassign Q_out` on GSR left a second source on the nexus and the non-blocking update waited for the next event) | 0012 |
| a Verilog module instantiating a unisim model from another library (`-L`) | 0013 |

lvdsim side: `vsim` accepts several design units (`tools/src/translate.cpp`, `docs/command-compat.md`).

## Change sets 30 to 39 (2026-11-14, night)

Kernel `kernel/nvc/build-k9` (fork working tree with change sets 04 to 39), scripts `D:/tmp/lvdsim-s9/scans.sh` (the scripts of
`D:/tmp/lvdsim-s8`), before = the build of the end of the previous session (copy `D:/tmp/lvdsim-s9/k-before`), results in
`D:/tmp/lvdsim-s9/scan-*`, `alt-*`, `smoke-*`. The unisim counts do not change (428 analyse, 318 run 1 us); `altera_mf.v` goes from 108 to
113 of 113 models (`$stop` of `altddio_bidir`, `altddio_in`, `altddio_out`, `altpll`, change set 30; `$isunknown` of
`altera_syncram_derived_forwarding_logic`, 31).

New Verilog in the kernel:

| Construct | Change set | Before |
|-----------|------------|--------|
| `$stop` (batch: ends the run; `nvc -i`: waits at the prompt, 36) | 30, 36 | fatal "system task $stop not registered" |
| `$isunknown` | 31 | fatal "system task $isunknown not registered" |
| `always @(net)` sees the value a continuous assignment, gate, port or `tri0`/`tri1` pull gives a net at time zero | 32 | never ran |
| input port with a driver inside the module (`assign (pull0, pull1) a = 1;`) connected to a variable | 33 | changed the variable; two instances: fatal `I_TYPE` |
| input port connected to a variable with a declaration initialiser: the port net changes at time zero | 33 | no event |
| task with `output`/`inout` arguments and a delay | 34 | access violation (Windows) |
| `$setuphold`, `$recrem` | 37 | parsed and ignored |
| `posedge`/`negedge` of timing check events | 37 | every change was an event |
| timing check limits in units of the `timescale` | 37 | taken as femtoseconds |
| SDF back-annotation of timing check limits (`docs/sdf.md`) | 38 | `--sdf` warned "not yet supported" |

The Verilog `altsyncram` of `altera_mf.v` (which goes to `altera_syncram_derived`) now writes and reads back: `D:/tmp/lvdsim-s9/smoke-src/tb_alt_mem.v`
passes (eight words, `lpm_ram_dp` and `altsyncram`). The causes were in the kernel: the model copies its inputs in `always @(clocken0)
clocken0_dly <= clocken0;` blocks, and the open `tri1 clocken0` got its value during initialisation without an event, so the clock enable stayed
`x` (32); the address input was collapsed onto the testbench's `reg [3:0] wa = 0`, which never changes at time zero, so word 0 was never
written (33).

## Limits found, not fixed

- A Verilog parameter is a VHDL integer (32 bit) or string generic. Parameters wider than 32 bits (`INIT` of LUT6, RAMB `INIT_xx`) cannot be
  set from VHDL; wrap the primitive in Verilog.
- Hierarchical references into generate blocks (`gen_rd_b.clkb_int`), `defparam`, arrays of gate instances, `min:typ:max` in timing checks and
  `$realtobits` were missing; change sets 15 to 21 add them (2026-11-14); rise/fall delays on continuous assignments and unknown delays
  treated as zero followed (24, 25). Still missing: `%m` in `$display` ("unknown format specifier"), delays on gate instances and net
  declarations (parsed and ignored), module path delays of `specify` blocks (parsed and dropped, so SDF `IOPATH` has no effect on Verilog
  cells), `$width`, `$period`, `$skew`, `$nochange`.
- An ANSI input port without a separate net declaration (`module m(input a); assign a = ...;`) cannot be driven inside the module
  ("'a' cannot be driven by continuous assignment"); declare it as `input a; wire a;`.
  The `%m` messages are noise in the unisim models and do not stop a run.
- `$time` is rounded to the nearest unit of the calling module, ties round up (6.5 ns under a 1 ns unit gives 7), both in the fork and in the upstream branch `pr/verilog-timescale`; `$realtime` keeps the fraction. This matches the behaviour of the reference simulator xsim 2022.2 (measured: under `1ns/1ps` 0.4 ns gives 0, 0.5 gives 1, 16.4 gives 16, 16.5 gives 17, 16.6 gives 17, 17.5 gives 18; under `10ns/1ns` 1.5 units gives 2, 1.6 gives 2, 2.4 gives 2, 4.1 gives 4, 5.6 gives 6). The wording of the standard was not checked. Before 2026-11-07 the fork truncated.
- The shell cannot see the signals of the second top level (`/glbl/GSR`).
- The shell command `exit` (and so `quit`) does not return for some mixed designs: a VHDL signal vector connected to a Verilog `output [n:0]`
  net. Minimal case: `module m1(input a, output y, output [7:0] k); assign y = ~a; endmodule` instantiated from VHDL with `k` connected,
  then `nvc -e t --do exit`. It also happens with the build before patches 0006 to 0013 (checked with a build of the base plus 0001 to 0005), so
  it is an upstream problem. A script that ends without `exit` finishes normally, which is what `examples/xpm_unisim` does. The GUI should stop
  such a simulator process instead of waiting for `exit`.
- With several top levels only the first is the root of the shell's signal paths.

## SystemVerilog

Probe files (2026-11-06): `always_ff`, `always_comb`, `logic`, typedef enum, packed struct, generate loops, tasks and programs work;
classes, interfaces, packages with `import`, queues and associative arrays, `fork`/`join`, concurrent assertions, covergroups and DPI do not.
The XPM sources use only the first group (plus `assert property` under a parameter, which is not elaborated). The scope is: SystemVerilog is supported as far as vendor models need it, VHDL is the testbench language.

## Regression checks run

- `kernel/test/regress`: the new tests `vtops1`, `vtimescale1`, `mixed_str1`, `sens_assign1`, `href_lval1`, `nba_deassign1`, plus 248 of the existing
  `verilog` and `mixed` tests (four more need `--seed` or a `.sv` file and were run by hand) and 750 plain VHDL tests, run with a separate `-e` and `-r` (the reheat path); see `kernel/PATCHES.md`.
- `tools_tests` and `tools_integration_tests` (new: several top levels with `glbl`, VHDL component bound to a Verilog module in another library
  with string generics, Verilog design using `-L`).
