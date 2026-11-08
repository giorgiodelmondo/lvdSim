// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lvd::tools {

struct ProcessOptions {
    std::vector<std::pair<std::string, std::string>> env;  ///< variables set for the child only
    std::vector<std::string> pathPrepend;                   ///< directories put in front of PATH for the child
    std::string workingDir;                                 ///< empty: inherit
    bool capture = false;  ///< capture stdout and stderr (merged) instead of passing them through
};

struct ProcessResult {
    bool started = false;
    int exitCode = -1;
    std::string output;  ///< only when capture was requested
    std::string error;   ///< why the process could not be started
};

/// Run a program and wait for it. argv[0] is looked up on PATH (including pathPrepend) if it has no
/// directory part. Standard input is inherited. Arguments are passed exactly, no shell is involved.
ProcessResult runProcess(const std::vector<std::string>& argv, const ProcessOptions& options = {});

/// Full path of the running executable, or empty if unknown.
std::string executablePath();

}  // namespace lvd::tools
