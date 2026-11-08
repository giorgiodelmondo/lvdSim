// SPDX-License-Identifier: GPL-3.0-or-later
#include "environment.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#define LVD_GETPID _getpid
#else
#include <unistd.h>
#define LVD_GETPID getpid
#endif

namespace fs = std::filesystem;

namespace lvd::tools {

std::string envVar(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
}

std::optional<std::string> readTextFile(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return std::nullopt;
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

namespace {

fs::path programDir() {
    const std::string exe = executablePath();
    return exe.empty() ? fs::path() : fs::path(exe).parent_path();
}

std::string findNextToProgram(std::initializer_list<const char*> names, const char* fallback) {
    const fs::path dir = programDir();
    for (const char* name : names) {
        const fs::path p = dir / name;
        std::error_code ec;
        if (!dir.empty() && fs::is_regular_file(p, ec))
            return p.string();
    }
    return fallback;
}

std::string findCompatTcl() {
    std::vector<fs::path> candidates;
    if (const std::string d = envVar("LVDSIM_TCL_DIR"); !d.empty())
        candidates.emplace_back(fs::path(d) / "lvdcompat.tcl");
    const fs::path dir = programDir();
    if (!dir.empty()) {
        candidates.push_back(dir / "tcl" / "compat" / "lvdcompat.tcl");
        candidates.push_back(dir / ".." / "tcl" / "compat" / "lvdcompat.tcl");
    }
#ifdef LVDSIM_SOURCE_TCL_DIR
    candidates.emplace_back(fs::path(LVDSIM_SOURCE_TCL_DIR) / "lvdcompat.tcl");
#endif
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec))
            return fs::weakly_canonical(c, ec).generic_string();
    }
    return {};
}

} // namespace

std::optional<BundledNvc> findBundledNvc(const std::string& programDir) {
    if (programDir.empty())
        return std::nullopt;
    const fs::path base(programDir);
    for (const fs::path& root : {base / "nvc", base / ".." / "nvc"}) {
        std::error_code ec;
        const fs::path lib = root / "lib";
        if (!fs::is_directory(lib / "std", ec) && !fs::is_regular_file(lib / "std", ec))
            continue;
#ifdef _WIN32
        const fs::path exe = root / "bin" / "nvc.exe";
#else
        const fs::path exe = root / "bin" / "nvc";
#endif
        if (!fs::is_regular_file(exe, ec))
            continue;
        BundledNvc b;
        b.exe = fs::weakly_canonical(exe, ec).generic_string();
        b.libDir = fs::weakly_canonical(lib, ec).generic_string();
#ifdef _WIN32
        b.dllDir = fs::weakly_canonical(root / "bin", ec).generic_string();
#endif
        return b;
    }
    return std::nullopt;
}

ProcessOptions Environment::processOptions() const {
    ProcessOptions o;
    if (!nvcDllDir.empty())
        o.pathPrepend.push_back(nvcDllDir);
    return o;
}

Environment discoverEnvironment() {
    Environment e;
    e.iniPath = !envVar("LVDSIM_INI").empty() ? envVar("LVDSIM_INI") : "lvdsim.ini";
    // Order: LVDSIM_NVC, nvc shipped in <program dir>/nvc, nvc next to the program, nvc on PATH.
    const std::optional<BundledNvc> bundled = findBundledNvc(programDir().string());
    const bool useBundled = envVar("LVDSIM_NVC").empty() && bundled.has_value();
    e.nvc = !envVar("LVDSIM_NVC").empty() ? envVar("LVDSIM_NVC")
            : useBundled                  ? bundled->exe
                                          : findNextToProgram({"nvc.exe", "nvc"}, "nvc");
    e.guiExe = !envVar("LVDSIM_GUI").empty()
                   ? envVar("LVDSIM_GUI")
                   : findNextToProgram({"lvdsim-gui.exe", "lvdsim-gui"}, "lvdsim-gui");
    e.nvcDllDir = envVar("LVDSIM_NVC_DLLDIR");
    if (useBundled && e.nvcDllDir.empty())
        e.nvcDllDir = bundled->dllDir;

    Context& c = e.ctx;
    c.libs = LibraryMap::parse(readTextFile(e.iniPath).value_or(""));
    c.dirExists = [](const std::string& p) {
        std::error_code ec;
        return fs::is_directory(p, ec);
    };
    c.readFile = readTextFile;
    const std::string libDir = !envVar("LVDSIM_NVC_LIBDIR").empty() ? envVar("LVDSIM_NVC_LIBDIR")
                               : useBundled                          ? bundled->libDir
                                                                     : std::string();
    if (!libDir.empty()) {
        c.nvcGlobalArgs.push_back("-L");
        c.nvcGlobalArgs.push_back(libDir);
    }
    // Extra global nvc options, split at spaces (for example "-M 128m" for very large packages).
    if (const std::string o = envVar("LVDSIM_NVC_OPTS"); !o.empty()) {
        std::string word;
        for (const char ch : o + ' ') {
            if (ch == ' ' || ch == '	') {
                if (!word.empty())
                    c.nvcGlobalArgs.push_back(word);
                word.clear();
            } else {
                word += ch;
            }
        }
    }
    c.compatTcl = findCompatTcl();
    c.tempDir = (fs::temp_directory_path() / ("lvdsim_" + std::to_string(LVD_GETPID()))).generic_string();
    if (const std::string s = envVar("LVDSIM_STD"); !s.empty())
        c.defaultStd = s;
    c.strict = !envVar("LVDSIM_STRICT").empty();
    return e;
}

} // namespace lvd::tools
