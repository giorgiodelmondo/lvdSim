// SPDX-License-Identifier: GPL-3.0-or-later
#include "project.hpp"
#include "regression.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace lvd::tools;
using namespace lvd::tools::regress;

namespace {

proj::Project sample() {
    return proj::parseProject(R"([project]
name = demo
vhdl = 2008
timeout = 50
[library base]
file = pkg.vhd
[library work]
file = tb.vhd
[test plain]
top = tb
generic = N=3
generic = /tb/u1/W=8
plusarg = +fast
stop_time = 1 ms
wave = on
[test scripted]
top = work.tb
commands = run -all
seeds = 7 9
seed_generic = SEED
coverage = on
timeout = 5
expect = PASS(ED)?
)")
        .project;
}

PlanOptions options() {
    PlanOptions o;
    o.projectDir = "/p";
    o.nvcGlobalArgs = {"-L", "/nvc/lib"};
    o.compatTcl = "/x/lvdcompat.tcl";
    o.libraries = {{"base", "/p/libs/base"}, {"work", "/p/libs/work"}, {"unisim", "/v/unisim"}};
    return o;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

std::ptrdiff_t indexOf(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) - v.begin();
}

} // namespace

TEST_CASE("regression plan: one job per seed with nvc arguments", "[regress]") {
    const PlanResult r = planJobs(sample(), options());
    REQUIRE(r.ok());
    REQUIRE(r.jobs.size() == 3);
    const Job& plain = r.jobs[0];
    CHECK(plain.id == "plain");
    CHECK(plain.dir == "/p/lvdsim_regression/plain");
    CHECK(plain.logFile == "/p/lvdsim_regression/plain/run.log");
    CHECK(plain.waveFile == "/p/lvdsim_regression/plain/wave.fst");
    CHECK(plain.coverFile.empty());
    CHECK(plain.timeoutSec == 50);
    const auto& a = plain.nvcArgs;
    CHECK(a[0] == "-L");
    CHECK(contains(a, "--std=08"));
    CHECK(contains(a, "--work=work:/p/libs/work"));
    CHECK(contains(a, "--map=base:/p/libs/base"));
    CHECK(contains(a, "--map=unisim:/v/unisim"));
    CHECK(contains(a, "--jit"));
    CHECK(contains(a, "--no-save"));
    CHECK(contains(a, "-gN=3"));
    CHECK(contains(a, "-gu1.W=8"));
    CHECK(contains(a, "--stop-time=1ms"));
    CHECK(contains(a, "--wave=/p/lvdsim_regression/plain/wave.fst"));
    CHECK(indexOf(a, "-e") < indexOf(a, "tb"));
    CHECK(indexOf(a, "tb") < indexOf(a, "-r"));
    CHECK(indexOf(a, "-r") < indexOf(a, "+fast")); // plus arguments of a plain run follow -r

    const Job& s7 = r.jobs[1];
    CHECK(s7.id == "scripted@7");
    CHECK(s7.seed == 7u);
    CHECK(s7.dir == "/p/lvdsim_regression/scripted_s7");
    CHECK(s7.coverFile == "/p/lvdsim_regression/scripted_s7/coverage.ncdb");
    CHECK(s7.waveFile.empty());
    CHECK(s7.timeoutSec == 5);
    CHECK(s7.scriptContents == "run -all\n");
    CHECK(contains(s7.nvcArgs, "--seed=7"));
    CHECK(contains(s7.nvcArgs, "-gSEED=7"));
    CHECK(contains(s7.nvcArgs, "--cover-file=/p/lvdsim_regression/scripted_s7/coverage.ncdb"));
    CHECK(indexOf(s7.nvcArgs, "--do") < indexOf(s7.nvcArgs, "/x/lvdcompat.tcl"));
    CHECK(s7.nvcArgs.back() == s7.scriptFile);
    CHECK(r.jobs[2].id == "scripted@9");
}

TEST_CASE("regression plan: selection, overrides and errors", "[regress]") {
    PlanOptions o = options();
    o.only = {"scripted@9"};
    o.wave = true;
    o.coverage = false;
    o.timeoutSec = 3;
    o.outDir = "/out";
    PlanResult r = planJobs(sample(), o);
    REQUIRE(r.jobs.size() == 1);
    CHECK(r.jobs[0].waveFile == "/out/scripted_s9/wave.fst");
    CHECK(r.jobs[0].coverFile.empty());
    CHECK(r.jobs[0].timeoutSec == 3);

    o.only = {"nope"};
    CHECK_FALSE(planJobs(sample(), o).ok());

    proj::Project p = sample();
    p.tests[0].top = "lost.tb";
    CHECK_FALSE(planJobs(p, options()).ok());
    p.tests[0].top = "work.tb base.other";
    CHECK_FALSE(planJobs(p, options()).ok());
}

TEST_CASE("regression verdict", "[regress]") {
    Job j;
    j.timeoutSec = 9;
    CHECK(evaluate(j, true, false, 0, "** Note: 0ms+0: hello\n", 1).status == Status::Passed);
    Outcome o = evaluate(j, true, false, 0, "** Error: 10ns+0: bad\n   Process :tb at tb.vhd:3\n", 1);
    CHECK(o.status == Status::Failed);
    CHECK(o.message == "1 error");
    CHECK(o.firstProblem == "10ns+0: bad");
    CHECK(evaluate(j, true, false, 1, "", 1).message == "exit code 1");
    CHECK(evaluate(j, true, false, 0, "** Failure: x\n** Failure: y\n", 1).message == "2 failures");
    CHECK(evaluate(j, true, true, -1, "", 9).status == Status::TimedOut);
    CHECK(evaluate(j, true, true, -1, "", 9).message == "timed out after 9 s");
    CHECK(evaluate(j, false, false, -1, "", 0, "cannot start 'nvc'").status == Status::NotStarted);
    j.expect = "TEST (PASSED|OK)";
    CHECK(evaluate(j, true, false, 0, "** Note: TEST OK\n", 1).status == Status::Passed);
    CHECK(evaluate(j, true, false, 0, "nothing\n", 1).message == "expected output not found");
    j.expect = "(";
    CHECK(evaluate(j, true, false, 0, "x", 1).message == "invalid expect pattern");
}

TEST_CASE("A stop in a batch run ends it without failing", "[regress]") {
    // VHDL std.env.stop and Verilog $stop both end a batch run with a note and exit code 0; in a --do script
    // the shell also prints the stop line and a prompt (no input, so it goes on). The verdict is the usual
    // one: errors, failures, the exit code and the expected output decide, the stop itself does not.
    Job j;
    j.timeoutSec = 9;
    const std::string vhdl = "** Note: 10fs+1: STOP called\n   Procedure STOP [] at env-body.vhd:32\n"
                             "   Process :vstop3:_p0 at vstop3.vhd:8\n";
    const std::string verilog =
        "** Note: 10fs+0: $stop called\n   Initial procedure 'initial#4#2' at vstop2.v:6\n"
        "$stop in /initial#4#2 at vstop2.v:6\n% ";
    CHECK(evaluate(j, true, false, 0, vhdl, 1).status == Status::Passed);
    CHECK(evaluate(j, true, false, 0, verilog, 1).status == Status::Passed);
    CHECK(evaluate(j, true, false, 0, verilog, 1).firstProblem.empty());
    CHECK(evaluate(j, true, false, 0, "** Error: 5fs+0: bad\n" + verilog, 1).status == Status::Failed);
    CHECK(evaluate(j, true, false, 3, "** Note: 10fs+1: STOP called with status 3\n", 1).message ==
          "exit code 3");
}

TEST_CASE("JUnit XML and results file", "[regress]") {
    std::vector<Job> jobs(4);
    jobs[0].id = jobs[0].test = "ok";
    jobs[1].id = "bad@1";
    jobs[1].test = "bad";
    jobs[2].id = jobs[2].test = "slow";
    jobs[3].id = jobs[3].test = "never";
    std::vector<Outcome> out(4);
    out[0].status = Status::Passed;
    out[0].message = "passed";
    out[0].seconds = 0.5;
    out[1].status = Status::Failed;
    out[1].message = "1 error";
    out[1].firstProblem = "a < b & \"c\"";
    out[2].status = Status::TimedOut;
    out[2].message = "timed out after 2 s";
    out[3].status = Status::NotStarted;
    out[3].message = "cannot start";
    const std::string x = junitXml("demo", jobs, out, "2026-01-01T00:00:00");
    CHECK(x.find("<testsuites name=\"demo\" tests=\"4\" failures=\"1\" errors=\"2\" skipped=\"0\"") !=
          std::string::npos);
    CHECK(x.find("timestamp=\"2026-01-01T00:00:00\"") != std::string::npos);
    CHECK(x.find("<testcase name=\"ok\" classname=\"demo.ok\" time=\"0.500\"/>") != std::string::npos);
    CHECK(x.find("message=\"1 error: a &lt; b &amp; &quot;c&quot;\"") != std::string::npos);
    CHECK(x.find("<error type=\"timeout\"") != std::string::npos);
    CHECK(x.find("<error type=\"not-started\"") != std::string::npos);

    const std::string res = writeResults(jobs, out);
    CHECK(failedIds(res) == std::vector<std::string>{"bad@1", "slow", "never"});
}
