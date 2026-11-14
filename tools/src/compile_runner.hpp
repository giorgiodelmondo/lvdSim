// SPDX-License-Identifier: GPL-3.0-or-later
// Analyses the files of a project with nvc, one file per nvc call, in dependency order, only what is out of
// date (or all, or a selection and what depends on it). Like the regression Runner it never blocks: step()
// starts or collects one analysis, so the GUI can drive it from a timer and the command line from a loop. The
// compile state is written after every file. Files that depend on a file that failed are skipped.
#pragma once

#include "compile_plan.hpp"
#include "diagnostics.hpp"
#include "process.hpp"
#include "project.hpp"
#include "translate.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lvd::tools::build {

/// Everything needed to plan the nvc calls of a project: the project directory, the context the vcom and vlog
/// translation works with (library mappings, nvc's global options) and how to start nvc.
struct Setup {
    std::string projectDir; ///< absolute
    Context ctx;            ///< from the environment; projectContext() adds the project's libraries
    std::string nvc;
    ProcessOptions process;
};

/// ctx with the project's libraries, its `map` entries and the libraries of its library folders added to the
/// library mappings (absolute paths).
Context projectContext(const proj::Project& p, const std::string& projectDir, Context ctx);
/// Library name -> absolute directory for every library a run of the project can see (project libraries
/// first).
std::vector<std::pair<std::string, std::string>>
projectLibraries(const proj::Project& p, const std::string& projectDir, const Context& projectCtx);

/// The nvc arguments that analyse one file (through the vcom/vlog translation), or an error.
struct FileCall {
    std::vector<std::string> args;
    std::string error;
};
FileCall analyseCall(const proj::Project& p, FileRef f, const Context& projectCtx);

/// How a file stands, for the status icons of the GUI and `lvdsim-cli project status`.
enum class FileState { UpToDate, OutOfDate, Failed, Missing };
struct FileStatus {
    FileState state = FileState::OutOfDate;
    Reason reason = Reason::New; ///< why it is out of date
    int cause = -1;              ///< Dependency: the file (index in projectFiles order) it waits for
};
/// Status of every file in projectFiles() order.
std::vector<FileStatus> assess(const proj::Project& p, const std::string& projectDir);

class Runner {
public:
    enum class StepState { Pending, Running, Ok, Failed, Skipped };

    explicit Runner(Setup setup);
    ~Runner();

    /// Scans the files, reads the compile state and plans the analysis. False with *error when nothing can
    /// be done (the plan may still be empty: everything is up to date).
    bool prepare(const proj::Project& p, Mode mode, const std::set<int>& selected, std::string* error);
    const Graph& graph() const noexcept { return graph_; }
    const Plan& plan() const noexcept { return plan_; }
    const proj::Project& project() const noexcept { return project_; }

    /// Starts the next analysis or collects the running one. Never blocks.
    void step();
    bool finished() const noexcept { return next_ >= plan_.steps.size() && !child_; }
    void cancel();
    void runToEnd(int pollMs = 10);

    StepState state(std::size_t step) const { return states_.at(step); }
    const std::string& output(std::size_t step) const { return outputs_.at(step); }
    std::vector<diag::Diagnostic> diagnostics(std::size_t step) const;
    const std::vector<std::string>& arguments(std::size_t step) const { return args_.at(step); }
    int failedCount() const;
    int okCount() const;
    /// Path of a step's file as the project names it, and its library.
    std::string fileName(std::size_t step) const;
    std::string libraryName(std::size_t step) const;

    std::function<void(std::size_t)> onStarted;
    std::function<void(std::size_t)> onFinished;

private:
    void startStep(std::size_t k);
    void finishStep(bool ok, int exitCode);
    void saveState();

    Setup setup_;
    proj::Project project_;
    Context ctx_;
    Graph graph_;
    Plan plan_;
    State state_;
    std::vector<std::optional<FileStamp>> stamps_;
    std::vector<StepState> states_;
    std::vector<std::string> outputs_;
    std::vector<std::vector<std::string>> args_;
    std::set<int> failedFiles_;
    std::size_t next_ = 0;
    std::size_t current_ = 0;
    std::unique_ptr<ChildProcess> child_;
    std::string logFile_;
};

} // namespace lvd::tools::build
