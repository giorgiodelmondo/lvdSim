rem SPDX-License-Identifier: GPL-3.0-or-later
@echo off
setlocal enabledelayedexpansion
rem Compile the Vivado and Quartus (Altera/Intel) simulation libraries with the lvdsim tools into one output directory
rem and write a lvdsim.ini that maps them. Same options and behaviour as compile_libs.sh; see docs\libraries.md.
rem   compile_libs.bat [--out DIR] [--vivado DIR] [--quartus DIR] [--only LIST] [--quiet]
rem   --quartus: Quartus folder (default %QUARTUS_ROOTDIR%, then %ALTERA_ROOT%); the version folder or its quartus subfolder.
rem   --only: comma list of unisim, unimacro, unisim_verilog, xpm (Xilinx, default unisim,unimacro) and
rem           altera_mf, lpm, sgate, altera_primitives (Altera default set), altera_lnsim, altera_verilog,
rem           cyclone10gx, twentynm, tennm (device atoms, need altera_lnsim); secureip-skip is accepted.
rem           Without --only: the Xilinx default, or the Altera default when only --quartus is given.
rem Exit status is non-zero if unisim or unimacro fail, or on a usage or source error (2).
rem vlib, vmap, vcom, vlog must be on PATH. Files with a `protect block are skipped; a failing file never stops the run.

set "VIVADO=%XILINX_VIVADO%"
set "QUARTUS=%QUARTUS_ROOTDIR%"
if "%QUARTUS%"=="" set "QUARTUS=%ALTERA_ROOT%"
set "OUT="
set "ONLY="
set "GOTV=0"
set "GOTQ=0"
set "QUIET=0"
goto args
:moreonly
rem cmd turns an unquoted comma into an argument break: take further words that are not options
set "NXT=%~1"
if "%NXT%"=="" goto args
if "%NXT:~0,1%"=="-" goto args
set "ONLY=%ONLY%,%NXT%"
shift
goto moreonly
:args
if "%~1"=="" goto argsdone
if /i "%~1"=="--vivado" (set "VIVADO=%~2" & set "GOTV=1" & shift & shift & goto args)
if /i "%~1"=="--quartus" (set "QUARTUS=%~2" & set "GOTQ=1" & shift & shift & goto args)
if /i "%~1"=="--out" (set "OUT=%~2" & shift & shift & goto args)
if /i "%~1"=="--only" (set "ONLY=%~2" & shift & shift & goto moreonly)
if /i "%~1"=="--jobs" (echo note: --jobs ignored 1>&2 & shift & shift & goto args)
if /i "%~1"=="--quiet" (set "QUIET=1" & shift & goto args)
if /i "%~1"=="-h" goto usage
if /i "%~1"=="--help" goto usage
echo unknown option "%~1" 1>&2
goto usage
:usage
echo usage: compile_libs.bat [--out DIR] [--vivado DIR] [--quartus DIR] [--only LIST] [--quiet]
exit /b 2
:argsdone

if "%ONLY%"=="" (
  if "%GOTQ%"=="1" (
    if "%GOTV%"=="1" (set "ONLY=unisim,unimacro,altera_mf,lpm,sgate,altera_primitives") else (set "ONLY=altera_mf,lpm,sgate,altera_primitives")
  ) else (set "ONLY=unisim,unimacro")
)
if "%OUT%"=="" (
  set "OUT=.\altera_libs"
  echo ,%ONLY%, | findstr /i /c:",unisim" /c:",unimacro," /c:",xpm," >nul && set "OUT=.\vivado_libs"
)
set "WANT= "
set "ONLY2=%ONLY:,= %"
for %%w in (%ONLY2%) do (
  set "known="
  for %%k in (unisim unimacro unisim_verilog xpm altera_mf lpm sgate altera_primitives altera_lnsim altera_verilog cyclone10gx twentynm tennm secureip-skip) do if /i "%%w"=="%%k" set "known=1"
  if not defined known (echo unknown library "%%w" in --only 1>&2 & exit /b 2)
  if /i not "%%w"=="secureip-skip" set "WANT=!WANT!%%w "
)
if "%WANT%"==" " (echo --only selects nothing 1>&2 & exit /b 2)
for %%t in (vlib vmap vcom vlog) do (
  where %%t >nul 2>&1 || (echo %%t not found on PATH ^(the lvdsim tools^) 1>&2 & exit /b 2)
)
set "NEEDX=0"
set "NEEDA=0"
for %%w in (unisim unimacro unisim_verilog xpm) do echo %WANT% | findstr /i /c:" %%w " >nul && set "NEEDX=1"
for %%w in (altera_mf lpm sgate altera_primitives altera_lnsim altera_verilog cyclone10gx twentynm tennm) do echo %WANT% | findstr /i /c:" %%w " >nul && set "NEEDA=1"
if "%NEEDX%"=="1" if not exist "%VIVADO%\" (echo Vivado folder "%VIVADO%" not found: use --vivado or set XILINX_VIVADO 1>&2 & exit /b 2)
set "ASIM="
if "%NEEDA%"=="1" (
  if exist "%QUARTUS%\eda\sim_lib\" set "ASIM=%QUARTUS%\eda\sim_lib"
  if not defined ASIM if exist "%QUARTUS%\quartus\eda\sim_lib\" set "ASIM=%QUARTUS%\quartus\eda\sim_lib"
  if not defined ASIM (echo Quartus simulation sources not found under "%QUARTUS%" ^(looked for eda\sim_lib^): use --quartus or set QUARTUS_ROOTDIR 1>&2 & exit /b 2)
)
if not exist "%OUT%\logs\" mkdir "%OUT%\logs"
for %%i in ("%OUT%") do set "OUT=%%~fi"
set "LVDSIM_INI=%OUT%\lvdsim.ini"
if "%LVDSIM_NVC_OPTS%"=="" set "LVDSIM_NVC_OPTS=-M 128m"
set "VHDL=%VIVADO%\data\vhdl\src"
set "VLOG=%VIVADO%\data\verilog\src"
set "XPM=%VIVADO%\data\ip\xpm"
set "RC=0"
type nul > "%OUT%\logs\summary.txt"
type nul > "%OUT%\logs\failed.txt"

echo %WANT% | findstr /i /c:" unisim " >nul && call :unisim
echo %WANT% | findstr /i /c:" unimacro " >nul && call :unimacro
echo %WANT% | findstr /i /c:" unisim_verilog " >nul && call :unisim_v
echo %WANT% | findstr /i /c:" xpm " >nul && call :xpm
echo %WANT% | findstr /i /c:" altera_mf " >nul && call :altera_mf
echo %WANT% | findstr /i /c:" lpm " >nul && call :lpm
echo %WANT% | findstr /i /c:" sgate " >nul && call :sgate
echo %WANT% | findstr /i /c:" altera_primitives " >nul && call :altera_primitives
echo %WANT% | findstr /i /c:" altera_lnsim " >nul && call :altera_lnsim
for %%f in (cyclone10gx twentynm tennm) do echo %WANT% | findstr /i /c:" %%f " >nul && call :family %%f
echo %WANT% | findstr /i /c:" altera_verilog " >nul && call :altera_verilog

echo.
echo library            files ok   failed  skipped    time
type "%OUT%\logs\summary.txt"
echo output: %OUT%   (lvdsim.ini written; logs in %OUT%\logs)
for /f "usebackq delims=" %%l in ("%OUT%\logs\failed.txt") do (set "L=%%l" & echo !L:~0,200!)
echo %WANT% | findstr /i /c:" altera_verilog " /c:" altera_lnsim " >nul && echo Altera Verilog models: altera_mf_ver, lpm_ver and the altera_lnsim .sv part use constructs nvc does not analyse yet ^(docs\libraries.md^).
echo %WANT% | findstr /i /c:" unisim_verilog " /c:" xpm " >nul && echo Verilog designs also need glbl: vlog -work work "%VLOG%\glbl.v" and 'vsim ... work.top work.glbl'.
exit /b %RC%

:unisim
call :start unisim
set "U=%VHDL%\unisims"
if not exist "%U%\unisim_VCOMP.vhd" (echo source not found: %U%\unisim_VCOMP.vhd 1>&2 & set "RC=1" & exit /b 0)
call :newlib unisim
set "OPTS=-2008 -explicit -work unisim"
call :cfile vcom "%U%\unisim_VPKG.vhd"
call :cfile vcom "%U%\unisim_VCOMP.vhd"
if exist "%U%\retarget_VCOMP.vhd" call :cfile vcom "%U%\retarget_VCOMP.vhd"
if exist "%U%\unisim_retarget_VCOMP.vhd" call :cfile vcom "%U%\unisim_retarget_VCOMP.vhd"
for %%f in ("%U%\primitive\*.vhd") do call :cfile vcom "%%f"
for %%f in ("%U%\retarget\*.vhd") do call :cfile vcom "%%f"
call :finish 1
exit /b 0

:unimacro
call :start unimacro
set "M=%VHDL%\unimacro"
if not exist "%M%\unimacro_VCOMP.vhd" (echo source not found: %M%\unimacro_VCOMP.vhd 1>&2 & set "RC=1" & exit /b 0)
if not exist "%OUT%\unisim\" echo warning: no unisim library in %OUT%; unimacro needs it 1>&2
call :newlib unimacro
set "OPTS=-2008 -explicit -work unimacro -L unisim"
call :cfile vcom "%M%\unimacro_VCOMP.vhd"
for %%f in ("%M%\*_MACRO.vhd") do call :cfile vcom "%%f"
call :finish 1
exit /b 0

:unisim_v
call :start unisim_v
if not exist "%VLOG%\unisims\FDRE.v" (echo source not found: %VLOG%\unisims\FDRE.v 1>&2 & set "RC=1" & exit /b 0)
call :newlib unisim_v
set "OPTS=-work unisim_v"
for %%f in ("%VLOG%\unisims\*.v") do call :cfile vlog "%%f"
call :finish 0
exit /b 0

:xpm
call :start xpm
if not exist "%XPM%\xpm_cdc\hdl\xpm_cdc.sv" (echo source not found: %XPM%\xpm_cdc\hdl\xpm_cdc.sv 1>&2 & set "RC=1" & exit /b 0)
call :newlib xpm
set "OPTS=-sv -work xpm"
call :cfile vlog "%XPM%\xpm_cdc\hdl\xpm_cdc.sv"
call :cfile vlog "%XPM%\xpm_memory\hdl\xpm_memory.sv"
call :cfile vlog "%XPM%\xpm_fifo\hdl\xpm_fifo.sv"
set "OPTS=-2008 -work xpm"
call :cfile vcom "%XPM%\xpm_VCOMP.vhd"
call :finish 0
exit /b 0

:altera_mf
call :start altera_mf
if not exist "%ASIM%\altera_mf.vhd" (echo source not found: %ASIM%\altera_mf.vhd 1>&2 & set "RC=1" & exit /b 0)
call :newlib altera_mf
set "OPTS=-2008 -explicit -work altera_mf"
call :cfile vcom "%ASIM%\altera_mf_components.vhd"
call :cfile vcom "%ASIM%\altera_mf.vhd"
call :finish 1
exit /b 0

:lpm
call :start lpm
if not exist "%ASIM%\220model.vhd" (echo source not found: %ASIM%\220model.vhd 1>&2 & set "RC=1" & exit /b 0)
call :newlib lpm
set "OPTS=-2008 -explicit -work lpm"
call :cfile vcom "%ASIM%\220pack.vhd"
call :cfile vcom "%ASIM%\220model.vhd"
call :finish 1
exit /b 0

:sgate
call :start sgate
if not exist "%ASIM%\sgate.vhd" (echo source not found: %ASIM%\sgate.vhd 1>&2 & set "RC=1" & exit /b 0)
if not exist "%OUT%\lpm\" echo warning: no lpm library in %OUT%; sgate needs it 1>&2
call :newlib sgate
set "OPTS=-2008 -explicit -work sgate"
call :cfile vcom "%ASIM%\sgate_pack.vhd"
set "OPTS=-2008 -explicit -work sgate -L lpm"
call :cfile vcom "%ASIM%\sgate.vhd"
call :finish 1
exit /b 0

:altera_primitives
call :start altera_primitives
if not exist "%ASIM%\altera_primitives.vhd" (echo source not found: %ASIM%\altera_primitives.vhd 1>&2 & set "RC=1" & exit /b 0)
call :newlib altera_primitives
set "OPTS=-2008 -explicit -work altera_primitives"
call :cfile vcom "%ASIM%\altera_primitives_components.vhd"
call :cfile vcom "%ASIM%\altera_primitives.vhd"
call :finish 1
exit /b 0

:altera_lnsim
call :start altera_lnsim
if not exist "%ASIM%\altera_lnsim_components.vhd" (echo source not found: %ASIM%\altera_lnsim_components.vhd 1>&2 & set "RC=1" & exit /b 0)
call :newlib altera_lnsim
set "OPTS=-2008 -work altera_lnsim"
call :cfile vcom "%ASIM%\altera_lnsim_components.vhd"
set "OPTS=-sv -work altera_lnsim"
if exist "%ASIM%\altera_lnsim.sv" call :cfile vlog "%ASIM%\altera_lnsim.sv"
call :finish 0
exit /b 0

:family
call :start %~1
if not exist "%ASIM%\%~1_atoms.vhd" (echo source not found: %ASIM%\%~1_atoms.vhd 1>&2 & set "RC=1" & exit /b 0)
if not exist "%OUT%\altera_lnsim\" echo warning: no altera_lnsim library in %OUT%; %~1 needs it 1>&2
call :newlib %~1
set "OPTS=-2008 -explicit -work %~1 -L altera_lnsim"
call :cfile vcom "%ASIM%\%~1_atoms.vhd"
if exist "%ASIM%\%~1_components.vhd" call :cfile vcom "%ASIM%\%~1_components.vhd"
call :finish 0
exit /b 0

:altera_verilog
if "%QUIET%"=="0" echo altera_verilog ^(altera_ver, sgate_ver, lpm_ver, altera_mf_ver^)
call :averilog altera_ver altera_primitives.v
call :averilog sgate_ver sgate.v
call :averilog lpm_ver 220model.v
call :averilog altera_mf_ver altera_mf.v
exit /b 0

:averilog
if not exist "%ASIM%\%~2" (echo source not found: %ASIM%\%~2 1>&2 & set "RC=1" & exit /b 0)
call :start %~1
call :newlib %~1
set "OPTS=-work %~1"
call :cfile vlog "%ASIM%\%~2"
call :finish 0
exit /b 0

:start
set "LIBNAME=%~1"
set "OK=0" & set "FAIL=0" & set "SKIP=0" & set "FAILED="
call :secs T0
if "%QUIET%"=="0" echo %LIBNAME%
exit /b 0

:newlib
set "LOG=%OUT%\logs\%~1.log"
type nul > "%LOG%"
vlib "%OUT%\%~1" >> "%LOG%" 2>&1 && vmap %~1 "%OUT%\%~1" >> "%LOG%" 2>&1 || (echo cannot create library %~1 1>&2 & set /a FAIL+=1)
exit /b 0

:cfile
echo %~nx2| findstr /i /c:"_ncrypt." >nul 2>&1 && (
  set /a SKIP+=1
  if "%QUIET%"=="0" echo   skipped ^(encrypted, _ncrypt^): %~nx2
  exit /b 0
)
findstr /c:"`protect" "%~2" >nul 2>&1 && (
  set /a SKIP+=1
  if "%QUIET%"=="0" echo   skipped ^(encrypted, `protect^): %~nx2
  exit /b 0
)
echo == %~1 %~2 >> "%LOG%"
%~1 %OPTS% "%~2" >> "%LOG%" 2>&1 && (
  set /a OK+=1
) || (
  set /a FAIL+=1
  set "FAILED=!FAILED! %~nx2"
  if "%QUIET%"=="0" echo   FAILED: %~nx2 ^(see %LOG%^)
)
exit /b 0

:finish
call :secs T1
set /a T=T1-T0
if !T! lss 0 set /a T+=86400
set "ROW=%LIBNAME%                  "
set "ROW=!ROW:~0,18!"
set "A=         %OK%"
set "B=         %FAIL%"
set "C=         %SKIP%"
set "D=      %T%"
>> "%OUT%\logs\summary.txt" echo !ROW! !A:~-8! !B:~-8! !C:~-8! !D:~-6!s
if defined FAILED echo %LIBNAME% failed:!FAILED!>> "%OUT%\logs\failed.txt"
if "%~1"=="1" if %FAIL% gtr 0 set "RC=1"
if "%QUIET%"=="0" echo %LIBNAME%: %OK% ok, %FAIL% failed, %SKIP% skipped in %T%s
exit /b 0

:secs
set "NOW=%TIME: =0%"
set /a S=(1!NOW:~0,2!-100)*3600+(1!NOW:~3,2!-100)*60+(1!NOW:~6,2!-100)
set "%~1=%S%"
exit /b 0
