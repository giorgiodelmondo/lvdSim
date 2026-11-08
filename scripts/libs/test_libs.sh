#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Test compile_libs.sh against a tiny made-up "Vivado" tree (no Xilinx sources needed).
#   test_libs.sh [TOOLS_BIN_DIR]     dir with vlib/vmap/vcom/vlog (default: build/mingw-debug/bin or PATH)
# Exit 0 = pass, 77 = skipped (nvc or tools not found; ctest SKIP_RETURN_CODE).
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
[ -n "$1" ] && [ -d "$1" ] && PATH="$(cd "$1" && pwd):$PATH"
[ -d "$ROOT/build/mingw-debug/bin" ] && PATH="$PATH:$ROOT/build/mingw-debug/bin"
export PATH
command -v vcom > /dev/null 2>&1 || { echo "SKIP: lvdsim tools not on PATH"; exit 77; }
K="$ROOT/kernel/nvc/build"
if [ -z "$LVDSIM_NVC" ]; then
    [ -x "$K/bin/nvc.exe" ] && export LVDSIM_NVC="$K/bin/nvc.exe" LVDSIM_NVC_LIBDIR="$K/lib" LVDSIM_NVC_DLLDIR="${LVDSIM_NVC_DLLDIR:-C:/msys64/mingw64/bin}"
    [ -z "$LVDSIM_NVC" ] && [ -x "$K/bin/nvc" ] && export LVDSIM_NVC="$K/bin/nvc" LVDSIM_NVC_LIBDIR="$K/lib"
fi
if [ -z "$LVDSIM_NVC" ] && ! command -v nvc > /dev/null 2>&1; then echo "SKIP: nvc not found"; exit 77; fi

T="${LVD_TEST_TMP:-${TMPDIR:-/tmp}}/lvd_libs_test_$$"
FAKE="$T/vivado"; U="$FAKE/data/vhdl/src/unisims"; M="$FAKE/data/vhdl/src/unimacro"
mkdir -p "$U/primitive" "$U/retarget" "$M" "$T/run" || exit 1
cat > "$U/unisim_VPKG.vhd" <<'V'
package vpkg is constant k : integer := 1; end package;
V
cat > "$U/unisim_VCOMP.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
package vcomponents is
  component MYBUF port (I : in std_logic; O : out std_logic); end component;
end package;
V
cat > "$U/primitive/MYBUF.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
entity MYBUF is port (I : in std_logic; O : out std_logic); end;
architecture a of MYBUF is begin O <= I; end;
V
cat > "$U/primitive/SECRET.vhd" <<'V'
`protect begin_protected
`protect end_protected
V
cat > "$U/retarget/MYBUF2.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
entity MYBUF2 is port (I : in std_logic; O : out std_logic); end;
architecture a of MYBUF2 is begin O <= I; end;
V
cat > "$M/unimacro_VCOMP.vhd" <<'V'
package ucomp is constant k : integer := 2; end package;
V
cat > "$M/X_MACRO.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
library unisim; use unisim.vcomponents.all;
entity X_MACRO is port (I : in std_logic; O : out std_logic); end;
architecture a of X_MACRO is begin u : entity unisim.MYBUF port map (I, O); end;
V

fail() { echo "FAIL: $*"; echo "output dir kept: $T"; exit 1; }

# 1. default set, one encrypted file skipped, everything else compiles
sh "$HERE/compile_libs.sh" --vivado "$FAKE" --out "$T/out1" > "$T/run1.txt" 2>&1
rc=$?; [ $rc -eq 0 ] || { cat "$T/run1.txt"; fail "default run returned $rc"; }
grep -q "skipped (encrypted" "$T/run1.txt" || fail "encrypted file not reported"
grep -q '^unisim  *4  *0  *1 ' "$T/run1.txt" || { cat "$T/run1.txt"; fail "unisim row (4 ok, 0 failed, 1 skipped) missing"; }
grep -q '^unimacro  *2  *0  *0 ' "$T/run1.txt" || { cat "$T/run1.txt"; fail "unimacro row missing"; }
grep -qi '^unisim *= ' "$T/out1/lvdsim.ini" || fail "lvdsim.ini has no unisim mapping"
grep -qi '^unimacro *= ' "$T/out1/lvdsim.ini" || fail "lvdsim.ini has no unimacro mapping"

# 2. a project that uses only the written lvdsim.ini
cat > "$T/run/t.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
library unisim; use unisim.vcomponents.all;
library unimacro;
entity t is end;
architecture a of t is
  signal i, o, p : std_logic := '1';
begin
  u : entity unisim.MYBUF port map (i, o);
  m : entity unimacro.X_MACRO port map (i, p);
  process begin wait for 1 ns; assert o = '1' and p = '1' report "FAIL" severity failure; report "PASS"; std.env.finish; end process;
end;
V
cp "$T/out1/lvdsim.ini" "$T/run/lvdsim.ini"
( cd "$T/run" && vlib work > /dev/null && vcom -2008 -L unisim -L unimacro t.vhd > c.txt 2>&1 \
  && vsim -c -L unisim -L unimacro -do "run -all" work.t > r.txt 2>&1 ) || { cat "$T/run/c.txt" "$T/run/r.txt" 2> /dev/null; fail "design using the libraries"; }
grep -q PASS "$T/run/r.txt" || fail "design did not print PASS"

# 3. a failing file is counted, the run continues, the exit status is non-zero
cat > "$U/primitive/BROKEN.vhd" <<'V'
entity BROKEN is port ( end;
V
sh "$HERE/compile_libs.sh" --vivado "$FAKE" --out "$T/out2" > "$T/run2.txt" 2>&1
rc=$?; [ $rc -ne 0 ] || fail "failing unisim file gave exit status 0"
grep -q '^unisim  *4  *1  *1 ' "$T/run2.txt" || { cat "$T/run2.txt"; fail "unisim row (4 ok, 1 failed) missing"; }
grep -q '^unimacro  *2  *0 ' "$T/run2.txt" || fail "run did not continue to unimacro"

# 4. usage errors
sh "$HERE/compile_libs.sh" --vivado "$FAKE" --out "$T/out3" --only nonsense > /dev/null 2>&1; [ $? -eq 2 ] || fail "bad --only accepted"
sh "$HERE/compile_libs.sh" --vivado "$T/nowhere" --out "$T/out3" > /dev/null 2>&1; [ $? -eq 2 ] || fail "missing Vivado accepted"

# 5. A made-up Quartus tree (eda/sim_lib; no Altera sources): default Altera set, a design that uses it, skip of an encrypted file
Q="$T/quartus/quartus"; A="$Q/eda/sim_lib"; mkdir -p "$A" "$T/run5"
cat > "$A/altera_mf_components.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
package altera_mf_components is
  component mybuf generic (w : integer := 1); port (a : in std_logic; q : out std_logic); end component;
end package;
V
cat > "$A/altera_mf.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
entity mybuf is generic (w : integer := 1); port (a : in std_logic; q : out std_logic); end;
architecture a of mybuf is begin q <= a; end;
V
cat > "$A/220pack.vhd" <<'V'
package lpm_components is constant k : integer := 3; end package;
V
cat > "$A/220model.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
entity lpm_inv is port (data : in std_logic; result : out std_logic); end;
architecture lpm_syn of lpm_inv is begin result <= not data; end;
V
cat > "$A/sgate_pack.vhd" <<'V'
package sgate_pack is constant k : integer := 4; end package;
V
cat > "$A/sgate.vhd" <<'V'
library lpm;
entity sgate_dummy is end;
architecture a of sgate_dummy is begin end;
V
cat > "$A/altera_primitives_components.vhd" <<'V'
package altera_primitives_components is constant k : integer := 5; end package;
V
cat > "$A/altera_primitives.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
entity lcell is port (a_in : in std_logic; a_out : out std_logic); end;
architecture behavior of lcell is begin a_out <= a_in; end;
V
cat > "$A/altera_lnsim_components.vhd" <<'V'
package altera_lnsim_components is constant k : integer := 6; end package;
V
cat > "$A/altera_lnsim.sv" <<'V'
`protect begin_protected
`protect end_protected
V
sh "$HERE/compile_libs.sh" --quartus "$T/quartus" --out "$T/out5" > "$T/run5.txt" 2>&1
rc=$?; [ $rc -eq 0 ] || { cat "$T/run5.txt"; fail "Altera default run returned $rc"; }
for row in 'altera_mf  *2  *0  *0 ' 'lpm  *2  *0  *0 ' 'sgate  *2  *0  *0 ' 'altera_primitives  *2  *0  *0 '; do
    grep -q "^$row" "$T/run5.txt" || { cat "$T/run5.txt"; fail "Altera summary row '$row' missing"; }
done
grep -qi '^altera_mf *= ' "$T/out5/lvdsim.ini" || fail "lvdsim.ini has no altera_mf mapping"
! grep -qi '^unisim' "$T/out5/lvdsim.ini" || fail "Altera only run mapped unisim"
cat > "$T/run5/t.vhd" <<'V'
library ieee; use ieee.std_logic_1164.all;
library altera_mf; use altera_mf.altera_mf_components.all;
library lpm;
library altera_primitives;
entity t is end;
architecture a of t is
  signal i, o, p : std_logic := '1';
begin
  u : mybuf port map (i, o);
  l : entity altera_primitives.lcell port map (i, p);
  v : entity lpm.lpm_inv port map (i, open);
  process begin wait for 1 ns; assert o = '1' and p = '1' report "FAIL" severity failure; report "PASS"; std.env.finish; end process;
end;
V
cp "$T/out5/lvdsim.ini" "$T/run5/lvdsim.ini"
( cd "$T/run5" && vlib work > /dev/null && vcom -2008 -L altera_mf -L lpm -L altera_primitives t.vhd > c.txt 2>&1 \
  && vsim -c -L altera_mf -L lpm -L altera_primitives -do "run -all" work.t > r.txt 2>&1 ) || { cat "$T/run5/c.txt" "$T/run5/r.txt" 2> /dev/null; fail "design using the Altera libraries"; }
grep -q PASS "$T/run5/r.txt" || fail "Altera design did not print PASS"
# the Quartus folder itself (eda/sim_lib directly below) and an opt-in library with an encrypted .sv file
sh "$HERE/compile_libs.sh" --quartus "$Q" --out "$T/out6" --only altera_lnsim > "$T/run6.txt" 2>&1
rc=$?; [ $rc -eq 0 ] || { cat "$T/run6.txt"; fail "altera_lnsim run returned $rc"; }
grep -q "skipped (encrypted" "$T/run6.txt" || fail "encrypted altera_lnsim.sv not reported"
grep -q '^altera_lnsim  *1  *0  *1 ' "$T/run6.txt" || { cat "$T/run6.txt"; fail "altera_lnsim row (1 ok, 0 failed, 1 skipped) missing"; }
sh "$HERE/compile_libs.sh" --quartus "$T/nowhere" --out "$T/out7" > /dev/null 2>&1; [ $? -eq 2 ] || fail "missing Quartus accepted"
echo "PASS: compile_libs.sh ($T)"
