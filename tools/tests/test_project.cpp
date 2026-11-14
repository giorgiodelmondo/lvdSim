// SPDX-License-Identifier: GPL-3.0-or-later
#include "do_import.hpp"
#include "project.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace lvd::tools::proj;

namespace {

const char* kSample = R"(# a comment
[project]
name = demo
vhdl = 2002
library_root = libs
library_folder = C:/vendor libs
map = unisim "C:/x/unisim"
jobs = 4
timeout = 60
wave = on
coverage = yes
coverage_kinds = statement,branch

[library util]
path = out/util
file = src/pkg.vhd
file = "src/my file.vhd" vhdl=2008 relaxed
file = rtl/top.sv define=WIDTH=8 define=SIM incdir=inc

[library work]
file = tb/tb.vhd

[test smoke]
top = work.tb
generic = WIDTH=16#FF#
plusarg = verbose
do = sim/smoke.do
seeds = 1 2 3
seed_generic = SEED
timeout = 30
expect = ALL # PASSED
stop_time = 1 ms
wave = off
)";

} // namespace

TEST_CASE("project file: all keys are read", "[project]") {
    const ParseResult r = parseProject(kSample);
    REQUIRE(r.ok());
    CHECK(r.warnings.empty());
    const Project& p = r.project;
    CHECK(p.settings.name == "demo");
    CHECK(p.settings.vhdl == "02");
    CHECK(p.settings.libraryRoot == "libs");
    REQUIRE(p.settings.libraryFolders.size() == 1);
    CHECK(p.settings.libraryFolders[0] == "C:/vendor libs");
    REQUIRE(p.settings.mappings.size() == 1);
    CHECK(p.settings.mappings[0].second == "C:/x/unisim");
    CHECK(p.settings.jobs == 4);
    CHECK(p.settings.timeoutSec == 60);
    CHECK(p.settings.wave);
    CHECK(p.settings.coverage);
    REQUIRE(p.libraries.size() == 2);
    const Library& util = p.libraries[0];
    CHECK(util.path == "out/util");
    REQUIRE(util.files.size() == 3);
    CHECK(util.files[1].path == "src/my file.vhd");
    CHECK(util.files[1].vhdl == "08");
    CHECK(util.files[1].relaxed);
    CHECK(util.files[2].effectiveLanguage() == Language::SystemVerilog);
    CHECK(util.files[2].defines == std::vector<std::string>{"WIDTH=8", "SIM"});
    CHECK(util.files[2].incdirs == std::vector<std::string>{"inc"});
    CHECK(p.libraryPath(p.libraries[1]) == "libs/work");
    CHECK(p.vhdlOf(util.files[0]) == "02");
    REQUIRE(p.tests.size() == 1);
    const Test& t = p.tests[0];
    CHECK(t.generics == std::vector<std::string>{"WIDTH=16#FF#"}); // '#' inside a value is not a comment
    CHECK(t.plusargs == std::vector<std::string>{"+verbose"});
    CHECK(t.seeds == std::vector<std::uint64_t>{1, 2, 3});
    CHECK(t.expect == "ALL # PASSED");
    CHECK(t.timeoutSec == 30);
    CHECK(t.wave == false);
    CHECK_FALSE(t.coverage.has_value());
}

TEST_CASE("project file: write and read back gives the same project", "[project]") {
    const Project a = parseProject(kSample).project;
    const std::string text = writeProject(a);
    const ParseResult b = parseProject(text);
    REQUIRE(b.ok());
    CHECK(writeProject(b.project) == text);
    CHECK(b.project.libraries[0].files[1].path == "src/my file.vhd");
    CHECK(b.project.tests[0].expect == "ALL # PASSED");
}

TEST_CASE("project file: one line per file keeps diffs small", "[project]") {
    Project p = parseProject(kSample).project;
    const std::string before = writeProject(p);
    p.libraries[1].files.push_back({});
    p.libraries[1].files.back().path = "tb/extra.vhd";
    const std::string after = writeProject(p);
    // The new file is one inserted line, nothing else changes.
    CHECK(after.size() == before.size() + std::string("file = tb/extra.vhd\n").size());
}

TEST_CASE("project file: errors and warnings carry line numbers", "[project]") {
    const ParseResult r =
        parseProject("[project]\nvhdl = 1987\nfoo = 1\n[library a b]\n[library x]\nfile = a.vhd bogus\n"
                     "[test t]\n[weird]\nnot a key\n");
    CHECK_FALSE(r.ok());
    REQUIRE(r.warnings.size() == 1);
    CHECK(r.warnings[0].line == 3);
    std::vector<int> lines;
    for (const ParseMessage& m : r.errors)
        lines.push_back(m.line);
    CHECK(lines == std::vector<int>{2, 4, 6, 8, 9, 0}); // 0: test t has no top
    // Unknown keys are kept and written back.
    CHECK(writeProject(r.project).find("foo = 1") != std::string::npos);
}

TEST_CASE("project file: duplicate names are refused", "[project]") {
    CHECK_FALSE(parseProject("[library a]\n[library A]\n").ok());
    CHECK_FALSE(parseProject("[test t]\ntop=x\n[test t]\ntop=y\n").ok());
}

TEST_CASE("project file: helpers", "[project]") {
    CHECK(normaliseVhdlStd("-2008") == "08");
    CHECK(normaliseVhdlStd("93") == "93");
    CHECK(normaliseVhdlStd("2019") == "19");
    CHECK(normaliseVhdlStd("87").empty());
    CHECK(languageOf("a/b.SV") == Language::SystemVerilog);
    CHECK(languageOf("x.v") == Language::Verilog);
    CHECK(languageOf("x.vhdl") == Language::Vhdl);
    CHECK(isValidName("lib_1.x-y"));
    CHECK_FALSE(isValidName("a b"));
    CHECK_FALSE(isValidName(""));
}

TEST_CASE("Tcl splitting: quotes, braces, variables, continuation", "[import]") {
    const auto cmds = splitTclCommands(
        "set SRC ../rtl\n# comment ; vcom no\nvcom -work lib \\\n  $SRC/a.vhd \"${SRC}/b c.vhd\"; "
        "vlog {+define+A=1} [file join x y]\n",
        {});
    REQUIRE(cmds.size() == 3);
    CHECK(cmds[1] == std::vector<std::string>{"vcom", "-work", "lib", "../rtl/a.vhd", "../rtl/b c.vhd"});
    CHECK(cmds[2] == std::vector<std::string>{"vlog", "+define+A=1", "[file join x y]"});
}

TEST_CASE("import: a compile script becomes libraries, files and tests", "[import]") {
    const std::string script = R"(vlib work
vlib libs/util
vmap util libs/util
vcom -2008 -work util src/pkg.vhd src/pkg2.vhd
vcom -93 -work util src/old.vhd
vcom -2008 -explicit src/tb.vhd
vlog -sv +define+W=8+X +incdir+inc -work work rtl/a.v rtl/b.sv
do more.do
vsim -c -do "run -all; quit" -gN=3 work.tb
vsim -c -do sim.do -G/tb/M=1 tb
mystery 1 2
)";
    ImportOptions o;
    o.scriptDir = "/p/scripts";
    o.projectDir = "/p";
    o.iniText = "[Library]\nunisim = /vendor/unisim\nutil = /elsewhere/util\n";
    o.readFile = [](const std::string& f) -> std::optional<std::string> {
        if (f == "/p/scripts/more.do")
            return std::string("vcom -2008 src/extra.vhd\n");
        if (f == "/p/scripts/sim.do")
            return std::string("run -all\n");
        return std::nullopt;
    };
    const ImportResult r = importDoScript(script, o);
    const Project& p = r.project;
    REQUIRE(p.libraries.size() == 2);
    CHECK(p.libraries[0].name == "work");
    CHECK(p.libraries[0].path == "scripts/work");
    CHECK(p.libraries[1].name == "util");
    CHECK(p.libraries[1].path == "scripts/libs/util"); // vmap beats the ini
    REQUIRE(p.libraries[1].files.size() == 3);
    CHECK(p.libraries[1].files[0].path == "scripts/src/pkg.vhd");
    CHECK(p.settings.vhdl == "08");
    CHECK(p.libraries[1].files[0].vhdl.empty());
    CHECK(p.libraries[1].files[2].vhdl == "93");
    const auto& work = p.libraries[0].files;
    REQUIRE(work.size() == 4);
    CHECK(work[0].relaxed);
    CHECK(work[1].effectiveLanguage() == Language::SystemVerilog); // -sv on a .v file
    CHECK(work[1].defines == std::vector<std::string>{"W=8", "X"});
    CHECK(work[1].incdirs == std::vector<std::string>{"scripts/inc"});
    CHECK(work[3].path == "scripts/src/extra.vhd"); // from more.do
    REQUIRE(p.settings.mappings.size() == 1);
    CHECK(p.settings.mappings[0].first == "unisim");
    REQUIRE(p.tests.size() == 2);
    CHECK(p.tests[0].name == "tb");
    CHECK(p.tests[0].top == "work.tb");
    CHECK(p.tests[0].commands == "run -all; quit");
    CHECK(p.tests[0].generics == std::vector<std::string>{"N=3"});
    CHECK(p.tests[1].name == "tb_2");
    CHECK(p.tests[1].doScript == "scripts/sim.do");
    CHECK(p.tests[1].generics == std::vector<std::string>{"/tb/M=1"});
    bool warned = false;
    for (const std::string& w : r.warnings)
        warned = warned || w.find("mystery") != std::string::npos;
    CHECK(warned);
}
