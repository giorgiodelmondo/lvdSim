-- Testbench: a triangle wave on the analog input crosses the comparator threshold a few times.
library ieee;
use ieee.std_logic_1164.all;

entity tb_xilinx is
end entity;

architecture sim of tb_xilinx is
  signal clk   : std_logic := '0';
  signal rst   : std_logic := '1';
  signal vin   : real := 0.0;
  signal above : std_logic;
  signal count : std_logic_vector(7 downto 0);
begin
  clk <= not clk after 5 ns;          -- 100 MHz
  rst <= '0' after 50 ns;

  -- triangle between 0 V and 3.3 V, period 200 ns
  stimulus : process
    variable v    : real := 0.0;
    variable step : real := 0.165;
  begin
    vin <= v;
    wait for 5 ns;
    v := v + step;
    if v >= 3.3 then v := 3.3; step := -0.165; end if;
    if v <= 0.0 then v := 0.0; step := 0.165; end if;
  end process;

  dut : entity work.comparator_sync
    port map (clk_in => clk, rst => rst, vin => vin, above => above, count => count);
end architecture;
