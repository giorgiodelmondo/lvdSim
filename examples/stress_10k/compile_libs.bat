@echo off
rem Compile the Xilinx VHDL libraries this example needs into .\unisim and .\unimacro (see compile_libs.sh).
rem   set XILINX_VIVADO=C:\Xilinx\Vivado\2022.2
rem   compile_libs.bat            or   compile_libs.bat C:\Xilinx\Vivado\2022.2
cd /d "%~dp0"
if "%LVDSIM_NVC_OPTS%"=="" set "LVDSIM_NVC_OPTS=-M 128m"
set "ROOT=%~1"
if "%ROOT%"=="" set "ROOT=%XILINX_VIVADO%"
set "U=%ROOT%\data\vhdl\src\unisims"
set "M=%ROOT%\data\vhdl\src\unimacro"
if not exist "%U%\unisim_VCOMP.vhd" (
  echo Vivado sources not found under "%ROOT%". Set XILINX_VIVADO or pass the installation folder. 1>&2
  exit /b 1
)
vlib unisim || exit /b 1
vcom -2008 -explicit -work unisim "%U%\unisim_VPKG.vhd" || exit /b 1
vcom -2008 -explicit -work unisim "%U%\unisim_VCOMP.vhd" || exit /b 1
if exist "%U%\unisim_retarget_VCOMP.vhd" vcom -2008 -explicit -work unisim "%U%\unisim_retarget_VCOMP.vhd" || exit /b 1
for %%f in ("%U%\primitive\*.vhd") do vcom -2008 -explicit -work unisim "%%f" || exit /b 1
if exist "%U%\retarget\" for %%f in ("%U%\retarget\*.vhd") do vcom -2008 -explicit -work unisim "%%f" || exit /b 1
vlib unimacro || exit /b 1
vcom -2008 -explicit -work unimacro -L unisim "%M%\unimacro_VCOMP.vhd" || exit /b 1
for %%f in ("%M%\*_MACRO.vhd") do vcom -2008 -explicit -work unimacro -L unisim "%%f" || exit /b 1
echo libraries ready in %cd%
