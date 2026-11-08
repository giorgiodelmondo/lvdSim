library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
library altera_mf; use altera_mf.altera_mf_components.all;
library lpm; use lpm.lpm_components.all;

entity alt_demo is end;
architecture sim of alt_demo is
  signal clk : std_logic := '0';
  signal a, b, s : std_logic_vector(7 downto 0);
  signal cnt : std_logic_vector(7 downto 0);
  signal q : std_logic_vector(7 downto 0);
  signal wren : std_logic := '0';
  signal addr : std_logic_vector(3 downto 0) := (others => '0');
  signal d : std_logic_vector(7 downto 0) := (others => '0');
  signal lc_o : std_logic;
begin
  clk <= not clk after 5 ns;
  lc : lcell port map (a_in => clk, a_out => lc_o);
  add : lpm_add_sub generic map (lpm_width => 8, lpm_direction => "ADD")
        port map (dataa => a, datab => b, result => s);
  cn : lpm_counter generic map (lpm_width => 8) port map (clock => clk, q => cnt);
  ram : altsyncram generic map (operation_mode => "SINGLE_PORT", width_a => 8, widthad_a => 4, numwords_a => 16,
                                outdata_reg_a => "UNREGISTERED", init_file => "UNUSED", lpm_type => "altsyncram", intended_device_family => "Arria 10")
        port map (clock0 => clk, address_a => addr, data_a => d, wren_a => wren, q_a => q);
  process
  begin
    a <= x"21"; b <= x"1c";
    wait for 1 ns;
    d <= x"5a"; wren <= '1'; addr <= x"3";
    wait until rising_edge(clk); wait until rising_edge(clk);
    wren <= '0'; wait until rising_edge(clk); wait for 1 ns;
    report "sum=" & integer'image(to_integer(unsigned(s))) & " cnt=" & integer'image(to_integer(unsigned(cnt)))
           & " ram=" & integer'image(to_integer(unsigned(q)));
    assert unsigned(s) = 61 report "adder FAIL" severity failure;
    assert unsigned(q) = 16#5a# report "ram FAIL" severity failure;
    assert unsigned(cnt) > 0 report "counter FAIL" severity failure;
    report "PASS";
    std.env.finish;
  end process;
end;
