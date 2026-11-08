// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <libraries.hpp>

using namespace lvd::tools;
namespace fs = std::filesystem;

namespace {

struct Tree {
    fs::path root;
    Tree() {
        root = fs::temp_directory_path() /
               ("lvdsim_libscan_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Tree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void file(const std::string& rel) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << "x";
    }
};

Context ctxWith(const Tree& t) {
    Context c;
    c.dirExists = [](const std::string& p) { return fs::is_directory(p); };
    (void)t;
    return c;
}

} // namespace

TEST_CASE("libraries below the root are found by their marker file") {
    Tree t;
    t.file("work/_NVC_LIB");
    t.file("work/WORK.COUNTER");
    t.file("work/WORK.COUNTER-RTL");
    t.file("work/WORK.COUNTER-FAST");
    t.file("work/WORK.COUNTER.elab");
    t.file("work/WORK.ALU");
    t.file("work/WORK.ALU-A");
    t.file("work/_index");
    t.file("notalibrary/readme.txt");

    const auto libs = scanLibraries(ctxWith(t), t.root.string());
    REQUIRE(libs.size() == 1);
    REQUIRE(libs[0].name == "work");

    const auto& units = libs[0].units;
    REQUIRE(units.size() == 2);
    REQUIRE(units[0].name == "alu"); // sorted
    REQUIRE(units[0].architectures == std::vector<std::string>{"a"});
    REQUIRE_FALSE(units[0].elaborated);
    REQUIRE(units[1].name == "counter");
    REQUIRE(units[1].architectures == std::vector<std::string>{"fast", "rtl"});
    REQUIRE(units[1].elaborated);
}

TEST_CASE("mapped libraries elsewhere are listed too, once, sorted") {
    Tree t;
    t.file("work/_NVC_LIB");
    t.file("work/WORK.T");
    t.file("elsewhere/deep/MYLIB.PKG");
    t.file("elsewhere/deep/MYLIB.PKG-body");

    Context c = ctxWith(t);
    c.libs.set("Zlib", (t.root / "elsewhere" / "deep").generic_string());
    c.libs.set("work", (t.root / "work").generic_string()); // also found by scanning: listed once
    c.libs.set("ghost", (t.root / "does_not_exist").generic_string());

    const auto libs = scanLibraries(c, t.root.string());
    REQUIRE(libs.size() == 2);
    REQUIRE(libs[0].name == "work");
    REQUIRE(libs[1].name == "Zlib");
    REQUIRE(libs[1].units.size() == 1);
    REQUIRE(libs[1].units[0].name == "pkg");
    REQUIRE(libs[1].units[0].architectures ==
            std::vector<std::string>{"body"}); // package bodies look like architectures
}

TEST_CASE("an empty or missing root has no libraries") {
    Tree t;
    REQUIRE(scanLibraries(ctxWith(t), t.root.string()).empty());
    REQUIRE(scanLibraries(ctxWith(t), (t.root / "nope").string()).empty());
}

TEST_CASE("an empty library has no units") {
    Tree t;
    t.file("lib/_NVC_LIB");
    const auto libs = scanLibraries(ctxWith(t), t.root.string());
    REQUIRE(libs.size() == 1);
    REQUIRE(libs[0].units.empty());
}

#include "environment.hpp"

#include <algorithm>
#include <cstdlib>

TEST_CASE("LVDSIM_NVC_OPTS adds global nvc options") {
#ifdef _WIN32
    _putenv_s("LVDSIM_NVC_OPTS", "-M 128m  --foo");
#else
    setenv("LVDSIM_NVC_OPTS", "-M 128m  --foo", 1);
#endif
    const auto env = lvd::tools::discoverEnvironment();
    const auto& g = env.ctx.nvcGlobalArgs;
    const auto it = std::find(g.begin(), g.end(), "-M");
    REQUIRE(it != g.end());
    REQUIRE(*(it + 1) == "128m");
    REQUIRE(*(it + 2) == "--foo");
#ifdef _WIN32
    _putenv_s("LVDSIM_NVC_OPTS", "");
#else
    unsetenv("LVDSIM_NVC_OPTS");
#endif
}
