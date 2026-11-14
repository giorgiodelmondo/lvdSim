// SPDX-License-Identifier: GPL-3.0-or-later
// Builds a project from an existing compile script: the vlib, vmap, vcom and vlog commands of a .do (Tcl)
// script become libraries and source files in the same order, vsim commands become tests, and the [Library]
// mappings of an lvdsim.ini give library directories. Only the plain forms are understood: commands on their
// own (or separated by ';'), `set name value` variables used as $name or ${name}, `do other.do`, -f argument
// files. Everything else is listed as a warning and left out.
#pragma once

#include "project.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lvd::tools::proj {

struct ImportOptions {
    std::string scriptDir;  ///< directory the script's relative paths are relative to (its working directory)
    std::string projectDir; ///< directory of the project file; paths are written relative to it
    std::string iniText;    ///< contents of lvdsim.ini (may be empty)
    /// Reads a file (do scripts called with `do`, -f files); nullopt if missing.
    std::function<std::optional<std::string>(const std::string&)> readFile;
};

struct ImportResult {
    Project project;
    std::vector<std::string> warnings;
    int commands = 0; ///< commands understood
};

ImportResult importDoScript(std::string_view script, const ImportOptions& options);

/// Splits a Tcl script into commands, each a list of words. Handles comments (# at the start of a command),
/// backslash-newline, ';', double quotes, braces and $variables of `vars` (unknown ones are kept as written).
/// `[...]` substitutions are kept as written. Exposed for the tests.
std::vector<std::vector<std::string>>
splitTclCommands(std::string_view script, const std::vector<std::pair<std::string, std::string>>& vars);

} // namespace lvd::tools::proj
