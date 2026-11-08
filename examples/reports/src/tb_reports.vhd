-- Reports of every severity at different times and units, for the transcript links and the event markers.
library ieee;
use ieee.std_logic_1164.all;

entity tb_reports is
end entity;

architecture sim of tb_reports is
    signal clk  : std_logic := '0';
    signal cnt  : integer := 0;
begin
    clk <= not clk after 5 ns;

    process (clk)
    begin
        if rising_edge(clk) then
            cnt <= cnt + 1;
        end if;
    end process;

    process
    begin
        wait for 100 ns;
        report "reset released" severity note;
        wait for 150 ns;
        report "fifo almost full" severity warning;
        wait for 50 ns;
        report "burst one" severity note;
        report "burst two" severity note;
        report "burst three" severity warning;
        wait for 200 ns;
        assert false report "checksum mismatch" severity error;
        wait for 1 us;
        report "late note at one and a half microseconds" severity note;
        wait for 500 ns;
        assert false report "fatal condition, stopping" severity failure;
        wait;
    end process;
end architecture;
