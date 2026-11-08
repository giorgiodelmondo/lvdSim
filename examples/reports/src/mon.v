// Verilog $display prints no timestamp: such lines stay plain text in the transcript.
module mon;
    initial begin
        #(120);
        $display("verilog display at 120 time units");
    end
endmodule
