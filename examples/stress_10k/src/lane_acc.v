// Verilog: running sum of the LFSR output with a parity bit.
module lane_acc (
    input  wire        clk,
    input  wire        rst,
    input  wire        en,
    input  wire [15:0] din,
    output reg  [15:0] sum,
    output reg         parity
);
    always @(posedge clk) begin
        if (rst) begin
            sum    <= 16'd0;
            parity <= 1'b0;
        end else if (en) begin
            sum    <= sum + din;
            parity <= ^din;
        end
    end
endmodule
