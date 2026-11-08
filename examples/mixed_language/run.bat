@echo off
rem Compile the mixed-language example, then open it in the lvdsim GUI with sim.do (live waveform).
rem   run.bat            compile, then start the GUI; close the window to end the run
rem   run.bat --batch    compile and simulate 2 us without a window, print the final values, write mixed.fst
rem   run.bat --help
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
vlib work || exit /b 1
vlog -work work src\avg_filter.v || exit /b 1
vlog -sv -work work src\level_monitor.sv || exit /b 1
vcom -2008 -work work src\adc_model.vhd src\tb_mixed.vhd || exit /b 1
if defined BATCH goto batch
echo Starting the GUI with sim.do; close the window to end the run.
start "" /wait lvdsim-gui %LVDSIM_GUI_ARGS% -do sim.do work.tb_mixed
exit /b %errorlevel%
:batch
vsim -c -do "run 2 us; puts [list events [examine /events] avg [examine /avg]]; quit" -lvdwave mixed.fst work.tb_mixed
exit /b %errorlevel%
:help
echo Compile the mixed-language example, then open it in the lvdsim GUI with sim.do ^(live waveform^).
echo   run.bat            compile, then start the GUI; close the window to end the run
echo   run.bat --batch    compile and simulate 2 us without a window, print the final values, write mixed.fst
echo Needs vlib vlog vcom and lvdsim-gui on PATH ^(examples\README.md section 1^). LVDSIM_GUI_ARGS adds GUI options.
exit /b 0
