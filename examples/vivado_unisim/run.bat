@echo off
rem Compile the Xilinx unisim example, then open it in the lvdsim GUI with sim.do (live waveform).
rem   run.bat            compile, then start the GUI; close the window to end the run
rem   run.bat --batch    compile and simulate 2 us without a window; prints count and above, writes xilinx.fst
rem   run.bat --help
rem Run compile_unisim.bat once first.
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
if not exist unisim\ (echo Run compile_unisim.bat first. 1>&2 & exit /b 1)
vlib work || exit /b 1
vlog -work work src\edge_counter.v || exit /b 1
vcom -2008 -work work -L unisim src\comparator_sync.vhd src\tb_xilinx.vhd || exit /b 1
if defined BATCH goto batch
echo Starting the GUI with sim.do; close the window to end the run.
start "" /wait lvdsim-gui %LVDSIM_GUI_ARGS% -L unisim -do sim.do work.tb_xilinx
exit /b %errorlevel%
:batch
vsim -c -L unisim -do "run 2 us; puts [list count [examine /count] above [examine /above]]; quit" -lvdwave xilinx.fst work.tb_xilinx
exit /b %errorlevel%
:help
echo Compile the Xilinx unisim example, then open it in the lvdsim GUI with sim.do ^(live waveform^).
echo   run.bat            compile, then start the GUI; close the window to end the run
echo   run.bat --batch    compile and simulate 2 us without a window; prints count and above, writes xilinx.fst
echo Run compile_unisim.bat once first.
echo Needs vlib vlog vcom and lvdsim-gui on PATH ^(examples\README.md section 1^). LVDSIM_GUI_ARGS adds GUI options.
exit /b 0
