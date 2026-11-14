-- SPDX-License-Identifier: GPL-3.0-or-later
-- Small design for the coverage tests: statements, branches, an expression, toggles and a state machine,
-- with one instance that runs and one that never leaves reset, so that some items stay uncovered.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity cov_counter is
    port (
        clk   : in  std_logic;
        rst   : in  std_logic;
        en    : in  std_logic;
        count : out unsigned(3 downto 0);
        wrap  : out std_logic);
end entity;

architecture rtl of cov_counter is
    type state_t is (IDLE, COUNTING, DONE, ERROR_STATE);
    signal state : state_t := IDLE;
    signal cnt   : unsigned(3 downto 0) := (others => '0');
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            if rst = '1' then
                cnt   <= (others => '0');
                state <= IDLE;
            else
                case state is
                    when IDLE =>
                        if en = '1' then
                            state <= COUNTING;
                        end if;
                    when COUNTING =>
                        cnt <= cnt + 1;
                        if cnt = 5 then
                            state <= DONE;
                        end if;
                    when DONE =>
                        state <= IDLE;
                    when ERROR_STATE =>
                        state <= IDLE;
                end case;
            end if;
        end if;
    end process;

    wrap  <= '1' when cnt = 15 and en = '1' else '0';
    count <= cnt;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity cov_tb is
end entity;

architecture test of cov_tb is
    signal clk    : std_logic := '0';
    signal rst    : std_logic := '1';
    signal en     : std_logic := '0';
    signal idle_r : std_logic := '1';
    signal c1, c2 : unsigned(3 downto 0);
    signal w1, w2 : std_logic;
    signal done   : boolean := false;
begin
    clk <= not clk after 5 ns when not done;

    u_busy : entity work.cov_counter
        port map (clk => clk, rst => rst, en => en, count => c1, wrap => w1);

    u_idle : entity work.cov_counter
        port map (clk => clk, rst => idle_r, en => '0', count => c2, wrap => w2);

    stim : process is
    begin
        wait for 12 ns;
        rst <= '0';
        wait for 10 ns;
        en <= '1';
        wait for 200 ns;
        done <= true;
        wait;
    end process;
end architecture;
