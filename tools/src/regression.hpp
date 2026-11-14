// SPDX-License-Identifier: GPL-3.0-or-later
// The regression runner: the tests of a project (one run per seed) as batch nvc processes, several at a time,
// each with its own log, waveform and coverage database. Planning (planJobs), judging a finished run
// (evaluate) and the JUnit report (junitXml) are pure; Runner starts and watches the processes without
// blocking, so the command line loops over step() and the GUI calls it from a timer.
//
// A run elaborates in memory (`nvc -e --jit --no-save`), so parallel runs of the same top level do not write
// the same library files. Every run gets NVC_MAX_THREADS=1 (see the quit hang note in docs/projects.md).
#pragma once

#include "diagnostics.hpp"
#include "process.hpp"
#include "project.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lvd::tools::regress {

struct Job {
    std::string id;   ///< test name, or test@seed when the test has seeds
    std::string test; ///< test name
    int testIndex = 0;
    std::optional<std::uint64_t> seed;
    std::string top;                  ///< as the test names it
    std::vector<std::string> nvcArgs; ///< after the nvc executable
    std::string dir;                  ///< output directory of the run (absolute)
    std::string logFile;
    std::string waveFile;   ///< empty without waveform
    std::string coverFile;  ///< empty without coverage
    std::string scriptFile; ///< inline commands are written here before the start (empty: none)
    std::string scriptContents;
    int timeoutSec = 600;
    std::string expect; ///< regular expression the output must contain
};

struct PlanOptions {
    std::string projectDir;                 ///< absolute
    std::string outDir;                     ///< absolute; empty: <projectDir>/<regression_dir>
    std::vector<std::string> nvcGlobalArgs; ///< for example -L <nvc library directory>
    std::string compatTcl;                  ///< command compatibility script, sourced before a do script
    /// Library name -> absolute directory for --work/--map: the project's libraries and the external ones.
    std::vector<std::pair<std::string, std::string>> libraries;
    std::vector<std::string> only; ///< test names or job ids to run; empty: all
    std::optional<bool> wave;      ///< overrides the project and the tests
    std::optional<bool> coverage;
    int timeoutSec = 0; ///< overrides when > 0
};

struct PlanResult {
    std::vector<Job> jobs;
    std::vector<std::string> errors;
    bool ok() const noexcept { return errors.empty(); }
};

PlanResult planJobs(const proj::Project& p, const PlanOptions& o);

enum class Status { Pending, Running, Passed, Failed, TimedOut, NotStarted, Cancelled };
const char*
statusName(Status s); ///< "pending", "running", "passed", "failed", "timeout", "error", "cancelled"
bool isDone(Status s);

struct Outcome {
    Status status = Status::Pending;
    int exitCode = -1;
    diag::Counts counts;
    bool expectFound = true;
    double seconds = 0;
    std::string message;      ///< one line: why it failed, or "passed"
    std::string firstProblem; ///< the first error or failure message of the log
};

/// Judges a finished run: passed when it started, did not time out, exited with 0, printed no error, failure
/// or fatal message and (when the test has one) its output matches the expected regular expression.
Outcome evaluate(const Job& job, bool started, bool timedOut, int exitCode, std::string_view log,
                 double seconds, const std::string& startError = {});

/// JUnit XML of a run (testsuites/testsuite/testcase; failures for failed runs, errors for time-outs and runs
/// that did not start; the tail of the log as system-out of a failed case). `timestamp` may be empty.
std::string junitXml(const std::string& suiteName, const std::vector<Job>& jobs,
                     const std::vector<Outcome>& outcomes, const std::string& timestamp = {});

/// results.txt of a regression directory: one line per run `id<TAB>status<TAB>seconds<TAB>message`.
std::string writeResults(const std::vector<Job>& jobs, const std::vector<Outcome>& outcomes);
/// Ids of the runs that did not pass, read from results.txt.
std::vector<std::string> failedIds(std::string_view resultsText);

/// The coverage databases of the runs that wrote one (files that exist).
std::vector<std::string> coverageDatabases(const std::vector<Job>& jobs);

struct RunnerConfig {
    std::string nvc;
    ProcessOptions process; ///< DLL directory, working directory (the project directory)
    int jobs = 1;
};

class Runner {
public:
    Runner(std::vector<Job> jobs, RunnerConfig config);
    ~Runner();

    /// Starts runs while fewer than `jobs` are running, collects ended runs, ends runs past their time-out.
    /// Never blocks.
    void step();
    bool finished() const;
    /// Ends the running runs and marks the pending ones cancelled.
    void cancel();
    /// Loops over step() until every run ended.
    void runToEnd(int pollMs = 20);

    const std::vector<Job>& jobs() const noexcept { return jobs_; }
    const std::vector<Outcome>& outcomes() const noexcept { return outcomes_; }
    int running() const;

    std::function<void(int)> onStarted;  ///< job index
    std::function<void(int)> onFinished; ///< job index; outcomes()[i] is final

private:
    void startJob(std::size_t i);
    void finishJob(std::size_t i, bool timedOut);

    struct Active;
    std::vector<Job> jobs_;
    RunnerConfig config_;
    std::vector<Outcome> outcomes_;
    std::vector<std::unique_ptr<Active>> active_;
    std::size_t next_ = 0;
};

/// Reads the whole file (for logs); empty if it cannot be read.
std::string readLog(const std::string& path);

} // namespace lvd::tools::regress
