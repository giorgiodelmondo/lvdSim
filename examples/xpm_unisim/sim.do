# Run in the lvdsim GUI after run.sh:   lvdsim-gui -L xpm -L unisim_v -do sim.do work.xpm_demo work.glbl
add wave -divider {Clocks (IBUF and BUFG from Verilog unisim)}
add wave /clk_pad
add wave /clk
add wave /sclk
add wave -divider {FDRE chain and LUT6}
add wave -radix unsigned /cnt
add wave /ff0
add wave /ff1
add wave /all_ones
add wave -divider {XPM clock domain crossing}
add wave /flag_dest
add wave -radix unsigned /gcnt
add wave -radix unsigned /gcnt_dest
add wave -divider {XPM FIFO}
add wave /wr_en
add wave -radix unsigned /din
add wave /rd_en
add wave -radix unsigned /dout
add wave /empty
run 1 us
