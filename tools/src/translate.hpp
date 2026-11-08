// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library_map.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lvd::tools {

/// Everything the planners need to know about the environment. All file system access goes through
/// the callbacks so the planners can be tested without touching the disk.
struct Context {
    LibraryMap libs;                                                   ///< from lvdsim.ini
    std::function<bool(const std::string&)> dirExists;                 ///< is there a directory with this path
    std::function<std::optional<std::string>(const std::string&)> readFile;  ///< file contents, nullopt if missing
    std::vector<std::string> nvcGlobalArgs;  ///< put in front of every nvc call, e.g. {"-L", "<libdir>"}
    std::string compatTcl;                   ///< path of tcl/compat/lvdcompat.tcl, empty if not found
    std::string tempDir = ".";               ///< where temporary scripts are written
    std::string defaultStd = "08";           ///< VHDL standard used when no -93/-2002/-2008/-2019 is given
    bool strict = false;                     ///< unsupported options are errors instead of warnings
    std::string guiWaveFile;                 ///< set by the GUI: live waveform stream nvc should write
};

struct NvcCall {
    std::vector<std::string> args;  ///< arguments after the nvc executable
};

struct TempFile {
    std::string path;
    std::string contents;
};

/// What to do for one tool invocation. The planner never executes anything.
struct Plan {
    std::vector<NvcCall> calls;                                  ///< run in order, stop at the first failure
    std::vector<std::string> directories;                        ///< create before running
    std::vector<TempFile> files;                                 ///< write before running, delete afterwards
    std::vector<TempFile> keepFiles;                             ///< write before running and keep (library markers)
    std::vector<std::pair<std::string, std::string>> mappings;   ///< library name -> path to record in lvdsim.ini
    std::vector<std::string> warnings;
    std::string message;                                         ///< informational text for stdout
    std::string error;                                           ///< non-empty: print and exit with 1, run nothing

    /// vsim without -c. A plan with gui set carries no calls when produced for the command line tools:
    /// they start the GUI with guiArgs. The GUI itself asks for a plan with Context::guiWaveFile set and
    /// then runs calls[0] as an interactive shell, sending startupScripts to it.
    bool gui = false;
    std::vector<std::string> guiArgs;
    std::vector<std::string> startupScripts;  ///< sourced in order once the shell is up (GUI only)

    bool ok() const noexcept { return error.empty(); }
};

/// vlib [-type directory] [-unix|-dos] <name>...
Plan planVlib(std::vector<std::string> args, const Context& ctx);
/// vmap [<name> [<path>]]: with a path records a mapping, with a name shows it, without arguments lists all.
Plan planVmap(std::vector<std::string> args, const Context& ctx);
/// vcom [-work lib] [-93|-2002|-2008|-2019] [-L lib] [-f file] files...
Plan planVcom(std::vector<std::string> args, const Context& ctx);
/// vlog [-work lib] [-sv] [+define+X=1] [+incdir+dir] [-L lib] [-f file] files...
Plan planVlog(std::vector<std::string> args, const Context& ctx);
/// vsim [-c] [-do script|"cmds"] [-lib lib] [-L lib] [-g name=value] [-wlf file|-lvdlive file] [lib.]top
Plan planVsim(std::vector<std::string> args, const Context& ctx);

/// Replace every "-f file" in args by the whitespace separated words of that file (recursively, quotes
/// group words). Sets *error and returns the input unchanged if a file cannot be read.
std::vector<std::string> expandArgFiles(const std::vector<std::string>& args,
                                        const std::function<std::optional<std::string>(const std::string&)>& readFile,
                                        std::string* error);

/// Splits "work.top(arch)" or "top" into library, unit and architecture (empty if absent).
struct DesignUnitName {
    std::string library;
    std::string unit;
    std::string architecture;
};
DesignUnitName parseDesignUnit(const std::string& text);

}  // namespace lvd::tools
