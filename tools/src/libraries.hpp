// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "translate.hpp"

#include <string>
#include <vector>

namespace lvd::tools {

/// A design unit found in a library directory.
struct LibraryUnit {
    std::string name;                       ///< lower case
    std::vector<std::string> architectures; ///< lower case, sorted (package bodies show up here too)
    bool elaborated = false;                ///< an elaborated form exists, so it can be simulated at once
};

struct LibraryInfo {
    std::string name; ///< as mapped, or the directory name
    std::string path;
    std::vector<LibraryUnit> units; ///< sorted by name
};

/// Finds the libraries a user can see: the [Library] mappings of lvdsim.ini, plus every directory directly
/// below `root` that NVC recognises as a library (it contains `_NVC_LIB`). Each is scanned for its units
/// from the file names NVC uses (`WORK.T`, `WORK.T-A`, `WORK.T.elab`). Libraries are sorted by name and
/// listed once. Missing directories are skipped.
std::vector<LibraryInfo> scanLibraries(const Context& ctx, const std::string& root = ".");

/// The libraries (directories that contain `_NVC_LIB`) directly below a library folder, for the "library folders"
/// setting of the GUI. Names are the directory names; sorted by name.
struct FolderLibrary {
    std::string name;
    std::string path; ///< forward slashes
};
std::vector<FolderLibrary> librariesInFolder(const std::string& folder);

/// `-L name` options (as vsim words) for every library of the given folders, skipping names in `skip` (compared
/// without case), the first folder wins when two folders hold the same name. Also reports the name/path pairs.
std::vector<std::string> searchPathArgs(const std::vector<std::string>& folders, const std::vector<std::string>& skip,
                                        std::vector<FolderLibrary>* libraries = nullptr);

} // namespace lvd::tools
