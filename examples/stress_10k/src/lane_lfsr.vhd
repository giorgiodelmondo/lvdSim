-- VHDL: a 16-bit Galois LFSR that advances when `en` is high. Every lane has its own seed.
library ieee;
use ieee.std_logic_1164.all;

entity lane_lfsr is
  generic (
    SEED : std_logic_vector(15 downto 0) := x"ACE1"
  );
  port (
    clk : in  std_logic;
    rst : in  std_logic;
    en  : in  std_logic;
    q   : out std_logic_vector(15 downto 0)
  );
end entity;

architecture rtl of lane_lfsr is
  signal state : std_logic_vector(15 downto 0) := SEED;
begin
  step : process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        state <= SEED;
      elsif en = '1' then
        if state(0) = '1' then
          state <= ('0' & state(15 downto 1)) xor x"B400";
        else
          state <= '0' & state(15 downto 1);
        end if;
      end if;
    end if;
  end process;

  q <= state;
end architecture;
