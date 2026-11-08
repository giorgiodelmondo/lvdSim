// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <process.hpp>

using namespace lvd::tools;

namespace {

// cmake is always available where these tests are built, so it serves as the child program.
ProcessResult cmake(std::vector<std::string> args, ProcessOptions o = {}) {
    std::vector<std::string> argv{"cmake"};
    argv.insert(argv.end(), args.begin(), args.end());
    o.capture = true;
    return runProcess(argv, o);
}

}  // namespace

TEST_CASE("a program runs and its output is captured") {
    const auto r = cmake({"--version"});
    REQUIRE(r.started);
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.output.find("cmake version") != std::string::npos);
}

TEST_CASE("the exit code is passed on") {
    const auto r = cmake({"-E", "no_such_command_xyz"});
    REQUIRE(r.started);
    REQUIRE(r.exitCode != 0);
}

TEST_CASE("a missing program is reported, not run") {
    const auto r = runProcess({"definitely_not_a_program_xyz"});
    REQUIRE_FALSE(r.started);
    REQUIRE_FALSE(r.error.empty());
    REQUIRE_FALSE(runProcess({}).started);
}

TEST_CASE("arguments with spaces and quotes arrive unchanged") {
    const auto r = cmake({"-E", "echo", "a b", "say \"hi\"", "back\\slash\\", ""});
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.output.find("a b say \"hi\" back\\slash\\") != std::string::npos);
}

TEST_CASE("environment variables and PATH additions reach the child only") {
    ProcessOptions o;
    o.env = {{"LVD_TEST_VAR", "hello world"}};
    o.pathPrepend = {"/lvd/first", "/lvd/second"};
    const auto r = cmake({"-E", "environment"}, o);
    REQUIRE(r.exitCode == 0);
    REQUIRE(r.output.find("LVD_TEST_VAR=hello world") != std::string::npos);
    REQUIRE(r.output.find("/lvd/first") != std::string::npos);

    const auto again = cmake({"-E", "environment"});
    REQUIRE(again.output.find("LVD_TEST_VAR") == std::string::npos);  // not leaked into later children
    REQUIRE(again.output.find("/lvd/first") == std::string::npos);
}

TEST_CASE("the working directory can be set") {
    ProcessOptions o;
    o.workingDir = ".";
    REQUIRE(cmake({"-E", "echo", "ok"}, o).exitCode == 0);
    o.workingDir = "/this/directory/does/not/exist";
    const auto r = cmake({"-E", "echo", "ok"}, o);
    REQUIRE((!r.started || r.exitCode != 0));
}

TEST_CASE("the executable path is known") { REQUIRE_FALSE(executablePath().empty()); }
