@echo off
setlocal enabledelayedexpansion
rem Compile the Xilinx Verilog simulation models and the XPM library into .\unisim_v and .\xpm.
rem The Xilinx sources are under Xilinx's licence and are not part of lvdsim: point the script at your Vivado.
rem   compile_libs.bat                       uses %XILINX_VIVADO%
rem   compile_libs.bat C:\path\to\Vivado\2022.2
rem Run it once. A few unisim models do not compile with NVC yet and are skipped (see docs\verilog-coverage.md).
cd /d "%~dp0"
if "%LVDSIM_NVC_OPTS%"=="" set "LVDSIM_NVC_OPTS=-M 128m"
set "ROOT=%~1"
if "%ROOT%"=="" set "ROOT=%XILINX_VIVADO%"
set "U=%ROOT%\data\verilog\src\unisims"
set "X=%ROOT%\data\ip\xpm"
if not exist "%U%\FDRE.v" goto missing
if not exist "%X%\xpm_cdc\hdl\xpm_cdc.sv" goto missing

vlib xpm || exit /b 1
vlog -sv -work xpm "%X%\xpm_cdc\hdl\xpm_cdc.sv" || exit /b 1
vlog -sv -work xpm "%X%\xpm_memory\hdl\xpm_memory.sv" || exit /b 1
vlog -sv -work xpm "%X%\xpm_fifo\hdl\xpm_fifo.sv" || exit /b 1
vcom -2008 -work xpm "%X%\xpm_VCOMP.vhd" || exit /b 1

vlib unisim_v || exit /b 1
set /a OK=0
set "SKIPPED="
for %%f in ("%U%\*.v") do (
  vlog -work unisim_v "%%f" > nul 2>&1 && (set /a OK+=1) || (set "SKIPPED=!SKIPPED! %%~nf")
)
echo unisim_v: %OK% models compiled, skipped:%SKIPPED%
echo libraries ready in %cd%
exit /b 0

:missing
echo Vivado sources not found under "%ROOT%" (data\verilog\src\unisims, data\ip\xpm). Set XILINX_VIVADO. 1>&2
exit /b 1
