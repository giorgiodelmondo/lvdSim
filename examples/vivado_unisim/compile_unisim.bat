@echo off
rem Compile the Xilinx VHDL unisim library from a Vivado installation into .\unisim.
rem The Xilinx sources are not part of lvdsim (Xilinx licence): point the script at your own copy.
rem   compile_unisim.bat                       uses %XILINX_VIVADO%\data\vhdl\src\unisims
rem   compile_unisim.bat C:\path\to\unisims    a folder with unisim_VCOMP.vhd and unisim_VPKG.vhd
cd /d "%~dp0"
rem Xilinx unisim_VCOMP is a very large package: give the compiler a bigger heap.
if "%LVDSIM_NVC_OPTS%"=="" set "LVDSIM_NVC_OPTS=-M 128m"
set "SRC=%~1"
if "%SRC%"=="" set "SRC=%XILINX_VIVADO%\data\vhdl\src\unisims"
if not exist "%SRC%\unisim_VCOMP.vhd" (
  echo unisim_VCOMP.vhd not found in "%SRC%". Set XILINX_VIVADO or pass the unisims folder. 1>&2
  exit /b 1
)
vlib unisim || exit /b 1
vcom -2008 -explicit -work unisim "%SRC%\unisim_VPKG.vhd" || exit /b 1
vcom -2008 -explicit -work unisim "%SRC%\unisim_VCOMP.vhd" || exit /b 1
if exist "%SRC%\primitive\" (
  for %%f in ("%SRC%\primitive\*.vhd") do vcom -2008 -explicit -work unisim "%%f" || exit /b 1
) else (
  if exist "%SRC%\unisim_SMODEL.vhd" vcom -2008 -explicit -work unisim "%SRC%\unisim_SMODEL.vhd"
  vcom -2008 -explicit -work unisim "%SRC%\unisim_VITAL.vhd" || exit /b 1
)
echo unisim library ready in %cd%\unisim
