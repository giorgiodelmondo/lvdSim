// SPDX-License-Identifier: GPL-3.0-or-later
// Unit scanner, dependency graph, analysis order, incremental plan, compile state and nvc message parsing.
#include "compile_plan.hpp"
#include "diagnostics.hpp"
#include "hdl_scanner.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace lvd::tools;
using namespace lvd::tools::build;

namespace {

bool hasRef(const scan::ScanResult& r, const std::string& lib, const std::string& name) {
    for (const scan::UnitRef& x : r.refs)
        if (x.library == lib && x.name == name)
            return true;
    return false;
}

} // namespace

TEST_CASE("VHDL scanner: units and references", "[scan]") {
    const auto r = scan::scanVhdl(R"(
library ieee, mylib; use ieee.std_logic_1164.all;
use mylib.Pkg_A.all;   -- comment: use mylib.not_this.all;
context mylib.ctx;
/* block comment entity fake is */
entity Top is port (s : in bit); end entity;
architecture rtl of top is
  signal c : character := ';';
  constant t : string := "entity nope is";
begin
  u1 : entity work.leaf(rtl) port map (s => s);
  u2 : component comp_b generic map (1) port map (s);
  u3 : comp_c port map (s);
  x <= s'event and work.consts.K = 1 when '1' = '1';
end architecture;
package body pkg_b is end package body;
configuration cfg of top is for rtl end for; end configuration;
)");
    REQUIRE(r.units.size() == 4);
    CHECK(r.units[0].kind == scan::UnitKind::Entity);
    CHECK(r.units[0].name == "top");
    CHECK(r.units[1].kind == scan::UnitKind::Architecture);
    CHECK(r.units[1].primary == "top");
    CHECK(r.units[2].kind == scan::UnitKind::PackageBody);
    CHECK(r.units[3].kind == scan::UnitKind::Configuration);
    CHECK(hasRef(r, "ieee", "std_logic_1164"));
    CHECK(hasRef(r, "mylib", "pkg_a"));
    CHECK_FALSE(hasRef(r, "mylib", "not_this"));
    CHECK(hasRef(r, "mylib", "ctx"));
    CHECK(hasRef(r, "work", "leaf"));
    CHECK(hasRef(r, "work", "consts"));
    CHECK(hasRef(r, "", "comp_b"));
    CHECK(hasRef(r, "", "comp_c"));
    CHECK(hasRef(r, "work", "pkg_b"));     // the body needs its package
    CHECK_FALSE(hasRef(r, "work", "top")); // declared in the same file
    for (const auto& u : r.units)
        CHECK(u.name != "fake");
}

TEST_CASE("Verilog scanner: modules, packages, instances, includes", "[scan]") {
    const auto r = scan::scanVerilog(R"V(
`include "defs.vh"
`define MAC(x) sub x ();
package my_pkg; endpackage
module top #(parameter W = 8) (input wire clk);
  import util_pkg::*;
  // leaf commented (.a(1));
  leaf #(.W(W)) u_leaf (.clk(clk));
  other u_other [3:0] (.clk(clk));
  wire [3:0] q;
  assign q = cfg_pkg::DEFAULT;
  always @(posedge clk) $display("leaf2 u2 (x)");
endmodule
interface bus_if; endinterface
)V");
    REQUIRE(r.units.size() == 3);
    CHECK(r.units[0].kind == scan::UnitKind::VerilogPackage);
    CHECK(r.units[1].name == "top");
    CHECK(r.units[2].name == "bus_if");
    CHECK(r.includes == std::vector<std::string>{"defs.vh"});
    CHECK(hasRef(r, "", "util_pkg"));
    CHECK(hasRef(r, "", "cfg_pkg"));
    CHECK(hasRef(r, "", "leaf"));
    CHECK(hasRef(r, "", "other"));
    CHECK_FALSE(hasRef(r, "", "leaf2"));
    CHECK_FALSE(hasRef(r, "", "sub"));
    CHECK_FALSE(hasRef(r, "", "wire"));
}

namespace {

// Three libraries: base (pkg, unrelated), mid (uses base.pkg), work (tb uses mid.core; comp instance of
// "helper" from base).
proj::Project threeLibs() {
    proj::Project p;
    auto lib = [&](const char* name, std::vector<const char*> files) {
        proj::Library l;
        l.name = name;
        for (const char* f : files) {
            l.files.push_back({});
            l.files.back().path = f;
        }
        p.libraries.push_back(l);
    };
    lib("work", {"tb.vhd"}); // listed first on purpose: the order comes from the dependencies
    lib("mid", {"core.vhd"});
    lib("base", {"pkg.vhd", "unrelated.vhd", "helper.vhd"});
    return p;
}

std::vector<scan::ScanResult> threeLibScans() {
    return {
        scan::scanVhdl("library mid; entity tb is end; architecture a of tb is begin d: entity mid.core; "
                       "h : helper port map (x); end;"),
        scan::scanVhdl(
            "library base; use base.pkg.all; entity core is end; architecture a of core is begin end;"),
        scan::scanVhdl("package pkg is end package;"),
        scan::scanVhdl("package unrelated is end package;"),
        scan::scanVhdl("entity helper is end; architecture a of helper is begin end;"),
    };
}

std::vector<std::optional<FileStamp>> stamps(const proj::Project& p, std::vector<std::uint64_t> hashes) {
    std::vector<std::optional<FileStamp>> out;
    const auto files = projectFiles(p);
    for (std::size_t i = 0; i < files.size(); ++i) {
        FileStamp s;
        s.hash = hashes[i];
        s.optionsHash = optionsHash(p, files[i]);
        out.push_back(s);
    }
    return out;
}

State stateOf(const proj::Project& p, const std::vector<std::optional<FileStamp>>& st) {
    State s;
    const auto files = projectFiles(p);
    for (std::size_t i = 0; i < files.size(); ++i)
        s.files[fileKey(p, files[i])] = *st[i];
    return s;
}

std::vector<int> stepFiles(const Plan& plan) {
    std::vector<int> v;
    for (const Step& s : plan.steps)
        v.push_back(s.file);
    return v;
}

} // namespace

TEST_CASE("dependency graph and analysis order across libraries", "[build]") {
    const proj::Project p = threeLibs();
    Graph g = buildGraph(p, threeLibScans());
    // files: 0 tb, 1 core, 2 pkg, 3 unrelated, 4 helper
    CHECK(g.deps[0] == std::vector<int>{1, 4});
    CHECK(g.deps[1] == std::vector<int>{2});
    CHECK(g.deps[2].empty());
    CHECK(g.fileOfUnit("BASE", "Pkg") == 2);
    const std::vector<int> order = compileOrder(g);
    CHECK(order == std::vector<int>{2, 1, 3, 4, 0});
    CHECK(g.warnings.empty());
    CHECK(withDependents(g, {2}) == std::set<int>{0, 1, 2});
}

TEST_CASE("incremental plan: only changed files and their dependents", "[build]") {
    const proj::Project p = threeLibs();
    Graph g = buildGraph(p, threeLibScans());
    const auto before = stamps(p, {1, 2, 3, 4, 5});
    const State state = stateOf(p, before);
    const std::vector<bool> libs(3, true);

    SECTION("nothing changed") {
        CHECK(planCompile(p, g, state, before, libs, Mode::OutOfDate).steps.empty());
    }
    SECTION("the package changed: it, core and tb, in that order") {
        const Plan plan = planCompile(p, g, state, stamps(p, {1, 2, 30, 4, 5}), libs, Mode::OutOfDate);
        CHECK(stepFiles(plan) == std::vector<int>{2, 1, 0});
        CHECK(plan.steps[0].reason == Reason::Changed);
        CHECK(plan.steps[1].reason == Reason::Dependency);
        CHECK(plan.steps[1].cause == 2);
    }
    SECTION("a file nothing depends on") {
        CHECK(stepFiles(planCompile(p, g, state, stamps(p, {1, 2, 3, 40, 5}), libs, Mode::OutOfDate)) ==
              std::vector<int>{3});
    }
    SECTION("options changed") {
        proj::Project q = p;
        q.libraries[2].files[1].vhdl = "93";
        Graph gq = buildGraph(q, threeLibScans());
        const Plan plan = planCompile(q, gq, state, stamps(q, {1, 2, 3, 4, 5}), libs, Mode::OutOfDate);
        REQUIRE(plan.steps.size() == 1);
        CHECK(plan.steps[0].reason == Reason::Options);
    }
    SECTION("failed last time, new file, missing library, unreadable file") {
        State s = state;
        s.files[fileKey(p, {2, 2})].ok = false; // helper
        s.files.erase(fileKey(p, {2, 1}));      // unrelated
        Plan plan = planCompile(p, g, s, before, libs, Mode::OutOfDate);
        CHECK(stepFiles(plan) == std::vector<int>{3, 4, 0});
        CHECK(plan.steps[0].reason == Reason::New);
        CHECK(plan.steps[1].reason == Reason::Failed);

        auto cur = before;
        cur[1].reset();
        plan = planCompile(p, g, state, cur, libs, Mode::OutOfDate);
        CHECK(stepFiles(plan) == std::vector<int>{1, 0});

        plan = planCompile(p, g, state, before, {true, false, true}, Mode::OutOfDate);
        CHECK(stepFiles(plan) == std::vector<int>{1, 0});
        CHECK(plan.steps[0].reason == Reason::LibraryMissing);
    }
    SECTION("all and selected") {
        CHECK(planCompile(p, g, state, before, libs, Mode::All).steps.size() == 5);
        CHECK(stepFiles(planCompile(p, g, state, before, libs, Mode::Selected, {4})) ==
              std::vector<int>{4, 0});
    }
}

TEST_CASE("dependency cycles keep project order and warn", "[build]") {
    proj::Project p;
    p.libraries.push_back({});
    p.libraries[0].name = "work";
    for (const char* f : {"a.vhd", "b.vhd"}) {
        p.libraries[0].files.push_back({});
        p.libraries[0].files.back().path = f;
    }
    Graph g = buildGraph(p, {scan::scanVhdl("package a is end; use work.b.all;"),
                             scan::scanVhdl("package b is end; use work.a.all;")});
    CHECK(compileOrder(g) == std::vector<int>{0, 1});
    CHECK_FALSE(g.warnings.empty());
}

TEST_CASE("compile state round trip", "[build]") {
    State s;
    s.files["lib|a b.vhd"] = {123456789012345LL, 77, 0xffffffffffffffffULL, 42, true};
    s.files["work|x.v"] = {-5, 0, 1, 2, false};
    const State r = parseState(writeState(s) + "garbage line\n");
    CHECK(r.files == s.files);
}

TEST_CASE("nvc messages: analysis errors with location and column", "[diag]") {
    const std::string out = "** Error: type of string literal cannot be determined\n"
                            "   > src/bad.vhd:3\n"
                            "   |\n"
                            " 3 |   signal s : integer := \"x\";\n"
                            "   |                         ^^^\n"
                            "   |\n"
                            "   = Note: context contains type INTEGER\n"
                            "** Warning: C:/w/x.v:12:4: something\n"
                            "** Error: no visible declaration for FOO\n"
                            "   > C:/proj/bad.vhd:5\n"
                            "   |\n"
                            " 5 |   foo <= a ^ b;\n"
                            "   |   ^^^ did you mean NOW?\n"
                            "other output\n"
                            "** Failure: 10ns+0: boom\n"
                            "   Process :tb:_p0 at src/tb.vhd:8\n";
    const auto d = diag::parse(out);
    REQUIRE(d.size() == 4);
    CHECK(d[0].severity == diag::Severity::Error);
    CHECK(d[0].file == "src/bad.vhd");
    CHECK(d[0].line == 3);
    CHECK(d[0].column == 25);
    CHECK(d[1].severity == diag::Severity::Warning);
    CHECK(d[2].file == "C:/proj/bad.vhd");
    CHECK(d[2].line == 5);
    CHECK(d[2].column == 3);
    CHECK(d[3].severity == diag::Severity::Failure);
    CHECK(d[3].file == "src/tb.vhd");
    CHECK(d[3].line == 8);
    const diag::Counts c = diag::count(out + "** Fatal: x\n** Note: y\n");
    CHECK(c.errors == 2);
    CHECK(c.failures == 1);
    CHECK(c.fatals == 1);
    CHECK(c.warnings == 1);
    CHECK(c.notes == 1);
    CHECK(c.problems() == 4);
}
