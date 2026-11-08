# GUI macro: a readable subset of the 10 000 signals. Adding all of them works too: add wave -r /*
add wave -divider {Clock and control}
add wave /clk
add wave /rst
add wave -radix unsigned /tick
add wave -divider {Analog input and ADC}
add wave -format analog-interpolated -height 80 -min 0 -max 3.3 /vin
add wave -radix unsigned /sample
add wave -divider {Xilinx unimacro}
add wave -radix unsigned /product
add wave -radix hex /ram_out
add wave -divider {Lane 0: VHDL, Verilog, SystemVerilog, unisim}
add wave -radix hex /lanes(0)/cell/lfsr
add wave -radix hex /lanes(0)/cell/sum
add wave /lanes(0)/cell/parity
add wave /lanes(0)/cell/wrapped
add wave /lanes(0)/cell/stage1
add wave /lanes(0)/cell/stage2
add wave -radix unsigned /lanes(0)/cell/events
add wave -divider {Totals}
add wave /checksum
add wave -radix unsigned /event_total
# The GUI run is short on purpose: the full design is heavy. run.sh/run.bat set LVDSIM_RUN to run longer.
if {[info exists ::env(LVDSIM_RUN)] && $::env(LVDSIM_RUN) ne ""} {
    run {*}$::env(LVDSIM_RUN)
} else {
    run 200 us
}
