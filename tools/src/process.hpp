// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lvd::tools {

struct ProcessOptions {
    std::vector<std::pair<std::string, std::string>> env; ///< variables set for the child only
    std::vector<std::string> pathPrepend;                 ///< directories put in front of PATH for the child
    std::string workingDir;                               ///< empty: inherit
    bool capture = false; ///< capture stdout and stderr (merged) instead of passing them through
};

struct ProcessResult {
    bool started = false;
    int exitCode = -1;
    std::string output; ///< only when capture was requested
    std::string error;  ///< why the process could not be started
};

/// Run a program and wait for it. argv[0] is looked up on PATH (including pathPrepend) if it has no
/// directory part. Standard input is inherited. Arguments are passed exactly, no shell is involved.
/// Runs a program and waits for it. While it runs, Ctrl-C (SIGINT; on Windows also Ctrl-Break) is left to the
/// child: this process ignores it, as system() does, so that it is not killed while the child (nvc ending a
/// run) goes on.
ProcessResult runProcess(const std::vector<std::string>& argv, const ProcessOptions& options = {});

/// Full path of the running executable, or empty if unknown.
std::string executablePath();

/// A child process that runs while the caller does other things (the regression scheduler polls several).
/// Its standard output and error go to a file, standard input is empty. The child and everything it starts
/// are ended by kill() and when the object is destroyed while the child runs. Use from one thread.
class ChildProcess {
public:
    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    /// Starts argv with options.env, options.pathPrepend and options.workingDir (options.capture is
    /// ignored); output is written to `outputFile` (created or truncated). False with *error set on failure.
    bool start(const std::vector<std::string>& argv, const ProcessOptions& options,
               const std::string& outputFile, std::string* error);
    /// True once the child has ended (then exitCode() is valid). Does not block.
    bool poll();
    /// Ends the child and the processes it started; poll() then reports it as ended.
    void kill();
    bool started() const noexcept { return started_; }
    int exitCode() const noexcept { return exitCode_; }

private:
    bool started_ = false;
    bool ended_ = false;
    int exitCode_ = -1;
#ifdef _WIN32
    void* process_ = nullptr;
    void* job_ = nullptr;
#else
    int pid_ = -1;
#endif
};

} // namespace lvd::tools
