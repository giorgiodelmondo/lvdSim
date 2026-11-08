// Verilog: four-sample moving average of the converted value.
module avg_filter (
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] din,
    output reg  [7:0] dout
);
    reg [7:0] d1, d2, d3;
    wire [9:0] sum = din + d1 + d2 + d3;

    always @(posedge clk) begin
        if (rst) begin
            d1 <= 8'd0; d2 <= 8'd0; d3 <= 8'd0; dout <= 8'd0;
        end else begin
            d1 <= din; d2 <= d1; d3 <= d2;
            dout <= sum[9:2];
        end
    end
endmodule
