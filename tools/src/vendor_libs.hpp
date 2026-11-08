// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace lvd::tools {

/// Pure helpers behind the GUI's "Compile libraries" dialog: which libraries each vendor script can build,
/// where the vendor installations are, the argument list of scripts/libs/compile_libs.{sh,bat} and how to
/// read what it prints. Nothing here starts a process.

enum class Vendor { Xilinx, Altera };

struct LibraryChoice {
    std::string id;          ///< word accepted by the script's --only
    std::string description; ///< one line for the check box
    bool defaultOn = false;
};

/// The libraries of a vendor in the order they are shown. The defaults are the ones the script builds without
/// --only.
const std::vector<LibraryChoice>& libraryChoices(Vendor vendor);
std::vector<std::string> defaultLibraries(Vendor vendor);
/// How many library folders the script creates for these ids (altera_verilog makes four).
int expectedLibraryCount(const std::vector<std::string>& ids);

/// An installation found on disk or named by an environment variable.
struct InstallCandidate {
    std::string path;  ///< version folder, forward slashes
    std::string label; ///< "2022.2  (D:/cad/Xilinx/Vivado/2022.2)"
    bool fromEnvironment = false;
};

/// Does this folder look like a Vivado (data/vhdl/src/unisims) or Quartus (eda/sim_lib, or
/// quartus/eda/sim_lib) install.
bool looksLikeInstall(Vendor vendor, const std::string& dir);

/// Candidates: first the folders named by `envValues` (the values of XILINX_VIVADO, or QUARTUS_ROOTDIR and
/// ALTERA_ROOT; empty ones are ignored), then the valid version folders one or two levels below each of
/// `roots`, newest name first. A QUARTUS_ROOTDIR that points at <version>/quartus is reported as <version>.
/// Duplicates are listed once.
std::vector<InstallCandidate> detectInstalls(Vendor vendor, const std::vector<std::string>& envValues,
                                             const std::vector<std::string>& roots);
/// The folders worth scanning on this machine (C:/Xilinx, D:/cad/Xilinx, C:/intelFPGA_pro, /opt/Xilinx, ...).
std::vector<std::string> defaultInstallRoots(Vendor vendor);
/// Environment variable names consulted for a vendor.
std::vector<std::string> installEnvironmentVariables(Vendor vendor);
/// detectInstalls with the real environment and default roots.
std::vector<InstallCandidate> detectInstalls(Vendor vendor);

struct ScriptRequest {
    Vendor vendor = Vendor::Xilinx;
    std::string installDir;
    std::string outDir;
    std::vector<std::string> libraries;
    bool quiet = false;
};
/// Arguments after the script name: --vivado|--quartus DIR --out DIR --only a,b[,c] [--quiet]. Empty install,
/// output or library list gives an error text in `error`.
std::vector<std::string> buildScriptArgs(const ScriptRequest& request, std::string* error = nullptr);

/// compile_libs.bat or compile_libs.sh: <appDir>/scripts/libs (bundled), <appDir>/../scripts/libs, then every
/// parent directory up to eight levels (the source tree during development). Empty if not found.
std::string findLibsScript(const std::string& appDir, bool windows,
                           const std::function<bool(const std::string&)>& fileExists);

/// Command line for the platform: on Windows `cmd.exe` with a native argument string that survives spaces in
/// paths
/// (/d /s /c ""script" "arg" ..."), elsewhere `sh script args...`.
struct ScriptCommand {
    std::string program;
    std::vector<std::string> args;
    std::string nativeArguments; ///< Windows only: pass as-is instead of `args`
};
ScriptCommand scriptCommand(bool windows, const std::string& script, const std::vector<std::string>& args);

/// One line of the script's output.
enum class LineKind { Other, Started, LibraryDone, SummaryRow, Skipped, Failed };
struct ScriptLine {
    LineKind kind = LineKind::Other;
    std::string library;
    int ok = 0, failed = 0, skipped = 0, seconds = 0;
};
ScriptLine parseScriptLine(const std::string& line);

struct Summary {
    std::vector<ScriptLine> rows; ///< the table at the end
    int librariesOk = 0, librariesFailed = 0;
    int filesOk = 0, filesFailed = 0, filesSkipped = 0;
};
/// Reads the table that follows the header `library  files ok  failed  skipped  time`.
Summary parseSummary(const std::string& output);
/// "9 libraries (8 ok, 1 with failed files): 20 files ok, 1 failed, 0 skipped" (no table: "no summary
/// printed").
std::string summaryText(const Summary& summary);

} // namespace lvd::tools
