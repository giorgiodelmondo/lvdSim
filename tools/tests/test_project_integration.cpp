// SPDX-License-Identifier: GPL-3.0-or-later
// End to end: lvdsim-cli compile / regress / project on a small project of three libraries with a real nvc.
// Skipped when nvc cannot be found (LVDSIM_NVC, LVDSIM_NVC_LIBDIR, LVDSIM_NVC_DLLDIR override the defaults).
#include <catch2/catch_test_macros.hpp>
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

struct Sandbox {
    fs::path dir;
    std::string nvc, libDir, dllDir;
    bool found = false;

    explicit Sandbox(const char* tag) {
        const fs::path build = LVD_KERNEL_BUILD;
#ifdef _WIN32
        nvc = envOr("LVDSIM_NVC", (build / "bin" / "nvc.exe").generic_string());
        dllDir =
            envOr("LVDSIM_NVC_DLLDIR", fs::exists("C:/msys64/mingw64/bin") ? "C:/msys64/mingw64/bin" : "");
#else
        nvc = envOr("LVDSIM_NVC", (build / "bin" / "nvc").generic_string());
        dllDir = envOr("LVDSIM_NVC_DLLDIR", "");
#endif
        libDir = envOr("LVDSIM_NVC_LIBDIR", (build / "lib").generic_string());
        found = fs::exists(nvc);
        dir = fs::temp_directory_path() / ("lvdsim_proj_" + std::string(tag) + "_" + std::to_string(LVD_PID));
        fs::remove_all(dir);
        fs::create_directories(dir / "src");
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void write(const std::string& name, const std::string& text) const {
        fs::create_directories((dir / name).parent_path());
        std::ofstream(dir / name, std::ios::binary) << text;
    }
    std::string read(const std::string& name) const {
        std::ifstream in(dir / name, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    ProcessResult cli(std::vector<std::string> args) const {
        std::vector<std::string> argv{LVD_CLI_EXE};
        argv.insert(argv.end(), args.begin(), args.end());
        ProcessOptions o;
        o.workingDir = dir.string();
        o.capture = true;
        o.env = {{"LVDSIM_NVC", nvc}, {"LVDSIM_NVC_LIBDIR", libDir}};
        if (!dllDir.empty())
            o.env.emplace_back("LVDSIM_NVC_DLLDIR", dllDir);
        return runProcess(argv, o);
    }
};

int countLines(const std::string& text, const std::string& what) {
    int n = 0;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
        n += line.find(what) != std::string::npos ? 1 : 0;
    return n;
}

void writeDesign(const Sandbox& s) {
    s.write("src/util_pkg.vhd",
            "package util_pkg is\n  function double(x : integer) return integer;\nend package;\n"
            "package body util_pkg is\n  function double(x : integer) return integer is\n"
            "  begin return 2 * x; end function;\nend package body;\n");
    s.write("src/unrelated.vhd", "package unrelated is constant K : integer := 1; end package;\n");
    s.write("src/core.vhd",
            "library util; use util.util_pkg.all;\nentity core is port (a : in integer; y : out integer);\n"
            "end entity;\narchitecture rtl of core is begin y <= double(a); end architecture;\n");
    s.write("src/tb.vhd", R"(library core_lib;
entity tb is generic (MODE : integer := 0); end entity;
architecture sim of tb is
  signal a, y : integer := 0;
begin
  dut : entity core_lib.core port map (a => a, y => y);
  process begin
    a <= 21;
    wait for 10 ns;
    if MODE = 0 then
      assert y = 42 report "wrong result" severity error;
      report "TEST PASSED";
    elsif MODE = 1 then
      assert y = 43 report "y is not 43" severity error;
    else
      loop wait for 1 ns; end loop;
    end if;
    wait;
  end process;
end architecture;
)");
    // Listed out of order on purpose: the dependencies decide the analysis order.
    s.write("demo.lvdproj", R"([project]
name = demo
[library work]
file = src/tb.vhd
[library core_lib]
file = src/core.vhd
[library util]
file = src/util_pkg.vhd
file = src/unrelated.vhd
[test pass]
top = work.tb
expect = TEST PASSED
[test fail]
top = work.tb
generic = MODE=1
[test hang]
top = work.tb
generic = MODE=2
timeout = 3
[test scripted]
top = tb
commands = run -all
seeds = 1 2
coverage = on
)");
}

} // namespace

TEST_CASE("project compile: dependency order, then only changed files and their dependents", "[project-it]") {
    Sandbox s("compile");
    if (!s.found)
        SKIP("nvc not found at " + s.nvc);
    writeDesign(s);

    ProcessResult r = s.cli({"compile", "demo.lvdproj"});
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(countLines(r.output, "-- Analysing") == 4);
    const auto pos = [&](const std::string& f) { return r.output.find("-- Analysing " + f); };
    CHECK(pos("util: src/util_pkg.vhd") < pos("core_lib: src/core.vhd"));
    CHECK(pos("core_lib: src/core.vhd") < pos("work: src/tb.vhd"));

    r = s.cli({"compile", "demo.lvdproj"});
    CHECK(r.output.find("All 4 files are up to date") != std::string::npos);

    // Change the package of library util: it, core (core_lib) and tb (work) are analysed again, unrelated is
    // not.
    s.write("src/util_pkg.vhd", s.read("src/util_pkg.vhd") + "-- changed\n");
    r = s.cli({"compile", "demo.lvdproj"});
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(countLines(r.output, "-- Analysing") == 3);
    CHECK(r.output.find("unrelated.vhd") == std::string::npos);
    CHECK(r.output.find("core.vhd (depends on src/util_pkg.vhd)") != std::string::npos);

    // An error: the file fails, its dependents are skipped, the exit code says so.
    s.write("src/core.vhd",
            s.read("src/core.vhd") + "architecture broken of core is begin y <= nothing; end;\n");
    r = s.cli({"compile", "demo.lvdproj"});
    CHECK(r.exitCode == 1);
    CHECK(r.output.find("** Error:") != std::string::npos);
    CHECK(r.output.find("-- Skipped work: src/tb.vhd") != std::string::npos);
    r = s.cli({"project", "status", "demo.lvdproj"});
    CHECK(r.output.find("failed  core_lib: src/core.vhd") != std::string::npos);
}

TEST_CASE("project regress: pass, fail, timeout, seeds, coverage and JUnit", "[project-it]") {
    Sandbox s("regress");
    if (!s.found)
        SKIP("nvc not found at " + s.nvc);
    writeDesign(s);

    ProcessResult r = s.cli({"regress", "demo.lvdproj", "-j", "4", "--junit", "junit.xml"});
    INFO(r.output);
    CHECK(r.exitCode == 1);
    CHECK(r.output.find("PASS    pass") != std::string::npos);
    CHECK(r.output.find("FAIL    fail") != std::string::npos);
    CHECK(r.output.find("TIMEOUT hang") != std::string::npos);
    CHECK(r.output.find("PASS    scripted@1") != std::string::npos);
    CHECK(r.output.find("PASS    scripted@2") != std::string::npos);
    CHECK(r.output.find("3 of 5 passed") != std::string::npos);
    CHECK(r.output.find("Coverage of 2 runs merged") != std::string::npos);
    CHECK(fs::exists(s.dir / "lvdsim_regression" / "merged.ncdb"));
    CHECK(fs::exists(s.dir / "lvdsim_regression" / "fail" / "run.log"));
    CHECK(s.read("lvdsim_regression/fail/run.log").find("y is not 43") != std::string::npos);

    const std::string junit = s.read("junit.xml");
    CHECK(junit.find("tests=\"5\" failures=\"1\" errors=\"1\"") != std::string::npos);
    CHECK(junit.find("<error type=\"timeout\"") != std::string::npos);

    // Only the runs that did not pass.
    r = s.cli({"regress", "demo.lvdproj", "--rerun-failed", "--no-compile", "--timeout", "2"});
    CHECK(r.output.find("Running 2 test runs") != std::string::npos);
    CHECK(r.output.find("scripted") == std::string::npos);
}

TEST_CASE("project import: a compile script gives a project that compiles", "[project-it]") {
    Sandbox s("import");
    if (!s.found)
        SKIP("nvc not found at " + s.nvc);
    writeDesign(s);
    s.write("compile.do",
            "vlib util\nvlib core_lib\nvlib work\nvcom -2008 -work util src/util_pkg.vhd src/unrelated.vhd\n"
            "vcom -work core_lib src/core.vhd\nvcom src/tb.vhd\nvsim -c -do \"run -all\" work.tb\n");
    ProcessResult r = s.cli({"project", "import", "compile.do", "-o", "imported.lvdproj"});
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("3 libraries, 4 files, 1 tests") != std::string::npos);
    r = s.cli({"compile", "imported.lvdproj"});
    INFO(r.output);
    CHECK(r.exitCode == 0);
    r = s.cli({"regress", "imported.lvdproj", "--no-compile"});
    INFO(r.output);
    CHECK(r.exitCode == 0);
    // Refuses to overwrite.
    CHECK(s.cli({"project", "import", "compile.do", "-o", "imported.lvdproj"}).exitCode == 2);
}
