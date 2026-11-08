// SystemVerilog: raises `high` when the filtered value stays above a threshold for four clocks,
// and counts how often that happened.
module level_monitor #(
    parameter logic [7:0] THRESHOLD = 8'd200
) (
    input  logic       clk,
    input  logic       rst,
    input  logic [7:0] level,
    output logic       high,
    output logic [7:0] events
);
    logic [2:0] run;

    always_ff @(posedge clk) begin
        if (rst) begin
            run    <= 3'd0;
            high   <= 1'b0;
            events <= 8'd0;
        end else begin
            if (level > THRESHOLD) begin
                if (run != 3'd4) run <= run + 3'd1;
            end else begin
                run <= 3'd0;
            end
            if (run == 3'd3 && level > THRESHOLD) begin
                high   <= 1'b1;
                events <= events + 8'd1;
            end else if (level <= THRESHOLD) begin
                high <= 1'b0;
            end
        end
    end
endmodule
