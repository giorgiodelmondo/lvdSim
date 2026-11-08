// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "process.hpp"
#include "translate.hpp"

#include <optional>
#include <string>

namespace lvd::tools {

/// Everything the command line tools and the GUI discover about the installation: where nvc is, which
/// libraries are mapped, where the command compatibility script lives. See the variables in
/// docs/command-compat.md (LVDSIM_NVC, LVDSIM_NVC_LIBDIR, LVDSIM_NVC_DLLDIR, LVDSIM_TCL_DIR, LVDSIM_STD,
/// LVDSIM_STRICT, LVDSIM_GUI, LVDSIM_INI).
struct Environment {
    Context ctx;
    std::string nvc;       ///< nvc executable
    std::string iniPath;   ///< lvdsim.ini that vmap writes and the library lookup reads
    std::string nvcDllDir; ///< empty if not needed
    std::string guiExe;    ///< lvdsim-gui executable

    /// Options for starting nvc or the GUI: runtime DLL directory in front of PATH.
    ProcessOptions processOptions() const;
};

/// An nvc shipped inside the installation: <dir>/nvc/bin/nvc[.exe] with its libraries in <dir>/nvc/lib and its
/// runtime DLLs next to the executable. `programDir` is the directory of the running tool; <programDir>/.. is tried
/// as well (the Linux layout keeps programs in bin/).
struct BundledNvc {
    std::string exe;
    std::string libDir; ///< contains std, ieee, ...
    std::string dllDir; ///< directory of nvc.exe on Windows, empty elsewhere
};
std::optional<BundledNvc> findBundledNvc(const std::string& programDir);

Environment discoverEnvironment();

/// Value of an environment variable, empty if unset.
std::string envVar(const char* name);
/// Contents of a regular file, nullopt if it does not exist or cannot be read.
std::optional<std::string> readTextFile(const std::string& path);

} // namespace lvd::tools
