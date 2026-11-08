-- Analog front end modelled with real-valued signals: a sine wave plus a slow ramp, sampled and
-- quantised by a 8-bit ADC that is clocked from the testbench.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

entity adc_model is
  generic (
    VREF   : real := 3.3;
    FSIG   : real := 1.0e6   -- signal frequency in Hz
  );
  port (
    clk    : in  std_logic;
    vin    : out real := 0.0;                       -- analog input, in volts
    sample : out std_logic_vector(7 downto 0)       -- converted value
  );
end entity;

architecture behav of adc_model is
begin
  -- the analog source updates every 10 ns of simulated time
  source : process
    variable t : real := 0.0;
  begin
    vin <= VREF / 2.0 + 0.45 * VREF * sin(2.0 * math_pi * FSIG * t) + 0.02 * VREF * (t * 2.0e4);
    wait for 10 ns;
    t := t + 10.0e-9;
  end process;

  -- the sampler: quantise on the rising clock edge
  convert : process (clk)
    variable code : integer;
  begin
    if rising_edge(clk) then
      code := integer(floor(vin / VREF * 255.0));
      if code < 0 then code := 0; end if;
      if code > 255 then code := 255; end if;
      sample <= std_logic_vector(to_unsigned(code, 8));
    end if;
  end process;
end architecture;
