# SDF back-annotation

Timing from a Standard Delay Format file (IEEE 1497, `.sdf` or `.sdo`) is applied when the design is elaborated. It needs the fork change
sets 37 and 38 (`kernel/upstream/37-verilog-timing-checks`, `38-sdf-annotation`, `kernel/PATCHES.md`); upstream NVC parses SDF files and
ignores them ("SDF is not yet supported").

## Use

With the lvdsim command-set tools:

    vsim -c -sdfmax /tb/dut=netlist.sdf -do "run -all; quit" work.tb
    vsim -c -sdfmin dut=netlist.sdf work.tb
    vsim -c -sdftyp netlist.sdf work.tb          # cell instances relative to the top level

`-sdfmin`, `-sdftyp` and `-sdfmax` choose the value of each `min:typ:max` triple; the option can be given several times (one file per
region). They become `nvc -e --sdf=min=[INSTANCE=]FILE` (`typ=`, `max=`), which can also be used directly:

    nvc -e --sdf=max=dut=netlist.sdf tb -r

The instance is a path of instance labels with `/` or `.` separators; a leading separator and the name of the top level may be written
(`/tb/dut`, `tb.dut` and `dut` are the same). The `INSTANCE` names of the file are relative to it. Names are compared without regard to case
and without the SDF escape character (`\[` is `[`). `(INSTANCE *)` cells apply to every instance of that cell type in the region. `INTERCONNECT`
and `PORT` delays are applied to the input port of the destination instance.

## What is annotated

| SDF construct | VHDL (VITAL models, IEEE 1076.4 names) | Verilog |
|---------------|----------------------------------------|---------|
| `IOPATH in out` | `tpd_in_out` (edge of `in` not part of the name) | not annotated, warning (module path delays are not implemented) |
| `PORT p`, `INTERCONNECT src dst` | `tipd_p` of the destination instance | not annotated, warning |
| `SETUP d r`, `HOLD d r` | `tsetup_d_r[_edge_edge]`, `thold_d_r[_edge_edge]` | limit of `$setup`/`$hold` (also the ones of `$setuphold`) whose events name the two ports |
| `SETUPHOLD d r s h` | `tsetup_...` and `thold_...` | both limits |
| `RECOVERY`, `REMOVAL`, `RECREM` | `trecovery_...`, `tremoval_...` | limits of `$recovery`, `$removal` (also of `$recrem`) |
| `WIDTH p` | `tpw_p_posedge`/`tpw_p_negedge` (both when the file gives no edge) | not annotated (`$width` is not checked) |
| `PERIOD p` | `tperiod_p[_edge]` | not annotated (`$period` is not checked) |
| `min:typ:max` | selected value | selected value |
| `TIMESCALE` | yes | yes |
| `DEVICE`, `NETDELAY`, `PATHPULSE`, `PATHPULSEPERCENT`, `LABEL`, `TIMINGENV` | parsed, not annotated | parsed, not annotated |
| `INCREMENT` | warning, not annotated | warning, not annotated |
| `COND`, `CONDELSE`, `SCOND`, `CCOND`, bit selects of bus ports | warning, not annotated | warning, not annotated |

Edge names in VITAL generics are `noedge`, `posedge` and `negedge`: `(SETUP d (posedge clk) (1.5))` sets `tsetup_d_clk_noedge_posedge`, or
`tsetup_d_clk` if the model has no generic with the edges. Generic types: a subtype of `TIME` (`VitalDelayType`) and the constrained arrays
`VitalDelayType01`, `VitalDelayType01Z`, `VitalDelayType01ZX`. The SDF values are expanded to the transitions as IEEE 1497 and 1076.4 say:
one value for all, rise and fall, rise, fall and turn-off, six or twelve values; the transitions to and from `X` are the minimum or maximum of
their neighbours. Arrays of these types (`VitalDelayArrayType01`, for bus ports) and the generics of the top-level design unit are not
annotated.

Verilog instances that the file annotates get their own copy of the module (like an instance with `defparam`), so the other instances keep
the limits of the source. Since change set 37 timing check limits are in units of the module's `timescale`, `$setuphold` and `$recrem` are
checked as two checks each, and `posedge`/`negedge` events are only transitions of that kind.

## Messages

The elaboration warns when an SDF entry matches no VITAL generic or Verilog timing check of its instance ("SDF: IOPATH does not match a
generic of U1 (expected TPD_A_Z)"), when a construct is not annotated (the rows above), and when a cell's instance is not in the design or
not in the region ("SDF: instance r1 was not found in the design").

## Tests

Kernel regress tests `sdf1` (VITAL inverters and a flip-flop: minimum, typical and maximum values, regions, wildcard cells, interconnect,
setup/width/period violations) and `vsdf1` (Verilog flip-flops with `$setuphold`, one instance annotated), unit tests `test_values1` and
`test_values2` (suite `sdf`), and `vsetuphold1` for the Verilog timing checks. `tools_tests`: "vsim SDF options become nvc elaboration
options". No netlist written by a vendor tool was available for testing (none on this machine, and none was generated).

## Not supported yet

- Verilog module path delays (`specify` paths are parsed and dropped), so `IOPATH` of Verilog cells (Xilinx `simprims`, ASIC libraries) has
  no effect; the next step.
- `$width`, `$period`, `$skew`, `$nochange` in Verilog; negative `$setuphold` limits with delayed signals.
- `INCREMENT`, conditional entries, bit-selected bus ports, `DEVICE`, `NETDELAY`, pulse limits.
- Instances inside generate blocks: the kernel names them `gen(1).u` while tools write `gen\[1\].u` in SDF files; they do not match (escaped
  names that contain brackets, as in flattened netlists, do).
