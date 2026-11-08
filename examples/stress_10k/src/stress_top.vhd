-- Stress design: N_LANES copies of lane_cell (VHDL + Verilog + SystemVerilog + Xilinx unisim), a real-valued
-- analog input with an ADC, a few Xilinx unimacro blocks, and a reduction tree that ends in one checksum bit.
--
-- Every lane contributes about 80 signals (the ports of each instance and the internals of the Xilinx primitives
-- count), and the unimacro blocks about 1 500, so N_LANES = 118 gives roughly 10 000 signals.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

library unisim;
use unisim.vcomponents.all;

library unimacro;
use unimacro.vcomponents.all;

entity stress_top is
  generic (
    N_LANES    : positive := 118;
    CLK_PERIOD : time     := 20 ns;    -- 50 MHz: 20 ms of simulated time is one million clocks
    GROUP_SIZE : positive := 25;
    USE_MACROS : boolean  := true      -- the Xilinx unimacro blocks (MULT_MACRO, BRAM_SINGLE_MACRO)
  );
end entity;

architecture sim of stress_top is
  constant N_GROUPS : positive := (N_LANES + GROUP_SIZE - 1) / GROUP_SIZE;

  signal clk_raw, clk_buf, clk : std_logic := '0';
  signal rst   : std_logic := '1';
  signal en    : std_logic_vector(7 downto 0) := (others => '0');   -- eight enable phases
  signal tick  : unsigned(2 downto 0) := (others => '0');

  type slv8_array is array (natural range <>) of std_logic_vector(7 downto 0);
  signal status : std_logic_vector(N_LANES - 1 downto 0);
  signal events : slv8_array(0 to N_LANES - 1);
  signal group_parity  : std_logic_vector(N_GROUPS - 1 downto 0);
  signal group_events  : slv8_array(0 to N_GROUPS - 1);
  signal checksum      : std_logic := '0';
  signal event_total   : unsigned(15 downto 0) := (others => '0');

  -- analog front end
  signal vin    : real := 0.0;
  signal sample : unsigned(7 downto 0) := (others => '0');
  signal product : std_logic_vector(15 downto 0);
  signal ram_out : std_logic_vector(17 downto 0);
  signal ram_we  : std_logic_vector(1 downto 0) := "11";
  signal ram_addr : std_logic_vector(9 downto 0) := (others => '0');
  signal ram_din  : std_logic_vector(17 downto 0) := (others => '0');
  signal vcc : std_logic := '1';
begin
  -- clock through Xilinx input and global buffers
  clk_raw <= not clk_raw after CLK_PERIOD / 2;
  ibuf_i : IBUF port map (I => clk_raw, O => clk_buf);
  bufg_i : BUFG port map (I => clk_buf, O => clk);

  rst <= '0' after 5 * CLK_PERIOD;

  -- enable phases: lane i uses phase (i mod 8), so only an eighth of the lanes advance each clock
  phase : process (clk)
  begin
    if rising_edge(clk) then
      tick <= tick + 1;
      en <= (others => '0');
      en(to_integer(tick)) <= '1';
    end if;
  end process;

  lanes : for i in 0 to N_LANES - 1 generate
    cell : entity work.lane_cell
      generic map (SEED => std_logic_vector(to_unsigned((i * 40503 + 1) mod 65535 + 1, 16)))
      port map (clk => clk, rst => rst, en => en(i mod 8), status => status(i), events => events(i));
  end generate;

  -- reduction in groups so that no single process is sensitive to every lane
  groups : for g in 0 to N_GROUPS - 1 generate
    reduce : process (status, events)
      variable p : std_logic;
      variable e : unsigned(7 downto 0);
    begin
      p := '0';
      e := (others => '0');
      for k in 0 to GROUP_SIZE - 1 loop
        if g * GROUP_SIZE + k < N_LANES then
          p := p xor status(g * GROUP_SIZE + k);
          e := e + unsigned(events(g * GROUP_SIZE + k));
        end if;
      end loop;
      group_parity(g) <= p;
      group_events(g) <= std_logic_vector(e);
    end process;
  end generate;

  total : process (clk)
    variable p : std_logic;
    variable t : unsigned(15 downto 0);
  begin
    if rising_edge(clk) then
      p := '0';
      t := (others => '0');
      for g in 0 to N_GROUPS - 1 loop
        p := p xor group_parity(g);
        t := t + resize(unsigned(group_events(g)), 16);
      end loop;
      checksum <= p;
      event_total <= t;
    end if;
  end process;

  -- analog input: a 20 kHz sine plus a 1 kHz ramp, updated every 5 us of simulated time
  analog : process
    variable t : real := 0.0;
  begin
    vin <= 1.65 + 1.4 * sin(2.0 * math_pi * 20.0e3 * t) + 0.2 * (t * 1.0e3 - floor(t * 1.0e3));
    wait for 5 us;
    t := t + 5.0e-6;
  end process;

  adc : process (clk)
    variable code : integer;
  begin
    if rising_edge(clk) then
      code := integer(floor(vin / 3.3 * 255.0));
      if code < 0 then code := 0; end if;
      if code > 255 then code := 255; end if;
      sample <= to_unsigned(code, 8);
    end if;
  end process;

  -- Xilinx unimacro blocks on the sampled signal
  macros : if USE_MACROS generate
    mult : MULT_MACRO
      generic map (DEVICE => "7SERIES", LATENCY => 1, WIDTH_A => 8, WIDTH_B => 8)
      port map (P => product, A => std_logic_vector(sample), B => std_logic_vector(sample),
                CE => vcc, CLK => clk, RST => rst);

    -- the address comes from a plain counter: an unknown address would make the RAM model very slow
    addr_count : process (clk)
      variable n : unsigned(9 downto 0) := (others => '0');
    begin
      if rising_edge(clk) then
        n := n + 1;
        ram_addr <= std_logic_vector(n);
      end if;
    end process;
    ram_din  <= "00" & product;

    ram : BRAM_SINGLE_MACRO
      generic map (BRAM_SIZE => "18Kb", DEVICE => "7SERIES", DO_REG => 0, WRITE_WIDTH => 18, READ_WIDTH => 18,
                   WRITE_MODE => "READ_FIRST")
      port map (DO => ram_out, ADDR => ram_addr, CLK => clk, DI => ram_din, EN => vcc,
                REGCE => vcc, RST => rst, WE => ram_we);
  end generate;
end architecture;
