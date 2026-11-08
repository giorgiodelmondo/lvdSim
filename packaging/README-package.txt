lvdsim - open source HDL simulation front end (NVC kernel, familiar simulator commands)

Start:
    lvdsim-gui.exe                     the graphical simulator
    vlib work
    vcom -2008 design.vhd
    vsim -c -do "run -all; quit" top   command line batch flow (also vlog, vmap)

The package is self contained: nvc, its VHDL libraries and the Tcl command package are found relative to the
program directory (nvc\, tcl\compat\). No environment variables are needed. The variables LVDSIM_NVC,
LVDSIM_NVC_LIBDIR, LVDSIM_NVC_DLLDIR and LVDSIM_TCL_DIR still override the built in locations.

Licences are in the licenses directory (GPL-3.0 for lvdsim and NVC, Qt under LGPL-3.0 linked dynamically).

The installer offers to add this directory to PATH (checked by default) so that vlib, vmap, vcom, vlog, vsim and
lvdsim-cli can be typed in any new terminal. The nvc subfolder is never added to PATH.

Simulation libraries of FPGA vendors are not shipped. scripts\libs\compile_libs.bat builds the Xilinx (Vivado) and
Altera (Quartus) libraries from your own installation; lvdsim-gui offers it as Tools > Compile libraries and then
searches the compiled libraries in every simulation. See docs\libraries.md in the source tree.
