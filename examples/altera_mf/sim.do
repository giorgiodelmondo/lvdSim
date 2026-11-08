# Run in the lvdsim GUI after the compile steps of run.sh (needs the libraries altera_mf and lpm).
add wave /clk
add wave /lc_o
add wave -radix unsigned /a
add wave -radix unsigned /b
add wave -radix unsigned /s
add wave -radix unsigned /cnt
add wave /wren
add wave -radix unsigned /addr
add wave -radix hex /d
add wave -radix hex /q
run 100 ns
