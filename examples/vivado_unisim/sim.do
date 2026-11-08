# Run in the lvdsim shell (GUI or console) after compile_unisim and the compile steps of run.sh.
add wave -divider {Analog input}
add wave -format analog-interpolated -height 80 -min 0 -max 3.3 /vin
add wave -divider {Xilinx primitives}
add wave /clk
add wave /dut/clk
add wave /dut/raw
add wave /dut/stage1
add wave /dut/stage2
add wave -divider {Verilog counter}
add wave -radix unsigned /count
run 2 us
