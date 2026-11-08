# Run in the lvdsim shell: add the signals to the wave window, then run.
add wave -divider {Analog front end}
add wave -format analog-interpolated -height 80 -min 0 -max 3.3 /vin
add wave /clk
add wave -radix unsigned /sample
add wave -divider {Verilog filter}
add wave -radix unsigned /avg
add wave -divider {SystemVerilog monitor}
add wave /high
add wave -radix unsigned /events
run 2 us
