-- A comparator on a real-valued analog input followed by Xilinx primitives from the unisim library:
-- an input buffer, a global clock buffer and a two-stage synchroniser made of FDRE flip-flops.
-- The edge counter behind it is Verilog.
library ieee;
use ieee.std_logic_1164.all;

library unisim;
use unisim.vcomponents.all;

entity comparator_sync is
  generic (
    THRESHOLD : real := 1.65
  );
  port (
    clk_in : in  std_logic;
    rst    : in  std_logic;
    vin    : in  real;                        -- analog input, in volts
    above  : out std_logic;                   -- synchronised comparator output
    count  : out std_logic_vector(7 downto 0) -- rising edges seen so far
  );
end entity;

architecture rtl of comparator_sync is
  signal clk, clk_ibuf : std_logic;
  signal raw, stage1, stage2, const1, const0 : std_logic;

  component edge_counter is
    port (clk, rst, din : in std_logic; count : out std_logic_vector(7 downto 0));
  end component;
begin
  const1 <= '1';
  const0 <= '0';

  -- the analog comparator, behavioural
  raw <= '1' when vin > THRESHOLD else '0';

  ibuf_i : IBUF  port map (I => clk_in, O => clk_ibuf);
  bufg_i : BUFG  port map (I => clk_ibuf, O => clk);

  sync1 : FDRE generic map (INIT => '0')
    port map (Q => stage1, C => clk, CE => const1, D => raw,    R => rst);
  sync2 : FDRE generic map (INIT => '0')
    port map (Q => stage2, C => clk, CE => const1, D => stage1, R => rst);

  above <= stage2;

  counter : component edge_counter
    port map (clk => clk, rst => rst, din => stage2, count => count);
end architecture;
