// SPDX-License-Identifier: GPL-3.0-or-later
// Coverage end to end with the real nvc: collect (elaboration options from lvd::tools::cov), export, merge
// and report through the nvc binary, then read the export. Skipped when nvc is not built.
#include <catch2/catch_test_macros.hpp>
#include <coverage.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <process.hpp>
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
    n.exe = envOr("LVDSIM_NVC", (build / "bin" / "nvc.exe").generic_string());
    n.dllDir = envOr("LVDSIM_NVC_DLLDIR", fs::exists("C:/msys64/mingw64/bin") ? "C:/msys64/mingw64/bin" : "");
#else
    n.exe = envOr("LVDSIM_NVC", (build / "bin" / "nvc").generic_string());
    n.dllDir = envOr("LVDSIM_NVC_DLLDIR", "");
#endif
    n.libDir = envOr("LVDSIM_NVC_LIBDIR", (build / "lib").generic_string());
    n.found = fs::exists(n.exe);
    return n;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The design analysed once into a scratch directory, removed at the end.
struct Sandbox {
    fs::path dir;
    Nvc nvc = findNvc();

    Sandbox() {
        dir = fs::temp_directory_path() / ("lvdsim_cov_" + std::to_string(LVD_PID));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    ProcessResult nvcRun(std::vector<std::string> args) const {
        std::vector<std::string> argv{nvc.exe, "-L", nvc.libDir};
        argv.insert(argv.end(), args.begin(), args.end());
        ProcessOptions o;
        o.workingDir = dir.string();
        o.capture = true;
        o.env = {{"NVC_MAX_THREADS", "1"}};
        if (!nvc.dllDir.empty())
            o.pathPrepend.push_back(nvc.dllDir);
        return runProcess(argv, o);
    }
    bool analyse(const std::string& file = "cov_design.vhd") const {
        const auto r = nvcRun({"--std=2008", "-a", std::string(LVD_COVERAGE_DATA) + "/" + file});
        if (r.exitCode != 0)
            UNSCOPED_INFO(r.output);
        return r.started && r.exitCode == 0;
    }
    /// Elaborates with coverage of every kind into `db` and runs (until `stop`, or to the end).
    bool simulate(const std::string& db, const std::string& stop = {}) const {
        std::vector<std::string> args = {"--std=2008", "-e", "cov_tb", "-r"};
        if (!stop.empty())
            args.push_back("--stop-time=" + stop);
        REQUIRE(cov::addElaborationOptions(args, {}, (dir / db).generic_string()));
        const auto r = nvcRun(args);
        if (r.exitCode != 0)
            UNSCOPED_INFO(r.output);
        return r.started && r.exitCode == 0 && fs::exists(dir / db);
    }
    cov::ParseResult exportXml(const std::vector<std::string>& dbs, const std::string& xml) const {
        std::vector<std::string> full;
        for (const std::string& d : dbs)
            full.push_back((dir / d).generic_string());
        const auto r = nvcRun(cov::exportArgs(full, (dir / xml).generic_string()));
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        return cov::parseExportXml(readFile(dir / xml));
    }
    cov::ParseResult exportJson(const std::string& db, const std::string& json) const {
        const auto r = nvcRun(cov::exportArgs({(dir / db).generic_string()}, (dir / json).generic_string(),
                                              cov::ExportFormat::Json));
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        return cov::parseExport(readFile(dir / json));
    }
};

const cov::ToggleSignal* toggleNamed(const std::vector<cov::ToggleSignal>& t, const std::string& name) {
    for (const cov::ToggleSignal& s : t)
        if (s.name == name)
            return &s;
    return nullptr;
}

std::vector<std::string> bitIndices(const cov::ToggleSignal& s) {
    std::vector<std::string> out;
    for (const cov::ToggleSignal::Bit& b : s.bits)
        out.push_back(b.index);
    return out;
}

std::int64_t statementHits(const cov::Database& db, const std::string& hier) {
    for (const cov::Bin& b : db.bins)
        if (b.kind == cov::Kind::Statement && b.hier == hier)
            return b.hits;
    return -1;
}

} // namespace

TEST_CASE("coverage with nvc: collect, export, merge, report") {
    Sandbox sb;
    if (!sb.nvc.found)
        SKIP("nvc not built: " << sb.nvc.exe);
    REQUIRE(sb.analyse());
    REQUIRE(sb.simulate("full.ncdb"));
    REQUIRE(sb.simulate("short.ncdb", "40ns"));

    const cov::ParseResult full = sb.exportXml({"full.ncdb"}, "full.xml");
    REQUIRE(full.ok());
    const cov::Counts c = full.db.totals();
    // Same totals as the fixture made from this design (see test_coverage.cpp).
    CHECK(c.allTotal() == 175);
    CHECK(c.allHit() == 104);
    CHECK(full.db.findScope("/cov_tb/u_busy") >= 0);

    const cov::ParseResult shortRun = sb.exportXml({"short.ncdb"}, "short.xml");
    REQUIRE(shortRun.ok());
    CHECK(shortRun.db.totals().allHit() < c.allHit());

    // Two databases given to the export are merged (union): the counts add up.
    const cov::ParseResult both = sb.exportXml({"full.ncdb", "short.ncdb"}, "both.xml");
    REQUIRE(both.ok());
    const std::string stmt = "WORK.COV_TB.U_BUSY._P0._S0";
    const std::int64_t a = statementHits(full.db, stmt), b = statementHits(shortRun.db, stmt);
    REQUIRE(a > 0);
    REQUIRE(b > 0);
    CHECK(statementHits(both.db, stmt) == a + b);
    CHECK(both.db.totals().allTotal() == c.allTotal());

    // --cover-merge writes one database with the same content.
    {
        const auto r = sb.nvcRun(cov::mergeArgs(
            {(sb.dir / "full.ncdb").generic_string(), (sb.dir / "short.ncdb").generic_string()},
            (sb.dir / "merged.ncdb").generic_string()));
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        const cov::ParseResult merged = sb.exportXml({"merged.ncdb"}, "merged.xml");
        REQUIRE(merged.ok());
        CHECK(statementHits(merged.db, stmt) == a + b);
    }

    // The HTML report.
    {
        const auto r = sb.nvcRun(
            cov::reportArgs({(sb.dir / "full.ncdb").generic_string()}, (sb.dir / "html").generic_string()));
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
        CHECK(fs::exists(sb.dir / "html" / "index.html"));
    }
}

TEST_CASE("coverage with nvc: the interactive shell writes run-time counts") {
    // The GUI runs the simulator as `nvc -e --cover ... top -i`. The kernel writes the coverage database with
    // the run-time counts when the shell ends (change set 13 of kernel/PATCHES.md).
    Sandbox sb;
    if (!sb.nvc.found)
        SKIP("nvc not built: " << sb.nvc.exe);
    REQUIRE(sb.analyse());
    const std::string script = (sb.dir / "run.tcl").generic_string();
    std::ofstream(script) << "run 300 ns\n";
    // --do runs the same Tcl shell as -i, from a script instead of standard input.
    std::vector<std::string> args = {"--std=2008", "-e", "cov_tb", "--do", script};
    REQUIRE(cov::addElaborationOptions(args, {}, (sb.dir / "shell.ncdb").generic_string()));
    const auto r = sb.nvcRun(args);
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    REQUIRE(fs::exists(sb.dir / "shell.ncdb"));
    const cov::ParseResult x = sb.exportXml({"shell.ncdb"}, "shell.xml");
    REQUIRE(x.ok());
    REQUIRE(x.db.totals().allTotal() > 0);
    CHECK(x.db.totals().allHit() > 0);
}

TEST_CASE("coverage with nvc: JSON and XML exports agree (generate, record, memory, extended names)") {
    // The GUI reads the JSON export; both exports of the same database must give the same tree and counts on
    // the names that are hard to split: generate indices, record fields, arrays of arrays, \extended ids\.
    Sandbox sb;
    if (!sb.nvc.found)
        SKIP("nvc not built: " << sb.nvc.exe);
    REQUIRE(sb.analyse("cov_rich.vhd"));
    {
        const auto r =
            sb.nvcRun({"--std=2008", "-e", "--cover=all,include-mems",
                       "--cover-file=" + (sb.dir / "rich.ncdb").generic_string(), "RichTop", "-r"});
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
    }
    const cov::ParseResult x = sb.exportXml({"rich.ncdb"}, "rich.xml");
    const cov::ParseResult j = sb.exportJson("rich.ncdb", "rich.json");
    REQUIRE(x.ok());
    REQUIRE(j.ok());
    CHECK(j.db.files == x.db.files);
    REQUIRE(j.db.scopes.size() == x.db.scopes.size());
    for (std::size_t i = 0; i < x.db.scopes.size(); ++i) {
        INFO("scope " << i << " " << x.db.scopes[i].path);
        CHECK(j.db.scopes[i].name == x.db.scopes[i].name);
        CHECK(j.db.scopes[i].path == x.db.scopes[i].path);
        CHECK(j.db.scopes[i].blockName == x.db.scopes[i].blockName);
        CHECK(j.db.scopes[i].parent == x.db.scopes[i].parent);
        CHECK(j.db.scopes[i].instance == x.db.scopes[i].instance);
        CHECK(j.db.scopes[i].bins == x.db.scopes[i].bins);
    }
    REQUIRE(j.db.bins.size() == x.db.bins.size());
    for (std::size_t i = 0; i < x.db.bins.size(); ++i) {
        INFO("bin " << x.db.bins[i].hier);
        CHECK(j.db.bins[i].kind == x.db.bins[i].kind);
        CHECK(j.db.bins[i].hier == x.db.bins[i].hier);
        CHECK(j.db.bins[i].label == x.db.bins[i].label);
        CHECK(j.db.bins[i].hits == x.db.bins[i].hits);
        CHECK(j.db.bins[i].unreachable == x.db.bins[i].unreachable);
        CHECK(j.db.bins[i].file == x.db.bins[i].file);
    }
    CHECK(j.db.totals().allTotal() == x.db.totals().allTotal());
    CHECK(j.db.totals().allHit() == x.db.totals().allHit());
    CHECK(j.db.totals().allHit() > 0);

    // The three generated instances, found by the path the GUI shows.
    const cov::Database& db = j.db;
    REQUIRE(db.topInstances().size() == 1);
    const int u = db.findScope("/richtop/g(1)/u");
    REQUIRE(u >= 0);
    CHECK(cov::displayPath(db, u) == "/richtop/g(1)/u");
    CHECK(db.scopes[static_cast<std::size_t>(u)].blockName == R"(\Leaf Unit\-RTL)");
    CHECK(db.childInstances(db.findScope("/richtop/g(1)")) == std::vector<int>{u});
    CHECK(db.counts(u, false).allTotal() > 0);

    // Toggles per bit: record fields, a memory (array of vectors), a signal with an extended name.
    const std::vector<cov::ToggleSignal> t = cov::toggleSignals(db, u);
    const cov::ToggleSignal* rec = toggleNamed(t, "R");
    REQUIRE(rec != nullptr);
    CHECK(bitIndices(*rec) == std::vector<std::string>{".A", ".B(1)", ".B(0)"});
    const cov::ToggleSignal* mem = toggleNamed(t, "M");
    REQUIRE(mem != nullptr);
    CHECK(bitIndices(*mem) == std::vector<std::string>{"(0)(1)", "(0)(0)", "(1)(1)", "(1)(0)"});
    CHECK(mem->bits[0].rise == 1);
    CHECK_FALSE(mem->bits[0].covered()); // never falls
    CHECK(mem->bits[2].rise == 0);       // m(1) is never written
    const cov::ToggleSignal* odd = toggleNamed(t, R"(\odd name\)");
    REQUIRE(odd != nullptr);
    CHECK(cov::displayName(odd->name) == R"(\odd name\)");
    REQUIRE(odd->bits.size() == 1);
    CHECK(odd->bits[0].index.empty());

    // The source lines of the instance: its process, the if statement (line 26) has branches.
    const auto lines = cov::lineCoverage(db, u, db.mainFile(u));
    REQUIRE(lines.count(26) == 1);
    CHECK(lines.at(26).bins > 1);
}

TEST_CASE("coverage with nvc: a Verilog design gives an empty database") {
    // nvc instruments VHDL only (docs/tutorial.md "Code coverage"); the GUI says so for an empty database.
    // When this fails, nvc has learnt Verilog coverage: update the GUI note and the tutorial.
    Sandbox sb;
    if (!sb.nvc.found)
        SKIP("nvc not built: " << sb.nvc.exe);
    std::ofstream(sb.dir / "vtop.v")
        << "module VTop;\n  reg clk = 0;\n  reg [3:0] cnt = 0;\n"
           "  always #5 clk = ~clk;\n"
           "  always @(posedge clk) if (cnt == 9) cnt <= 0; else cnt <= cnt + 1;\n"
           "  initial #200 $finish;\nendmodule\n";
    {
        const auto r = sb.nvcRun({"-a", "vtop.v", "-e", "--cover=all",
                                  "--cover-file=" + (sb.dir / "v.ncdb").generic_string(), "VTop", "-r"});
        INFO(r.output);
        REQUIRE(r.exitCode == 0);
    }
    const cov::ParseResult j = sb.exportJson("v.ncdb", "v.json");
    REQUIRE(j.ok());
    CHECK(j.db.totals().allTotal() == 0);
}
