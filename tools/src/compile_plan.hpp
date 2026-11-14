// SPDX-License-Identifier: GPL-3.0-or-later
// Incremental analysis of a project: which source file depends on which (from the unit scanner), the order to
// analyse them in, and which files must be analysed again because they or a file they depend on changed.
//
// What was analysed last time is kept in a small text file in the library root (`.lvdsim-compile-state`): per
// file its time stamp, size, a content hash (also over its `include files) and a hash of its compile options.
// A file whose time stamp and size are unchanged is not read again.
//
// Everything here is pure except the functions marked as reading the disk (stampFile, scanFiles).
#pragma once

#include "hdl_scanner.hpp"
#include "project.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lvd::tools::build {

/// A source file of the project with its place in the project.
struct FileRef {
    int library = 0; ///< index into Project::libraries
    int file = 0;    ///< index into Library::files
    bool operator==(const FileRef&) const = default;
};

/// All files of the project in project order (libraries in order, files in order).
std::vector<FileRef> projectFiles(const proj::Project& p);
/// "library|path": the key a file has in the compile state.
std::string fileKey(const proj::Project& p, FileRef f);

struct Graph {
    std::vector<FileRef> files;          ///< project order
    std::vector<std::vector<int>> deps;  ///< file index -> indices of the files it depends on
    std::vector<std::vector<int>> users; ///< the reverse
    std::vector<std::string> warnings;   ///< units declared twice, dependency cycles
    /// The file that declares a primary unit, by "library.unit" (lower case); -1 if not in the project.
    int fileOfUnit(const std::string& library, const std::string& unit) const;
    std::map<std::string, int> unitFile;
};

/// Builds the dependency graph from one scan per file (in projectFiles order). References to `work` mean the
/// file's own library; a reference without a library is looked up in the file's library first and then in the
/// other libraries in project order; references to libraries outside the project are ignored.
Graph buildGraph(const proj::Project& p, const std::vector<scan::ScanResult>& scans);

/// Analysis order: every file after the files it depends on, otherwise in project order. Files on a cycle
/// keep project order (a warning is added to the graph).
std::vector<int> compileOrder(Graph& g);

/// The files that depend on any of `changed`, directly or not (the changed files included).
std::set<int> withDependents(const Graph& g, const std::set<int>& changed);

struct FileStamp {
    std::int64_t mtime = 0;
    std::uint64_t size = 0;
    std::uint64_t hash = 0;        ///< content (and include files)
    std::uint64_t optionsHash = 0; ///< library, language, standard, defines, include directories
    bool ok = true;                ///< false: the last analysis failed
    bool operator==(const FileStamp&) const = default;
};

/// The compile state file: one line per file `key<TAB>mtime<TAB>size<TAB>hash<TAB>options<TAB>ok|failed`.
struct State {
    std::map<std::string, FileStamp> files;
};
std::string writeState(const State& s);
State parseState(std::string_view text);
/// The state file of a project (relative to the project directory).
std::string statePath(const proj::Project& p);

/// Hash of what, apart from the contents, decides the result of analysing the file.
std::uint64_t optionsHash(const proj::Project& p, FileRef f);

enum class Mode {
    OutOfDate, ///< changed files and the files that depend on them
    All,       ///< every file
    Selected,  ///< the selected files and the files that depend on them
};

enum class Reason { New, Changed, Options, Failed, LibraryMissing, Dependency, Requested };
const char* reasonText(Reason r);

struct Step {
    int file = 0; ///< index into Graph::files
    Reason reason = Reason::Changed;
    int cause = -1; ///< for Dependency: the file it depends on that is analysed again
};

struct Plan {
    std::vector<int> order;  ///< every file, in analysis order
    std::vector<Step> steps; ///< the files to analyse, in analysis order
    bool contains(int file) const;
};

/// Which files to analyse. `current` holds the stamp of every file now (Graph::files order; a file that
/// cannot be read has nullopt and is analysed so that nvc reports it), `libraryExists` says whether a library
/// directory holds an nvc library (index into Project::libraries).
Plan planCompile(const proj::Project& p, Graph& g, const State& state,
                 const std::vector<std::optional<FileStamp>>& current, const std::vector<bool>& libraryExists,
                 Mode mode, const std::set<int>& selected = {});

// ---- reading the disk

/// Absolute path of a project path (relative to the project directory unless absolute), forward slashes.
std::string absolutePath(const std::string& projectDir, const std::string& path);

/// The stamp of a file now. When `previous` has the same time stamp and size its hash is reused without
/// reading the file. Include files are looked up next to the file and in its include directories.
std::optional<FileStamp> stampFile(const proj::Project& p, const std::string& projectDir, FileRef f,
                                   const FileStamp* previous);

/// Reads and scans every file (unreadable files give an empty scan).
std::vector<scan::ScanResult> scanFiles(const proj::Project& p, const std::string& projectDir,
                                        const std::vector<FileRef>& files);

/// Whether the directory of each library is an nvc library (contains _NVC_LIB).
std::vector<bool> librariesPresent(const proj::Project& p, const std::string& projectDir);

} // namespace lvd::tools::build
