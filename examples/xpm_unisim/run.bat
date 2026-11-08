@echo off
rem Compile the Xilinx XPM and Verilog unisim example, then open it in the lvdsim GUI with sim.do (live waveform).
rem   run.bat            compile, then start the GUI; close the window to end the run
rem   run.bat --batch    compile, then run both testbenches without a window (PASS lines)
rem   run.bat --help
rem Run compile_libs.bat once first; XILINX_VIVADO must name your Vivado installation (glbl.v).
rem The GUI shows the VHDL design (work.xpm_demo with work.glbl as second top level).
rem Needs vlib vlog vcom (and lvdsim-gui) on PATH: see ..\README.md section 1. LVDSIM_GUI_ARGS adds GUI options.
set "BATCH="
if "%~1"=="--help" goto help
if "%~1"=="-h" goto help
if "%~1"=="/?" goto help
if /i "%~1"=="--batch" set "BATCH=1"
if /i "%~1"=="/batch" set "BATCH=1"
if not "%~1"=="" if not defined BATCH (echo Unknown argument "%~1" ^(try --help^). 1>&2 & exit /b 2)
cd /d "%~dp0"
for %%T in (vlib vlog vcom) do where %%T >nul 2>nul || (echo Cannot find %%T on PATH. Set up the shell as in examples\README.md section 1, or put the lvdsim folder on PATH. 1>&2 & exit /b 1)
if defined BATCH (where vsim >nul 2>nul || (echo Cannot find vsim on PATH ^(see examples\README.md section 1^). 1>&2 & exit /b 1))
if not defined BATCH (where lvdsim-gui >nul 2>nul || (echo Cannot find lvdsim-gui on PATH. Set up the shell as in examples\README.md section 1, or put the lvdsim folder on PATH. "run.bat --batch" needs only the command line tools. 1>&2 & exit /b 1))
if not exist xpm\ (echo Run compile_libs.bat first. 1>&2 & exit /b 1)
if not exist unisim_v\ (echo Run compile_libs.bat first. 1>&2 & exit /b 1)
if "%XILINX_VIVADO%"=="" (echo Set XILINX_VIVADO. 1>&2 & exit /b 1)
if "%LVDSIM_NVC_OPTS%"=="" set "LVDSIM_NVC_OPTS=-M 128m"
vlib work || exit /b 1
vlog -work work "%XILINX_VIVADO%\data\verilog\src\glbl.v" || exit /b 1
vlog -work work src\lut6_and.v src\tb_unisim.v || exit /b 1
vcom -2008 -work work -L xpm -L unisim_v src\xpm_demo.vhd || exit /b 1
if defined BATCH goto batch
echo Starting the GUI with sim.do; close the window to end the run.
start "" /wait lvdsim-gui %LVDSIM_GUI_ARGS% -L xpm -L unisim_v -do sim.do work.xpm_demo work.glbl
exit /b %errorlevel%
:batch
echo == VHDL design: IBUF, BUFG, FDRE, LUT6 and SRL16E from Verilog unisim, XPM CDC and FIFO
vsim -c -L xpm -L unisim_v -do "run -all" work.xpm_demo work.glbl || exit /b 1
echo == Verilog testbench: unisim primitives and RAMB18E1
vsim -c -L unisim_v -do "run -all" work.tb_unisim work.glbl
exit /b %errorlevel%
:help
echo Compile the Xilinx XPM and Verilog unisim example, then open it in the lvdsim GUI with sim.do ^(live waveform^).
echo   run.bat            compile, then start the GUI; close the window to end the run
echo   run.bat --batch    compile, then run both testbenches without a window ^(PASS lines^)
echo Run compile_libs.bat once first; XILINX_VIVADO must name your Vivado installation ^(glbl.v^).
echo The GUI shows the VHDL design ^(work.xpm_demo with work.glbl as second top level^).
echo Needs vlib vlog vcom and lvdsim-gui on PATH ^(examples\README.md section 1^). LVDSIM_GUI_ARGS adds GUI options.
exit /b 0
