@echo off
rem Compile the Altera megafunction demo, then open it in the lvdsim GUI with sim.do (live waveform).
rem   run.bat            compile, then start the GUI; close the window to end the run
rem   run.bat --batch    compile and run without a window; prints sum=61 cnt=3 ram=90 and PASS
rem   run.bat --help
rem Needs altera_mf and lpm: scripts\libs\compile_libs.bat --quartus DIR --out libs, then set LVDSIM_INI=libs\lvdsim.ini
rem Needs vlib vcom (and lvdsim-gui) on PATH: see ..\README.md section 1. LVDSIM_GUI_ARGS adds GUI options.
set "BATCH="
if "%~1"=="--help" goto help
if "%~1"=="-h" goto help
if "%~1"=="/?" goto help
if /i "%~1"=="--batch" set "BATCH=1"
if /i "%~1"=="/batch" set "BATCH=1"
if not "%~1"=="" if not defined BATCH (echo Unknown argument "%~1" ^(try --help^). 1>&2 & exit /b 2)
cd /d "%~dp0"
for %%T in (vlib vcom) do where %%T >nul 2>nul || (echo Cannot find %%T on PATH. Set up the shell as in examples\README.md section 1, or put the lvdsim folder on PATH. 1>&2 & exit /b 1)
if defined BATCH (where vsim >nul 2>nul || (echo Cannot find vsim on PATH ^(see examples\README.md section 1^). 1>&2 & exit /b 1))
if not defined BATCH (where lvdsim-gui >nul 2>nul || (echo Cannot find lvdsim-gui on PATH. Set up the shell as in examples\README.md section 1, or put the lvdsim folder on PATH. "run.bat --batch" needs only the command line tools. 1>&2 & exit /b 1))
if "%LVDSIM_INI%"=="" if not exist lvdsim.ini echo Note: LVDSIM_INI is not set; the libraries altera_mf and lpm must be mapped in a lvdsim.ini ^(see above^). 1>&2
vlib work || exit /b 1
vcom -2008 -explicit -L altera_mf -L lpm src\alt_demo.vhd || exit /b 1
if defined BATCH goto batch
echo Starting the GUI with sim.do; close the window to end the run.
start "" /wait lvdsim-gui %LVDSIM_GUI_ARGS% -L altera_mf -L lpm -do sim.do work.alt_demo
exit /b %errorlevel%
:batch
vsim -c -L altera_mf -L lpm -do "run -all" work.alt_demo
exit /b %errorlevel%
:help
echo Compile the Altera megafunction demo, then open it in the lvdsim GUI with sim.do ^(live waveform^).
echo   run.bat            compile, then start the GUI; close the window to end the run
echo   run.bat --batch    compile and run without a window; prints sum=61 cnt=3 ram=90 and PASS
echo Needs altera_mf and lpm: scripts\libs\compile_libs.bat --quartus DIR --out libs, then set LVDSIM_INI=libs\lvdsim.ini
echo Needs vlib vcom and lvdsim-gui on PATH ^(examples\README.md section 1^). LVDSIM_GUI_ARGS adds GUI options.
exit /b 0
