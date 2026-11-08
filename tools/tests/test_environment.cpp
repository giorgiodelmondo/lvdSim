// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <environment.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace lvd::tools;

namespace {

fs::path freshDir(const char* tag) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path d = fs::temp_directory_path() / (std::string("lvd_env_") + tag + std::to_string(stamp));
    fs::create_directories(d);
    return d;
}

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

#ifdef _WIN32
const char* kNvcName = "nvc.exe";
#else
const char* kNvcName = "nvc";
#endif

} // namespace

TEST_CASE("a bundled nvc is found next to the program") {
    const fs::path root = freshDir("a");
    touch(root / "nvc" / "bin" / kNvcName);
    touch(root / "nvc" / "lib" / "std" / "_index");
    const auto b = findBundledNvc(root.string());
    REQUIRE(b.has_value());
    CHECK(b->exe.find(kNvcName) != std::string::npos);
    CHECK(b->libDir.size() >= 7);
    CHECK(b->libDir.substr(b->libDir.size() - 7) == "nvc/lib");
#ifdef _WIN32
    CHECK(!b->dllDir.empty());
#endif
}

TEST_CASE("a bundled nvc is found one directory up (bin/ layout)") {
    const fs::path root = freshDir("b");
    touch(root / "bin" / "placeholder");
    touch(root / "nvc" / "bin" / kNvcName);
    touch(root / "nvc" / "lib" / "std" / "_index");
    REQUIRE(findBundledNvc((root / "bin").string()).has_value());
}

TEST_CASE("an incomplete bundle is not used") {
    const fs::path noLib = freshDir("c");
    touch(noLib / "nvc" / "bin" / kNvcName);
    CHECK(!findBundledNvc(noLib.string()).has_value());
    const fs::path noExe = freshDir("d");
    touch(noExe / "nvc" / "lib" / "std" / "_index");
    CHECK(!findBundledNvc(noExe.string()).has_value());
    CHECK(!findBundledNvc("").has_value());
}
