-- SPDX-License-Identifier: GPL-3.0-or-later
-- Coverage export test design: a for-generate of instances of an entity with an extended name, a record, a
-- memory (toggle coverage with include-mems) and a signal with an extended name.
library ieee;
use ieee.std_logic_1164.all;
entity \Leaf Unit\ is
  port (clk : in std_logic; d : in std_logic_vector(1 downto 0); q : out std_logic);
end entity;
architecture rtl of \Leaf Unit\ is
  type rec_t is record
    a : std_logic;
    b : std_logic_vector(1 downto 0);
  end record;
  type mem_t is array (0 to 1) of std_logic_vector(1 downto 0);
  signal r : rec_t := ('0', "00");
  signal m : mem_t := (others => "00");
  signal \odd name\ : std_logic := '0';
begin
  process (clk)
  begin
    if rising_edge(clk) then
      r.a <= d(0);
      r.b <= d;
      m(0) <= d;
      \odd name\ <= d(1) and d(0);
      if d = "11" or d = "00" then
        q <= '1';
      else
        q <= '0';
      end if;
    end if;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity RichTop is
end entity;
architecture tb of RichTop is
  signal clk : std_logic := '0';
  signal d : std_logic_vector(1 downto 0) := "00";
  signal q : std_logic_vector(0 to 2);
begin
  clk <= not clk after 5 ns when now < 200 ns;
  d <= "01" after 20 ns, "11" after 40 ns;
  g : for i in 0 to 2 generate
    u : entity work.\Leaf Unit\ port map (clk => clk, d => d, q => q(i));
  end generate;
end architecture;
