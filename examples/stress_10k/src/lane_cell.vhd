-- One lane: VHDL LFSR -> Verilog accumulator -> SystemVerilog monitor, with the monitor flag passed
-- through two Xilinx FDRE flip-flops and combined with the enable in a Xilinx LUT2.
library ieee;
use ieee.std_logic_1164.all;

library unisim;
use unisim.vcomponents.all;

entity lane_cell is
  generic (
    SEED : std_logic_vector(15 downto 0) := x"ACE1"
  );
  port (
    clk    : in  std_logic;
    rst    : in  std_logic;
    en     : in  std_logic;
    status : out std_logic;                      -- synchronised wrap flag xor enable
    events : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of lane_cell is
  signal lfsr    : std_logic_vector(15 downto 0);
  signal sum     : std_logic_vector(15 downto 0);
  signal parity  : std_logic;
  signal wrapped : std_logic;
  signal stage1, stage2 : std_logic;
  signal one     : std_logic := '1';

  component lane_acc is
    port (clk, rst, en : in std_logic; din : in std_logic_vector(15 downto 0);
          sum : out std_logic_vector(15 downto 0); parity : out std_logic);
  end component;

  component lane_mon is
    port (clk, rst : in std_logic; sum : in std_logic_vector(15 downto 0);
          wrapped : out std_logic; events : out std_logic_vector(7 downto 0));
  end component;
begin
  lfsr_i : entity work.lane_lfsr
    generic map (SEED => SEED)
    port map (clk => clk, rst => rst, en => en, q => lfsr);

  acc_i : component lane_acc
    port map (clk => clk, rst => rst, en => en, din => lfsr, sum => sum, parity => parity);

  mon_i : component lane_mon
    port map (clk => clk, rst => rst, sum => sum, wrapped => wrapped, events => events);

  sync1 : FDRE generic map (INIT => '0')
    port map (Q => stage1, C => clk, CE => one, D => wrapped, R => rst);
  sync2 : FDRE generic map (INIT => '0')
    port map (Q => stage2, C => clk, CE => one, D => stage1, R => rst);

  combine : LUT2 generic map (INIT => "0110")
    port map (O => status, I0 => stage2, I1 => parity);
end architecture;
