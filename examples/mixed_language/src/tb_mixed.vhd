-- Testbench: VHDL analog source and ADC, Verilog filter, SystemVerilog monitor.
library ieee;
use ieee.std_logic_1164.all;

entity tb_mixed is
end entity;

architecture sim of tb_mixed is
  signal clk    : std_logic := '0';
  signal rst    : std_logic := '1';
  signal vin    : real := 0.0;
  signal sample : std_logic_vector(7 downto 0);
  signal avg    : std_logic_vector(7 downto 0);
  signal high   : std_logic;
  signal events : std_logic_vector(7 downto 0);

  component avg_filter is
    port (clk, rst : in std_logic; din : in std_logic_vector(7 downto 0);
          dout : out std_logic_vector(7 downto 0));
  end component;

  component level_monitor is
    port (clk, rst : in std_logic; level : in std_logic_vector(7 downto 0);
          high : out std_logic; events : out std_logic_vector(7 downto 0));
  end component;
begin
  clk <= not clk after 20 ns;          -- 25 MHz sample clock
  rst <= '0' after 100 ns;

  adc : entity work.adc_model
    port map (clk => clk, vin => vin, sample => sample);

  filt : component avg_filter
    port map (clk => clk, rst => rst, din => sample, dout => avg);

  mon : component level_monitor
    port map (clk => clk, rst => rst, level => avg, high => high, events => events);
end architecture;
