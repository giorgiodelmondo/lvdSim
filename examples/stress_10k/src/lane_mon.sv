// SystemVerilog: watches the running sum and counts how often its top nibble wraps through zero.
module lane_mon (
    input  logic        clk,
    input  logic        rst,
    input  logic [15:0] sum,
    output logic        wrapped,
    output logic [7:0]  events
);
    logic [3:0] last_top;

    always_ff @(posedge clk) begin
        if (rst) begin
            last_top <= 4'd0;
            wrapped  <= 1'b0;
            events   <= 8'd0;
        end else begin
            last_top <= sum[15:12];
            wrapped  <= (sum[15:12] < last_top);
            if (sum[15:12] < last_top) events <= events + 8'd1;
        end
    end
endmodule
