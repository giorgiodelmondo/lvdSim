// A Verilog testbench for Xilinx Verilog primitives: IBUF and BUFG on the clock, FDRE, LUT6, SRL16E and RAMB18E1.
// The models read the global module glbl (GSR is released after 100 ns), so glbl must be a second top level:
//     vsim -c work.tb_unisim work.glbl
`timescale 1ps / 1ps
module tb_unisim;
    reg clk_pad = 0;
    always #5000 clk_pad = ~clk_pad;            // 100 MHz

    wire clk_ibuf, clk;
    IBUF ib (.I(clk_pad), .O(clk_ibuf));
    BUFG bg (.I(clk_ibuf), .O(clk));

    reg d = 0;
    wire q;
    FDRE #(.INIT(1'b0)) ff (.C(clk), .CE(1'b1), .R(1'b0), .D(d), .Q(q));

    reg [5:0] i6 = 0;
    wire lut_o;
    LUT6 #(.INIT(64'h8000000000000000)) l6 (
        .O(lut_o), .I0(i6[0]), .I1(i6[1]), .I2(i6[2]), .I3(i6[3]), .I4(i6[4]), .I5(i6[5]));

    wire srl_q;                                  // address 3: output is the input delayed by four clocks
    SRL16E #(.INIT(16'h0000)) sr (
        .CLK(clk), .CE(1'b1), .D(d), .A0(1'b1), .A1(1'b1), .A2(1'b0), .A3(1'b0), .Q(srl_q));

    reg [9:0] waddr = 0, raddr = 0;
    reg [15:0] wdata = 0;
    reg we = 0;
    wire [15:0] rdata;
    RAMB18E1 #(.READ_WIDTH_A(18), .WRITE_WIDTH_A(18), .READ_WIDTH_B(18), .WRITE_WIDTH_B(18),
               .DOA_REG(0), .DOB_REG(0), .SIM_DEVICE("7SERIES")) ram (
        .CLKARDCLK(clk), .CLKBWRCLK(clk), .ENARDEN(1'b1), .ENBWREN(1'b1),
        .REGCEAREGCE(1'b0), .REGCEB(1'b0), .RSTRAMARSTRAM(1'b0), .RSTRAMB(1'b0),
        .RSTREGARSTREG(1'b0), .RSTREGB(1'b0),
        .ADDRARDADDR({raddr, 4'b0}), .ADDRBWRADDR({waddr, 4'b0}),
        .WEA(2'b00), .WEBWE({4{we}}), .DIADI(16'b0), .DIPADIP(2'b0), .DIBDI(wdata), .DIPBDIP(2'b0),
        .DOADO(rdata), .DOPADOP(), .DOBDO(), .DOPBDOP());

    integer errors = 0, n;

`define CHECK(cond, msg) if (!(cond)) begin errors = errors + 1; $display("FAILED: %0s", msg); end

    initial begin
        #150000;                                // glbl.GSR is released at 100 ns
        @(posedge clk); #1000;
        d = 1;
        @(posedge clk); #1000;
        `CHECK(q === 1'b1, "FDRE follows D after one clock")
        n = 1;
        while (srl_q !== 1'b1 && n < 10) begin
            @(posedge clk); #1000;
            n = n + 1;
        end
        `CHECK(n == 4, "SRL16E delays by four clocks")
        d = 0;

        i6 = 6'b111111; #10;
        `CHECK(lut_o === 1'b1, "LUT6 AND of six ones")
        i6 = 6'b111101; #10;
        `CHECK(lut_o === 1'b0, "LUT6 AND with a zero input")

        waddr = 5; wdata = 16'hBEEF; we = 1;
        @(posedge clk); #1000;
        we = 0; raddr = 5;
        @(posedge clk); #1000;
        @(posedge clk); #1000;
        `CHECK(rdata === 16'hBEEF, "RAMB18E1 reads back what was written")

        if (errors == 0) $display("PASSED");
        $finish;
    end
endmodule
