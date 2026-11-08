@echo off
rem Compile the stress design (about 10 000 signals), then open it in the lvdsim GUI with sim.do. Run compile_libs.bat once first.
rem   run.bat [N_LANES] [RUN_TIME]            GUI: default 118 lanes and a 200 us run, then the window stays open
rem   run.bat --batch [N_LANES] [RUN_TIME]    no window: default 118 lanes and 20 ms, prints the signal count and the checksum
rem   run.bat --help                          examples: run.bat 20 1ms     run.bat --batch 500 20ms
rem Set WAVE=1 with --batch to also write stress.fst (large). The GUI records its own waveform.
rem The full 118-lane design is heavy in the GUI (10 039 signals): keep GUI runs short, or lower N_LANES.
rem Needs vlib vlog vcom (and lvdsim-gui) on PATH: see ..\README.md section 1. LVDSIM_GUI_ARGS adds GUI options.
set "HERE=%~dp0"
set "BATCH="
set "LANES="
set "RTIME="
:args
if "%~1"=="" goto argsdone
if "%~1"=="--help" goto help
if "%~1"=="-h" goto help
if "%~1"=="/?" goto help
if /i "%~1"=="--batch" (set "BATCH=1" & shift & goto args)
if /i "%~1"=="/batch" (set "BATCH=1" & shift & goto args)
if "%LANES%"=="" (set "LANES=%~1" & shift & goto args)
if "%RTIME%"=="" (set "RTIME=%~1" & shift & goto args)
echo Too many arguments ^(try --help^). 1>&2
exit /b 2
:argsdone
cd /d "%HERE%"
for %%T in (vlib vlog vcom) do where %%T >nul 2>nul || (echo Cannot find %%T on PATH. Set up the shell as in examples\README.md section 1, or put the lvdsim folder on PATH. 1>&2 & exit /b 1)
if defined BATCH (where vsim >nul 2>nul || (echo Cannot find vsim on PATH ^(see examples\README.md section 1^). 1>&2 & exit /b 1))
if not defined BATCH (where lvdsim-gui >nul 2>nul || (echo Cannot find lvdsim-gui on PATH. Set up the shell as in examples\README.md section 1, or put the lvdsim folder on PATH. "run.bat --batch" needs only the command line tools. 1>&2 & exit /b 1))
if "%LVDSIM_NVC_OPTS%"=="" set "LVDSIM_NVC_OPTS=-M 128m"
if not exist unisim\ (echo Run compile_libs.bat first. 1>&2 & exit /b 1)
if "%LANES%"=="" set "LANES=118"
vlib work || exit /b 1
vlog -work work src\lane_acc.v || exit /b 1
vlog -sv -work work src\lane_mon.sv || exit /b 1
vcom -2008 -explicit -work work -L unisim -L unimacro src\lane_lfsr.vhd src\lane_cell.vhd src\stress_top.vhd || exit /b 1
if defined BATCH goto batch
rem sim.do runs 200 us unless LVDSIM_RUN names another time.
if not "%RTIME%"=="" set "LVDSIM_RUN=%RTIME%"
echo Starting the GUI with sim.do (%LANES% lanes); close the window to end the run.
if %LANES% GTR 20 echo Note: this design is heavy in the GUI; use "run.bat 20" for a lighter session or "run.bat --batch" for the full run.
start "" /wait lvdsim-gui %LVDSIM_GUI_ARGS% -L unisim -L unimacro -g N_LANES=%LANES% -do sim.do work.stress_top
exit /b %errorlevel%
:batch
if "%RTIME%"=="" set "RTIME=20 ms"
set "WAVEOPT="
if not "%WAVE%"=="" set "WAVEOPT=-lvdwave stress.fst"
vsim -c -L unisim -L unimacro -g N_LANES=%LANES% %WAVEOPT% -do "puts \"signals: [llength [find signals -r /*]]\"; run %RTIME%; puts [list checksum [examine /checksum] event_total [examine /event_total]]; quit" work.stress_top
exit /b %errorlevel%
:help
echo Compile the stress design (about 10 000 signals), then open it in the lvdsim GUI with sim.do. Run compile_libs.bat once first.
echo   run.bat [N_LANES] [RUN_TIME]            GUI: default 118 lanes and a 200 us run, then the window stays open
echo   run.bat --batch [N_LANES] [RUN_TIME]    no window: default 118 lanes and 20 ms, prints the signal count and the checksum
echo   examples: run.bat 20 1ms     run.bat --batch 500 20ms
echo Set WAVE=1 with --batch to also write stress.fst (large). The GUI records its own waveform.
echo The full 118-lane design is heavy in the GUI ^(10 039 signals^): keep GUI runs short, or lower N_LANES.
echo Needs vlib vlog vcom and lvdsim-gui on PATH ^(examples\README.md section 1^). LVDSIM_GUI_ARGS adds GUI options.
exit /b 0
