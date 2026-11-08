// Verilog: counts rising edges of a synchronised input.
module edge_counter (
    input  wire       clk,
    input  wire       rst,
    input  wire       din,
    output reg  [7:0] count
);
    reg prev;

    always @(posedge clk) begin
        if (rst) begin
            prev  <= 1'b0;
            count <= 8'd0;
        end else begin
            prev <= din;
            if (din && !prev) count <= count + 8'd1;
        end
    end
endmodule
