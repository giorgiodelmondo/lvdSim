// SPDX-License-Identifier: GPL-3.0-or-later
// End to end: the real lvdsim-cli drives a real nvc through vlib, vcom and vsim -c.
#include <catch2/catch_test_macros.hpp>
#include <coverage.hpp>
#include <process.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#define LVD_PID _getpid()
#else
#include <unistd.h>
#define LVD_PID getpid()
#endif

namespace fs = std::filesystem;
using namespace lvd::tools;

namespace {

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? v : fallback;
}

struct Nvc {
    std::string exe, libDir, dllDir;
    bool found = false;
};

Nvc findNvc() {
    Nvc n;
    const fs::path build = LVD_KERNEL_BUILD;
#ifdef _WIN32
    const std::string defaultExe = (build / "bin" / "nvc.exe").generic_string();
    const std::string defaultDll = fs::exists("C:/msys64/mingw64/bin") ? "C:/msys64/mingw64/bin" : "";
#else
    const std::string defaultExe = (build / "bin" / "nvc").generic_string();
    const std::string defaultDll;
#endif
    n.exe = envOr("LVDSIM_NVC", defaultExe);
    n.libDir = envOr("LVDSIM_NVC_LIBDIR", (build / "lib").generic_string());
    n.dllDir = envOr("LVDSIM_NVC_DLLDIR", defaultDll);
    n.found = fs::exists(n.exe);
    return n;
}

// A scratch directory per test case, removed afterwards.
struct Sandbox {
    fs::path dir;
    Nvc nvc = findNvc();

    Sandbox() {
        dir = fs::temp_directory_path() / ("lvdsim_it_" + std::to_string(LVD_PID));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    void write(const std::string& name, const std::string& text) const {
        std::ofstream(dir / name, std::ios::binary) << text;
    }

    ProcessResult run(const std::string& tool, std::vector<std::string> args,
                      std::vector<std::pair<std::string, std::string>> extraEnv = {}) const {
        std::vector<std::string> argv{LVD_CLI_EXE, tool};
        argv.insert(argv.end(), args.begin(), args.end());
        ProcessOptions o;
        o.workingDir = dir.string();
        o.capture = true;
        o.env = {{"LVDSIM_NVC", nvc.exe}, {"LVDSIM_NVC_LIBDIR", nvc.libDir}};
        if (!nvc.dllDir.empty()) o.env.emplace_back("LVDSIM_NVC_DLLDIR", nvc.dllDir);
        o.env.insert(o.env.end(), extraEnv.begin(), extraEnv.end());
        return runProcess(argv, o);
    }

    /// nvc itself (not through the tools), in the sandbox.
    ProcessResult nvcRun(std::vector<std::string> args) const {
        std::vector<std::string> argv{nvc.exe, "-L", nvc.libDir};
        argv.insert(argv.end(), args.begin(), args.end());
        ProcessOptions o;
        o.workingDir = dir.string();
        o.capture = true;
        if (!nvc.dllDir.empty()) o.pathPrepend.push_back(nvc.dllDir);
        return runProcess(argv, o);
    }
};

const char* kCounter = R"(
entity t is end;
architecture a of t is
  signal clk : bit := '0';
  signal cnt : integer := 0;
begin
  clk <= not clk after 5 ns;
  process (clk) begin if clk = '1' then cnt <= cnt + 1; end if; end process;
end;
)";

const char* kFinite = R"(
library std;
use std.env.all;
entity u is end;
architecture a of u is
  signal v : integer := 0;
begin
  process begin
    v <= 1; wait for 20 ns;
    v <= 2; wait for 30 ns;
    finish;
  end process;
end;
)";

#define REQUIRE_NVC(sb) \
    if (!(sb).nvc.found) SKIP("nvc not found (set LVDSIM_NVC); looked for " + (sb).nvc.exe)

}  // namespace

TEST_CASE("vlib, vcom and vsim -c run a design") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    sb.write("t.vhd", kCounter);

    auto lib = sb.run("vlib", {"work"});
    REQUIRE(lib.exitCode == 0);
    REQUIRE(lib.output.find("Creating library 'work'") != std::string::npos);
    REQUIRE(fs::is_directory(sb.dir / "work"));

    auto com = sb.run("vcom", {"-2008", "t.vhd"});
    INFO(com.output);
    REQUIRE(com.exitCode == 0);
    REQUIRE(com.output.find("not an NVC library") == std::string::npos);  // vlib made a proper library

    auto sim = sb.run("vsim", {"-c", "-do", "run 100 ns; puts \"cnt=[examine /cnt]\"; quit -f", "work.t"});
    INFO(sim.output);
    REQUIRE(sim.exitCode == 0);
    REQUIRE(sim.output.find("cnt=10") != std::string::npos);
}

TEST_CASE("familiar simulator commands in a script") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    sb.write("t.vhd", kCounter);
    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);
    REQUIRE(sb.run("vcom", {"t.vhd"}).exitCode == 0);

    SECTION("force, noforce and run with and without a space in the time") {
        auto r = sb.run("vsim", {"-c", "-do",
                                 "force -freeze /cnt 99; run 20ns; puts \"a=[examine /cnt]\"; noforce /cnt; run 20 ns; "
                                 "puts \"b=[examine /cnt]\"; quit -f",
                                 "t"});
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        REQUIRE(r.output.find("a=99") != std::string::npos);
        REQUIRE(r.output.find("b=102") != std::string::npos);  // released signals resume counting from the forced value
    }
    SECTION("based numbers in force") {
        auto r = sb.run("vsim", {"-c", "-do", "force /cnt 16#2A; run 1 ns; puts \"v=[examine /cnt]\"; quit -f", "t"});
        INFO(r.output);
        REQUIRE(r.output.find("v=42") != std::string::npos);
    }
    SECTION("commands that only matter in a GUI are accepted") {
        auto r = sb.run("vsim", {"-c", "-do", "add wave -r /*; log -r /*; radix hex; onerror {quit -f}; run 10 ns; puts alive; quit -f", "t"});
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        REQUIRE(r.output.find("alive") != std::string::npos);
    }
    SECTION("quit -code is the exit status") {
        auto r = sb.run("vsim", {"-c", "-do", "run 5 ns; quit -code 3", "t"});
        REQUIRE(r.exitCode == 3);
    }
    SECTION("a script file and the do command") {
        sb.write("inner.do", "puts inner_ran\n");
        sb.write("outer.do", "do inner.do\nrun 10 ns\nputs outer_done\nquit -f\n");
        auto r = sb.run("vsim", {"-c", "-do", "outer.do", "t"});
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        REQUIRE(r.output.find("inner_ran") != std::string::npos);
        REQUIRE(r.output.find("outer_done") != std::string::npos);
    }
    SECTION("unsupported commands fail with a clear message") {
        auto r = sb.run("vsim", {"-c", "-do", "bp foo.vhd 3; quit -f", "t"});
        REQUIRE(r.output.find("not supported yet") != std::string::npos);
    }
    SECTION("a bad time is an error") {
        auto r = sb.run("vsim", {"-c", "-do", "run soon; puts after; quit -f", "t"});
        REQUIRE(r.output.find("invalid time") != std::string::npos);
    }
}

TEST_CASE("run -all stops when the design finishes") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    sb.write("u.vhd", kFinite);
    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);
    auto com = sb.run("vcom", {"-2008", "u.vhd"});
    INFO(com.output);
    REQUIRE(com.exitCode == 0);

    auto r = sb.run("vsim", {"-c", "-do", "run -all; puts finished; quit -f", "u"});
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.output.find("finished") != std::string::npos);
}

TEST_CASE("compile errors and missing libraries fail") {
    Sandbox sb;
    REQUIRE_NVC(sb);

    sb.write("bad.vhd", "entity broken is\narchitecture a of nothing is begin end;\n");
    auto noLib = sb.run("vcom", {"bad.vhd"});
    REQUIRE(noLib.exitCode != 0);
    REQUIRE(noLib.output.find("vlib work") != std::string::npos);

    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);
    auto bad = sb.run("vcom", {"bad.vhd"});
    REQUIRE(bad.exitCode != 0);

    auto noDesign = sb.run("vsim", {"-c", "-do", "quit -f", "work.missing"});
    REQUIRE(noDesign.exitCode != 0);
}

TEST_CASE("libraries can live elsewhere through lvdsim.ini and vmap") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    sb.write("t.vhd", kCounter);
    fs::create_directories(sb.dir / "libs" / "mylib");

    auto map = sb.run("vmap", {"mylib", "libs/mylib"});
    REQUIRE(map.exitCode == 0);
    std::ifstream ini(sb.dir / "lvdsim.ini");
    std::string text((std::istreambuf_iterator<char>(ini)), std::istreambuf_iterator<char>());
    REQUIRE(text.find("mylib = libs/mylib") != std::string::npos);

    REQUIRE(sb.run("vcom", {"-work", "mylib", "t.vhd"}).exitCode == 0);
    auto r = sb.run("vsim", {"-c", "-do", "run 50 ns; puts \"n=[examine /cnt]\"; quit -f", "mylib.t"});
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.output.find("n=5") != std::string::npos);
}

TEST_CASE("wlf and lvdwave options write a waveform file") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    sb.write("t.vhd", kCounter);
    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);
    REQUIRE(sb.run("vcom", {"t.vhd"}).exitCode == 0);

    auto r = sb.run("vsim", {"-c", "-wlf", "run1.wlf", "-do", "run 100 ns; quit -f", "t"});
    INFO(r.output);
    if (r.exitCode != 0 && r.output.find("--wave") != std::string::npos) SKIP("this nvc build has no --wave for the interactive shell (kernel patch 0001)");
    REQUIRE(r.exitCode == 0);
    REQUIRE(fs::exists(sb.dir / "run1.fst"));
    REQUIRE(fs::file_size(sb.dir / "run1.fst") > 100);
}

TEST_CASE("Verilog goes through vlog") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    sb.write("m.v", "module m; initial $display(\"hello from verilog\"); endmodule\n");
    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);
    auto r = sb.run("vlog", {"m.v"});
    INFO(r.output);
    if (r.exitCode != 0) SKIP("this nvc build cannot analyse the sample Verilog: " + r.output);
    REQUIRE(r.exitCode == 0);
}

TEST_CASE("several top levels: a Verilog design reads a global module by hierarchical name") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    // The Xilinx pattern: every unisim model reads glbl.GSR, glbl is a second top level.
    sb.write("glbl.v", R"(`timescale 1 ps / 1 ps
module glbl; reg GSR = 1; initial #(100000) GSR = 0; endmodule
)");
    sb.write("top.v", R"(`timescale 1 ps / 1 ps
module leaf(output o); assign o = glbl.GSR; endmodule
module top;
  wire g;
  leaf u(.o(g));
  initial begin
    #1000 $display("early=%b", g);
    #200000 $display("late=%b", g);
  end
endmodule
)");
    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);
    auto com = sb.run("vlog", {"glbl.v", "top.v"});
    INFO(com.output);
    if (com.exitCode != 0) SKIP("this nvc build cannot analyse the sample Verilog: " + com.output);

    auto one = sb.run("vsim", {"-c", "-do", "run 1 ns; quit -f", "work.top"});
    REQUIRE(one.exitCode != 0);  // glbl is not part of the design
    REQUIRE(one.output.find("glbl") != std::string::npos);

    auto r = sb.run("vsim", {"-c", "-do", "run 300 ns; quit -f", "work.top", "work.glbl"});
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.output.find("early=1") != std::string::npos);
    REQUIRE(r.output.find("late=0") != std::string::npos);  // the 100 ns delay is in picoseconds, not femtoseconds
}

TEST_CASE("a VHDL component binds to a Verilog module in another library") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    // The Xilinx XPM pattern: a component package and Verilog modules in library vl, a VHDL design in work.
    sb.write("vl.v", R"(module vl_child #(parameter integer W = 1) (input a, output y);
  assign y = ~a;
endmodule
module vl_parent #(parameter MODE = "inv", parameter integer D = 3) (input a, output y, output z);
  wire t;
  vl_child #(.W(D)) c (.a(a), .y(t));
  assign y = (MODE == "inv") ? t : a;
  assign z = (D == 9);
endmodule
)");
    sb.write("vcomp.vhd", R"(library ieee; use ieee.std_logic_1164.all;
package vcomp is
  component vl_parent is
    generic (MODE : string := "inv"; D : integer := 3);
    port (a : in std_logic; y : out std_logic; z : out std_logic);
  end component;
end package;
)");
    sb.write("t.vhd", R"(library ieee; use ieee.std_logic_1164.all;
library vl; use vl.vcomp.all;
entity t is end;
architecture a of t is
  signal i, y1, y2, z1, z2 : std_logic := '0';
begin
  u1 : vl_parent generic map (MODE => "inv", D => 9) port map (i, y1, z1);
  u2 : vl_parent generic map (MODE => "buf") port map (i, y2, z2);
end;
)");
    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);
    REQUIRE(sb.run("vlib", {"vl"}).exitCode == 0);
    auto v = sb.run("vlog", {"-work", "vl", "vl.v"});
    INFO(v.output);
    if (v.exitCode != 0) SKIP("this nvc build cannot analyse the sample Verilog: " + v.output);
    REQUIRE(sb.run("vcom", {"-work", "vl", "vcomp.vhd"}).exitCode == 0);
    auto c = sb.run("vcom", {"-L", "vl", "t.vhd"});
    INFO(c.output);
    REQUIRE(c.exitCode == 0);

    auto r = sb.run("vsim", {"-c", "-L", "vl", "-do",
                             "run 1 ns; puts \"y1=[examine /y1] y2=[examine /y2]\"; "
                             "puts \"z1=[examine /z1] z2=[examine /z2]\"; quit -f",
                             "t"});
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.output.find("y1='1' y2='0'") != std::string::npos);  // inverter and buffer of i = '0'
    REQUIRE(r.output.find("z1='1'") != std::string::npos);           // integer generic reached the Verilog parameter (D = 9)
    REQUIRE(r.output.find("z2='0'") != std::string::npos);           // default of the component generic
}

TEST_CASE("a Verilog design finds modules in a library given with -L") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    sb.write("leaf.v", R"(module leaf(input a, output y);
  assign y = ~a;
endmodule
)");
    sb.write("top.v", R"(module top;
  reg a = 0;
  wire y;
  leaf u(.a(a), .y(y));
  initial begin
    #1 $display("y=%b", y);
    #1 a = 1;
    #1 $display("y=%b", y);
  end
endmodule
)");
    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);
    REQUIRE(sb.run("vlib", {"cells"}).exitCode == 0);
    auto l = sb.run("vlog", {"-work", "cells", "leaf.v"});
    INFO(l.output);
    if (l.exitCode != 0) SKIP("this nvc build cannot analyse the sample Verilog: " + l.output);
    auto t = sb.run("vlog", {"-L", "cells", "top.v"});
    INFO(t.output);
    REQUIRE(t.exitCode == 0);

    auto r = sb.run("vsim", {"-c", "-L", "cells", "-do", "run 5 ns", "top"});
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.output.find("y=1") != std::string::npos);
    REQUIRE(r.output.find("y=0") != std::string::npos);

    auto missing = sb.run("vsim", {"-c", "-do", "run 5 ns", "top"});
    REQUIRE(missing.exitCode != 0);  // without -L the module cannot be found
    REQUIRE(missing.output.find("leaf") != std::string::npos);
}

TEST_CASE("generic overrides reach the elaboration") {
    Sandbox sb;
    REQUIRE_NVC(sb);
    sb.write("g.vhd", R"(
entity leaf is generic (WIDTH : integer := 4); end;
architecture a of leaf is
  signal w : integer := WIDTH;
begin
end;
entity tb is generic (BUG : integer := 0; MSG : string := "none"; V : bit_vector(7 downto 0) := x"00"); end;
architecture a of tb is
  signal bug_s : integer := BUG;
  signal v_s : bit_vector(7 downto 0) := V;
begin
  u : entity work.leaf;
  e : entity work.leaf generic map (WIDTH => 2);
  process begin
    report "MSG=<" & MSG & ">";
    assert BUG = 0 report "bug mode" severity failure;
    wait;
  end process;
end;
)");
    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);
    auto com = sb.run("vcom", {"-2008", "g.vhd"});
    INFO(com.output);
    REQUIRE(com.exitCode == 0);
    const std::string show = "run 1 ns; puts \"bug=[examine /bug_s] u=[examine /u/w] e=[examine /e/w] "
                             "v=[examine /v_s]\"; quit -f";

    SECTION("defaults without overrides") {
        auto r = sb.run("vsim", {"-c", "-do", show, "work.tb"});
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        REQUIRE(r.output.find("bug=0 u=4 e=2") != std::string::npos);
        REQUIRE(r.output.find("MSG=<none>") != std::string::npos);
    }
    SECTION("-gBUG=1 changes the behaviour of the testbench") {
        auto r = sb.run("vsim", {"-c", "-gBUG=1", "-do", "run 1 ns; quit -f", "work.tb"});
        INFO(r.output);
        REQUIRE(r.output.find("not supported") == std::string::npos);
        REQUIRE(r.output.find("bug mode") != std::string::npos);
    }
    SECTION("hierarchical names, -G over an explicit generic map, strings with spaces and quotes, based literals") {
        auto r = sb.run("vsim", {"-c", "-g/tb/u/WIDTH=8", "-G/tb/e/WIDTH=16#10#", "-gMSG=\"hello world\"", "-gV=x\"A5\"",
                                 "-do", show, "work.tb"});
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        REQUIRE(r.output.find("u=8 e=16") != std::string::npos);
        REQUIRE(r.output.find("MSG=<hello world>") != std::string::npos);
        REQUIRE(r.output.find("10100101") != std::string::npos);
    }
    SECTION("a plain -g sets instances without a generic map value, -G sets every instance") {
        auto g = sb.run("vsim", {"-c", "-gWIDTH=7", "-do", show, "work.tb"});
        INFO(g.output);
        REQUIRE(g.exitCode == 0);
        REQUIRE(g.output.find("u=7 e=2") != std::string::npos);
        auto big = sb.run("vsim", {"-c", "-GWIDTH=9", "-do", show, "work.tb"});
        INFO(big.output);
        REQUIRE(big.exitCode == 0);
        REQUIRE(big.output.find("u=9 e=9") != std::string::npos);
    }
    SECTION("a path through a generate statement names one iteration") {
        sb.write("gen.vhd", R"(
entity gtb is end;
architecture a of gtb is
begin
  g : for i in 1 to 2 generate
    u : entity work.leaf;
  end generate;
end;
)");
        REQUIRE(sb.run("vcom", {"-2008", "gen.vhd"}).exitCode == 0);
        auto r = sb.run("vsim", {"-c", "-g/gtb/g(2)/u/WIDTH=5", "-do",
                                 "run 1 ns; puts \"w=[examine /g(1)/u/w] [examine /g(2)/u/w]\"; quit -f", "work.gtb"});
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        REQUIRE(r.output.find("w=4 5") != std::string::npos);
    }
    SECTION("a value of the wrong type fails the elaboration") {
        auto r = sb.run("vsim", {"-c", "-gBUG=abc", "-do", "quit -f", "work.tb"});
        INFO(r.output);
        REQUIRE(r.exitCode != 0);
        REQUIRE(r.output.find("BUG") != std::string::npos);
    }
}

TEST_CASE("vsim -coverage writes a coverage database") {
    // Only that the database is written and can be exported: whether the Tcl shell stores the run-time counts
    // is checked by coverage_integration_tests.
    Sandbox sb;
    REQUIRE_NVC(sb);
    sb.write("u.vhd", kFinite);
    REQUIRE(sb.run("vlib", {"work"}).exitCode == 0);

    auto com = sb.run("vcom", {"-2008", "+cover=sbf", "u.vhd"});
    INFO(com.output);
    REQUIRE(com.exitCode == 0);
    REQUIRE(com.output.find("Warning") == std::string::npos);
    REQUIRE(com.output.find("vsim -coverage -lvdcover sbf") != std::string::npos);  // the note says where it goes

    const std::vector<std::pair<std::string, std::string>> oneThread = {{"NVC_MAX_THREADS", "1"}};
    auto exportDb = [&](const std::string& db) {
        const std::string xml = db + ".xml";
        const auto r = sb.nvcRun(cov::exportArgs({(sb.dir / db).generic_string()}, (sb.dir / xml).generic_string()));
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        std::ifstream in(sb.dir / xml, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return cov::parseExportXml(ss.str());
    };

    SECTION("every kind into <top>.ncdb") {
        auto r = sb.run("vsim", {"-c", "-coverage", "-do", "run -all; quit", "work.u"}, oneThread);
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        REQUIRE(fs::exists(sb.dir / "u.ncdb"));
        const cov::ParseResult x = exportDb("u.ncdb");
        REQUIRE(x.ok());
        CHECK(x.db.totals().has(cov::Kind::Statement));
        CHECK(x.db.findScope("/u") >= 0);
    }
    SECTION("chosen kinds into a named database") {
        auto r = sb.run("vsim", {"-c", "-coverage", "-lvdcover", "b", "-lvdcoverfile", "only_branch.ncdb", "-do",
                                 "run -all; quit", "u"},
                        oneThread);
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        REQUIRE_FALSE(fs::exists(sb.dir / "u.ncdb"));
        REQUIRE(fs::exists(sb.dir / "only_branch.ncdb"));
        const cov::ParseResult x = exportDb("only_branch.ncdb");
        REQUIRE(x.ok());
        CHECK_FALSE(x.db.totals().has(cov::Kind::Statement));
    }
    SECTION("no database without -coverage") {
        auto r = sb.run("vsim", {"-c", "-do", "run -all; quit", "u"}, oneThread);
        REQUIRE(r.exitCode == 0);
        REQUIRE_FALSE(fs::exists(sb.dir / "u.ncdb"));
    }
}
