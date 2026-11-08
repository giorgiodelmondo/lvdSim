// A Xilinx LUT6 used as a 6 input AND. A VHDL component cannot pass the 64 bit INIT as a generic,
// so the wrapper sets it.
module lut6_and (
    output O,
    input [5:0] I
);
    LUT6 #(.INIT(64'h8000000000000000)) lut (
        .O(O), .I0(I[0]), .I1(I[1]), .I2(I[2]), .I3(I[3]), .I4(I[4]), .I5(I[5]));
endmodule
