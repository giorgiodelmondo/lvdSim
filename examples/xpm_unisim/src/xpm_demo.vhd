-- A VHDL design that uses Xilinx primitives written in Verilog (library unisim_v) and XPM blocks (library xpm).
-- Two clocks come through IBUF and BUFG, a counter is pipelined through FDRE flip-flops, a LUT6 detects a
-- counter value, and XPM clock-domain-crossing and FIFO blocks move data between the clocks.
-- The process at the bottom checks the results and prints PASS or FAIL.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library unisim_v;                -- loads the Verilog unisim library so the components below bind to it
library xpm;
use xpm.vcomponents.all;         -- component declarations of the XPM blocks

entity xpm_demo is
end entity;

architecture sim of xpm_demo is
    -- Verilog parameters are VHDL integer generics (or string generics, see the XPM blocks)
    component IBUF is
        port (O : out std_logic; I : in std_logic);
    end component;
    component BUFG is
        port (O : out std_logic; I : in std_logic);
    end component;
    component FDRE is
        generic (INIT : integer := 0);
        port (Q : out std_logic; C : in std_logic; CE : in std_logic; D : in std_logic; R : in std_logic);
    end component;
    -- LUT6 has a 64 bit INIT, which does not fit an integer generic: lut6_and.v fixes it in Verilog
    component lut6_and is
        port (O : out std_logic; I : in std_logic_vector(5 downto 0));
    end component;

    signal clk_pad, clk_ibuf, clk   : std_logic := '0';
    signal sclk_pad, sclk_ibuf, sclk : std_logic := '0';
    signal rst                      : std_logic := '1';

    signal cnt                      : unsigned(3 downto 0) := (others => '0');
    signal ff0, ff1                 : std_logic;
    signal all_ones                 : std_logic;

    signal gcnt                     : unsigned(3 downto 0) := (others => '0');
    signal gcnt_dest                : std_logic_vector(3 downto 0);
    signal flag_dest                : std_logic;

    signal wr_en, rd_en, full, empty, dvalid : std_logic := '0';
    signal din, dout                : std_logic_vector(15 downto 0) := (others => '0');
    signal wr_busy, rd_busy         : std_logic;
    signal nread, fifo_errors       : natural := 0;
begin
    clk_pad  <= not clk_pad after 5 ns;      -- 100 MHz
    sclk_pad <= not sclk_pad after 8 ns;     -- 62.5 MHz

    ib0 : IBUF port map (O => clk_ibuf, I => clk_pad);
    bg0 : BUFG port map (O => clk, I => clk_ibuf);
    ib1 : IBUF port map (O => sclk_ibuf, I => sclk_pad);
    bg1 : BUFG port map (O => sclk, I => sclk_ibuf);

    -- Counter on clk, delayed through two FDRE flip-flops
    process (clk) begin
        if rising_edge(clk) then
            cnt <= cnt + 1;
        end if;
    end process;

    f0 : FDRE generic map (INIT => 0) port map (Q => ff0, C => clk, CE => '1', D => cnt(0), R => '0');
    f1 : FDRE generic map (INIT => 0) port map (Q => ff1, C => clk, CE => '1', D => ff0, R => '0');

    -- LUT6 as a 6 input AND of cnt and two constant ones: high when cnt = 15
    l6 : lut6_and port map (O => all_ones, I => "11" & std_logic_vector(cnt));

    -- Clock domain crossing from clk to sclk: a single bit and a Gray coded counter that changes slowly
    process (clk) begin
        if rising_edge(clk) then
            if cnt = 7 then
                gcnt <= gcnt + 1;
            end if;
        end if;
    end process;

    x_single : xpm_cdc_single
        generic map (DEST_SYNC_FF => 4, INIT_SYNC_FF => 0, SIM_ASSERT_CHK => 0, SRC_INPUT_REG => 1)
        port map (src_clk => clk, src_in => cnt(3), dest_clk => sclk, dest_out => flag_dest);

    x_gray : xpm_cdc_gray
        generic map (DEST_SYNC_FF => 4, INIT_SYNC_FF => 0, REG_OUTPUT => 0, SIM_ASSERT_CHK => 0,
                     SIM_LOSSLESS_GRAY_CHK => 0, WIDTH => 4)
        port map (src_clk => clk, src_in_bin => std_logic_vector(gcnt), dest_clk => sclk,
                  dest_out_bin => gcnt_dest);

    -- Synchronous FIFO written on clk, 16 deep. String generics reach the Verilog parameters
    x_fifo : xpm_fifo_sync
        generic map (FIFO_MEMORY_TYPE => "distributed", FIFO_WRITE_DEPTH => 16, WRITE_DATA_WIDTH => 16,
                     READ_DATA_WIDTH => 16, READ_MODE => "std", FIFO_READ_LATENCY => 1,
                     USE_ADV_FEATURES => "1404", WR_DATA_COUNT_WIDTH => 5, RD_DATA_COUNT_WIDTH => 5,
                     SIM_ASSERT_CHK => 0)
        port map (sleep => '0', rst => rst, wr_clk => clk, wr_en => wr_en, din => din, full => full,
                  rd_en => rd_en, dout => dout, empty => empty, data_valid => dvalid,
                  injectsbiterr => '0', injectdbiterr => '0', wr_rst_busy => wr_busy, rd_rst_busy => rd_busy);

    -- The FIFO has a read latency of one clock: dout is valid in the cycle where data_valid is high
    reader : process (clk)
    begin
        if rising_edge(clk) then
            if dvalid = '1' then
                nread <= nread + 1;
                if to_integer(unsigned(dout)) /= (nread + 1) * 257 then
                    fifo_errors <= fifo_errors + 1;
                end if;
            end if;
        end if;
    end process;

    check : process
        variable seen_one, seen_lut, seen_gray, seen_ff : boolean := false;
        variable errors : natural := 0;
    begin
        -- glbl.GSR is released after 100 ns; wait for the reset to finish
        wait for 200 ns;
        rst <= '0';
        wait until wr_busy = '0' and rd_busy = '0';
        wait until rising_edge(clk);

        -- Write 8 words, then read them back
        for i in 1 to 8 loop
            wr_en <= '1';
            din <= std_logic_vector(to_unsigned(i * 257, 16));
            wait until rising_edge(clk);
        end loop;
        wr_en <= '0';
        wait for 50 ns;

        for i in 1 to 8 loop
            rd_en <= '1';
            wait until rising_edge(clk);
        end loop;
        rd_en <= '0';
        wait for 100 ns;

        -- Let the counters run a while and look at the other blocks
        for i in 1 to 200 loop
            wait until rising_edge(clk);
            if flag_dest = '1' then seen_one := true; end if;
            if all_ones = '1' then seen_lut := true; end if;
            if gcnt_dest /= "0000" then seen_gray := true; end if;
            if ff1 = '1' then seen_ff := true; end if;
        end loop;

        if nread /= 8 then errors := errors + 1; report "FIFO returned " & integer'image(nread) & " words" severity warning; end if;
        if fifo_errors /= 0 then errors := errors + 1; report "FIFO data is wrong" severity warning; end if;
        if not seen_one then errors := errors + 1; report "CDC single bit never changed" severity warning; end if;
        if not seen_lut then errors := errors + 1; report "LUT6 never fired" severity warning; end if;
        if not seen_gray then errors := errors + 1; report "Gray CDC output stuck" severity warning; end if;
        if not seen_ff then errors := errors + 1; report "FDRE chain stuck" severity warning; end if;

        report "FIFO words read: " & integer'image(nread) & ", counter " & integer'image(to_integer(cnt))
            & ", gray crossing " & integer'image(to_integer(unsigned(gcnt_dest)));
        if errors = 0 then
            report "PASS";
        else
            report "FAIL: " & integer'image(errors) & " errors" severity failure;
        end if;
        std.env.finish;
    end process;
end architecture;
