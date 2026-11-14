// SPDX-License-Identifier: GPL-3.0-or-later
// The lvdsim project file (.lvdproj): libraries with their source files in compile order, the tests of the
// regression and a few settings. The format is described in docs/projects.md. In short: an ini-like text with
// `[project]`, `[library NAME]` and `[test NAME]` sections, one `key = value` per line, keys that can repeat
// (`file`, `generic`, ...) keep their order, and only whole lines starting with `#` are comments (so values
// such as 16#FF# or a regular expression can contain '#'). One source file per line keeps diffs readable.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lvd::tools::proj {

enum class Language { Vhdl, Verilog, SystemVerilog };

/// Language of a source file from its extension (.v .vh .vlog: Verilog; .sv .svh: SystemVerilog; anything
/// else is VHDL).
Language languageOf(std::string_view path);

/// A key with its value that the reader does not know; written back unchanged.
using ExtraKey = std::pair<std::string, std::string>;

struct SourceFile {
    std::string path;     ///< as written in the project: relative to the project directory (or absolute)
    std::string vhdl;     ///< VHDL standard of this file: "93", "02", "08", "19"; empty: the project default
    bool relaxed = false; ///< VHDL: nvc --relaxed (vcom -explicit)
    std::vector<std::string> defines; ///< Verilog: NAME or NAME=VALUE
    std::vector<std::string> incdirs; ///< Verilog include directories (relative to the project)
    std::optional<Language> language; ///< forced language ("sv", "verilog", "vhdl" option); else extension
    Language effectiveLanguage() const { return language ? *language : languageOf(path); }
    bool isVerilog() const { return effectiveLanguage() != Language::Vhdl; }
};

struct Library {
    std::string name;
    std::string path; ///< library directory; empty: <library_root>/<name>
    std::vector<SourceFile> files;
    std::vector<ExtraKey> extra;
};

struct Test {
    std::string name;
    std::string top;                   ///< [library.]unit; several top levels separated by spaces
    std::vector<std::string> generics; ///< NAME=VALUE, given to nvc as -gNAME=VALUE
    std::vector<std::string> plusargs; ///< +name or +name=value, given to the run
    std::string doScript;              ///< Tcl script run in the simulator (relative to the project)
    std::string commands;              ///< inline Tcl commands (used when there is no do script)
    std::vector<std::uint64_t> seeds;  ///< one run per seed (nvc --seed); empty: one run without a seed
    std::string seedGeneric;           ///< also pass the seed as this generic (for VHDL random generators)
    int timeoutSec = 0;                ///< 0: the project's timeout
    std::string expect;                ///< regular expression the output must contain to pass
    std::string stopTime;              ///< without a script: stop the run at this time (for example 1 ms)
    std::optional<bool> wave;          ///< record a waveform (FST); unset: the project setting
    std::optional<bool> coverage;      ///< collect coverage; unset: the project setting
    std::vector<ExtraKey> extra;
};

struct Settings {
    std::string name;
    std::string vhdl = "08";                 ///< default VHDL standard
    std::string libraryRoot = "lvdsim_libs"; ///< where libraries without a path are created
    std::vector<std::string> libraryFolders; ///< folders of precompiled libraries (every library in them)
    std::vector<std::pair<std::string, std::string>> mappings; ///< external library name -> directory
    int jobs = 0;         ///< parallel regression jobs; 0: number of processors
    int timeoutSec = 600; ///< per test run
    bool wave = false;
    bool coverage = false;
    std::string coverageKinds; ///< empty: every kind (see cov::parseKinds)
    std::string regressionDir = "lvdsim_regression";
    std::vector<ExtraKey> extra;
};

struct Project {
    Settings settings;
    std::vector<Library> libraries;
    std::vector<Test> tests;

    Library* findLibrary(std::string_view name);
    const Library* findLibrary(std::string_view name) const;
    const Test* findTest(std::string_view name) const;
    /// Directory of a library as the project says (relative to the project directory unless absolute).
    std::string libraryPath(const Library& lib) const;
    /// The VHDL standard of a file ("93", "02", "08", "19").
    std::string vhdlOf(const SourceFile& f) const;
    /// Number of source files over all libraries.
    std::size_t fileCount() const;
};

struct ParseMessage {
    int line = 0; ///< 1 based
    std::string text;
};

struct ParseResult {
    Project project;
    std::vector<ParseMessage> errors;   ///< the project should not be used
    std::vector<ParseMessage> warnings; ///< unknown keys (kept), duplicated names
    bool ok() const noexcept { return errors.empty(); }
};

/// Reads a project file. Unknown keys are kept (and written back) with a warning; malformed lines, unknown
/// section kinds and bad values are errors.
ParseResult parseProject(std::string_view text);

/// The project as text, in canonical order (sections and keys as documented). Comments of a file that was
/// read are not kept.
std::string writeProject(const Project& project);

/// "2008", "08", "-2008", "1993", "93" ... -> "08", "93"; empty if not a VHDL standard nvc knows.
std::string normaliseVhdlStd(std::string_view text);

/// A name that may be used for a library or a test: letters, digits, '_', '-', '.'; not empty.
bool isValidName(std::string_view name);

/// A project read from disk.
struct LoadedProject {
    Project project;
    std::string path; ///< absolute, forward slashes
    std::string dir;  ///< absolute directory of the file: project paths are relative to it
    std::vector<ParseMessage> warnings;
    std::string error; ///< empty on success (all parse errors, one per line)
    bool ok() const noexcept { return error.empty(); }
};
LoadedProject loadProject(const std::string& path);
/// Writes writeProject(p) to the file. False with *error on failure.
bool saveProject(const Project& p, const std::string& path, std::string* error);

} // namespace lvd::tools::proj
