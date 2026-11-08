// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <libraries.hpp>
#include <vendor_libs.hpp>

using namespace lvd::tools;
namespace fs = std::filesystem;

namespace {

struct Tree {
    fs::path root;
    Tree() {
        root = fs::temp_directory_path() /
               ("lvdsim_vendor_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }
    ~Tree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void dir(const std::string& rel) const { fs::create_directories(root / rel); }
    void file(const std::string& rel) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << "x";
    }
    std::string str(const std::string& rel = "") const {
        return (rel.empty() ? root : root / rel).generic_string();
    }
};

bool has(const std::vector<InstallCandidate>& v, const std::string& path) {
    for (const auto& c : v)
        if (c.path == path)
            return true;
    return false;
}

} // namespace

TEST_CASE("library choices: defaults match the scripts", "[vendor]") {
    CHECK(defaultLibraries(Vendor::Xilinx) == std::vector<std::string>{"unisim", "unimacro"});
    CHECK(defaultLibraries(Vendor::Altera) ==
          std::vector<std::string>{"altera_mf", "lpm", "sgate", "altera_primitives"});
    CHECK(libraryChoices(Vendor::Altera).size() == 9);
    CHECK(expectedLibraryCount({"altera_mf", "altera_verilog"}) == 5);
}

TEST_CASE("detectInstalls finds Vivado version folders two levels down, newest first", "[vendor]") {
    Tree t;
    t.dir("Xilinx/Vivado/2022.2/data/vhdl/src/unisims");
    t.dir("Xilinx/Vivado/2023.1/data/vhdl/src/unisims");
    t.dir("Xilinx/Vivado/2022.10/data/vhdl/src/unisims");
    t.dir("Xilinx/Vitis/2023.1/data");        // not a Vivado
    t.dir("Xilinx/Vivado/2019.1/data/other"); // no unisims
    const auto found = detectInstalls(Vendor::Xilinx, {}, {t.str("Xilinx")});
    REQUIRE(found.size() == 3);
    CHECK(found[0].path == t.str("Xilinx/Vivado/2023.1"));
    CHECK(found[1].path == t.str("Xilinx/Vivado/2022.10"));
    CHECK(found[2].path == t.str("Xilinx/Vivado/2022.2"));
    CHECK(found[0].label.find("2023.1") == 0);
    CHECK_FALSE(found[0].fromEnvironment);
}

TEST_CASE("detectInstalls finds Quartus folders one level down and both layouts", "[vendor]") {
    Tree t;
    t.dir("intelFPGA_pro/24.2/quartus/eda/sim_lib");
    t.dir("intelFPGA_pro/23.4/eda/sim_lib");
    t.dir("intelFPGA_pro/docs");
    const auto found = detectInstalls(Vendor::Altera, {}, {t.str("intelFPGA_pro"), t.str("missing_root")});
    REQUIRE(found.size() == 2);
    CHECK(found[0].path == t.str("intelFPGA_pro/24.2"));
    CHECK(found[1].path == t.str("intelFPGA_pro/23.4"));
}

TEST_CASE("detectInstalls puts the environment first and lists a folder once", "[vendor]") {
    Tree t;
    t.dir("intelFPGA_pro/24.2/quartus/eda/sim_lib");
    // QUARTUS_ROOTDIR usually points at <version>/quartus
    const auto found =
        detectInstalls(Vendor::Altera, {"", t.str("intelFPGA_pro/24.2/quartus"), "/does/not/exist"},
                       {t.str("intelFPGA_pro")});
    REQUIRE(found.size() == 1);
    CHECK(found[0].path == t.str("intelFPGA_pro/24.2"));
    CHECK(found[0].fromEnvironment);

    Tree v;
    v.dir("a/data/vhdl/src/unisims");
    v.dir("root/2020.1/Vivado/data/vhdl/src/unisims");
    const auto xv = detectInstalls(Vendor::Xilinx, {v.str("a")}, {v.str("root")});
    REQUIRE(xv.size() == 2);
    CHECK(xv[0].fromEnvironment);
    CHECK(has(xv, v.str("root/2020.1/Vivado")));
}

TEST_CASE("looksLikeInstall", "[vendor]") {
    Tree t;
    t.dir("x/eda/sim_lib");
    t.dir("y/data/vhdl/src/unisims");
    CHECK(looksLikeInstall(Vendor::Altera, t.str("x")));
    CHECK_FALSE(looksLikeInstall(Vendor::Xilinx, t.str("x")));
    CHECK(looksLikeInstall(Vendor::Xilinx, t.str("y")));
    CHECK_FALSE(looksLikeInstall(Vendor::Altera, ""));
}

TEST_CASE("buildScriptArgs", "[vendor]") {
    ScriptRequest r;
    r.vendor = Vendor::Altera;
    r.installDir = "D:/cad/q 24.2";
    r.outDir = "D:/out dir";
    r.libraries = {"altera_mf", "lpm"};
    std::string err;
    auto a = buildScriptArgs(r, &err);
    CHECK(a == std::vector<std::string>{"--quartus", "D:/cad/q 24.2", "--out", "D:/out dir", "--only",
                                        "altera_mf,lpm"});
    CHECK(err.empty());
    r.vendor = Vendor::Xilinx;
    r.quiet = true;
    a = buildScriptArgs(r);
    CHECK(a.front() == "--vivado");
    CHECK(a.back() == "--quiet");

    r.libraries.clear();
    CHECK(buildScriptArgs(r, &err).empty());
    CHECK(err == "no library selected");
    r.libraries = {"xpm"};
    r.outDir.clear();
    CHECK(buildScriptArgs(r, &err).empty());
    CHECK(err == "no output folder given");
    r.outDir = "o";
    r.installDir.clear();
    CHECK(buildScriptArgs(r, &err).empty());
    CHECK(err == "no installation folder given");
}

TEST_CASE("findLibsScript looks beside the program, then upwards", "[vendor]") {
    const auto exists = [](const std::string& p) { return p == "D:/repo/scripts/libs/compile_libs.bat"; };
    CHECK(findLibsScript("D:/repo/build/mingw-debug/bin", true, exists) ==
          "D:/repo/scripts/libs/compile_libs.bat");
    CHECK(findLibsScript("D:/repo/build/mingw-debug/bin", false, exists).empty());
    const auto bundled = [](const std::string& p) { return p == "C:/lvdsim/scripts/libs/compile_libs.sh"; };
    CHECK(findLibsScript("C:/lvdsim", false, bundled) == "C:/lvdsim/scripts/libs/compile_libs.sh");
    CHECK(findLibsScript("C:/lvdsim/bin", false, bundled) ==
          "C:/lvdsim/scripts/libs/compile_libs.sh"); // Linux layout
    CHECK(findLibsScript("C:/lvdsim", true, [](const std::string&) { return false; }).empty());
}

TEST_CASE("scriptCommand per platform", "[vendor]") {
    const auto win = scriptCommand(true, "C:/Program Files/lvdsim/scripts/libs/compile_libs.bat",
                                   {"--out", "D:/my libs", "--only", "a,b"});
    CHECK(win.program == "cmd.exe");
    CHECK(win.nativeArguments == "/d /s /c \"\"C:\\Program Files\\lvdsim\\scripts\\libs\\compile_libs.bat\" "
                                 "\"--out\" \"D:\\my libs\" \"--only\" \"a,b\"\"");
    const auto lin = scriptCommand(false, "/opt/lvdsim/scripts/libs/compile_libs.sh", {"--out", "/tmp/x"});
    CHECK(lin.program == "sh");
    CHECK(lin.args ==
          std::vector<std::string>{"/opt/lvdsim/scripts/libs/compile_libs.sh", "--out", "/tmp/x"});
    CHECK(lin.nativeArguments.empty());
}

TEST_CASE("parseScriptLine", "[vendor]") {
    CHECK(parseScriptLine("unisim (VHDL)").kind == LineKind::Started);
    CHECK(parseScriptLine("unisim (VHDL)").library == "unisim");
    CHECK(parseScriptLine("altera_ver\r").kind == LineKind::Started);
    const ScriptLine d = parseScriptLine("altera_mf: 2 ok, 1 failed, 3 skipped in 4s");
    CHECK(d.kind == LineKind::LibraryDone);
    CHECK(d.library == "altera_mf");
    CHECK(d.ok == 2);
    CHECK(d.failed == 1);
    CHECK(d.skipped == 3);
    CHECK(d.seconds == 4);
    const ScriptLine r = parseScriptLine("altera_primitives         2        0        0      1s");
    CHECK(r.kind == LineKind::SummaryRow);
    CHECK(r.library == "altera_primitives");
    CHECK(r.ok == 2);
    CHECK(parseScriptLine("  FAILED: x.v (see log)").kind == LineKind::Failed);
    CHECK(parseScriptLine("  skipped (encrypted, `protect): a.vhd").kind == LineKind::Skipped);
    CHECK(parseScriptLine("library          files ok   failed  skipped    time").kind == LineKind::Other);
    CHECK(parseScriptLine("output: D:/x   (lvdsim.ini written)").kind == LineKind::Other);
}

TEST_CASE("parseSummary reads the table from sh and bat output", "[vendor]") {
    const std::string out = "unisim (VHDL)\nunisim: 4 ok, 0 failed, 1 skipped in 2s\n\n"
                            "library            files ok   failed  skipped    time\r\n"
                            "unisim                    4        0        1      2s\r\n"
                            "altera_lnsim              1        1        0      0s\r\n"
                            "output: D:/x   (lvdsim.ini written; logs in D:/x/logs)\n";
    const Summary s = parseSummary(out);
    REQUIRE(s.rows.size() == 2);
    CHECK(s.librariesOk == 1);
    CHECK(s.librariesFailed == 1);
    CHECK(s.filesOk == 5);
    CHECK(s.filesFailed == 1);
    CHECK(s.filesSkipped == 1);
    CHECK(summaryText(s) == "2 libraries (1 ok, 1 with failed files): 5 files ok, 1 failed, 1 skipped");
    CHECK(summaryText(parseSummary("nothing here")) == "no summary printed");
}

TEST_CASE("librariesInFolder and searchPathArgs", "[vendor]") {
    Tree t;
    t.file("libs/unisim/_NVC_LIB");
    t.file("libs/altera_mf/_NVC_LIB");
    t.file("libs/logs/failed.txt");
    t.file("libs2/unisim/_NVC_LIB");
    t.file("libs2/lpm/_NVC_LIB");
    const auto a = librariesInFolder(t.str("libs"));
    REQUIRE(a.size() == 2);
    CHECK(a[0].name == "altera_mf");
    CHECK(a[1].name == "unisim");
    CHECK(librariesInFolder(t.str("nowhere")).empty());

    std::vector<FolderLibrary> libs;
    const auto args = searchPathArgs({t.str("libs"), t.str("libs2")}, {"WORK"}, &libs);
    CHECK(args == std::vector<std::string>{"-L", "altera_mf", "-L", "unisim", "-L", "lpm"});
    REQUIRE(libs.size() == 3);
    CHECK(libs[1].path == t.str("libs/unisim")); // first folder wins
    CHECK(searchPathArgs({t.str("libs")}, {"UNISIM", "altera_mf"}).empty());
}
