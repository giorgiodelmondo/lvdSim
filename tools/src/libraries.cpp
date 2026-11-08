// SPDX-License-Identifier: GPL-3.0-or-later
#include "libraries.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace lvd::tools {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

LibraryInfo scan(const std::string& name, const fs::path& dir) {
    LibraryInfo info;
    info.name = name;
    info.path = dir.generic_string();

    std::map<std::string, LibraryUnit> units;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        const std::string file = e.path().filename().string();
        const std::size_t dot = file.find('.');
        if (dot == std::string::npos || dot == 0)
            continue; // _index, _NVC_LIB and other bookkeeping
        std::string rest = file.substr(dot + 1);

        bool elab = false;
        if (endsWith(rest, ".elab")) {
            elab = true;
            rest = rest.substr(0, rest.size() - 5);
        }
        std::string arch;
        const std::size_t dash = rest.find('-');
        if (dash != std::string::npos) {
            arch = rest.substr(dash + 1);
            rest = rest.substr(0, dash);
        }
        if (rest.empty())
            continue;

        LibraryUnit& u = units[lower(rest)];
        u.name = lower(rest);
        if (elab)
            u.elaborated = true;
        if (!arch.empty())
            u.architectures.push_back(lower(arch));
    }

    for (auto& [key, u] : units) {
        std::sort(u.architectures.begin(), u.architectures.end());
        u.architectures.erase(std::unique(u.architectures.begin(), u.architectures.end()),
                              u.architectures.end());
        info.units.push_back(std::move(u));
    }
    return info;
}

} // namespace

std::vector<LibraryInfo> scanLibraries(const Context& ctx, const std::string& root) {
    std::vector<std::pair<std::string, fs::path>> found;

    for (const auto& [name, path] : ctx.libs.entries()) {
        std::error_code ec;
        if (fs::is_directory(path, ec))
            found.emplace_back(name, path);
    }

    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(root.empty() ? "." : root, ec)) {
        std::error_code e2;
        if (e.is_directory(e2) && fs::exists(e.path() / "_NVC_LIB", e2))
            found.emplace_back(e.path().filename().string(), e.path());
    }

    std::vector<LibraryInfo> result;
    std::set<std::string> seenPaths, seenNames;
    for (const auto& [name, path] : found) {
        std::error_code e3;
        const std::string canonical = fs::weakly_canonical(path, e3).generic_string();
        if (!seenPaths.insert(canonical).second || !seenNames.insert(lower(name)).second)
            continue;
        result.push_back(scan(name, path));
    }
    std::sort(result.begin(), result.end(),
              [](const LibraryInfo& a, const LibraryInfo& b) { return lower(a.name) < lower(b.name); });
    return result;
}

std::vector<FolderLibrary> librariesInFolder(const std::string& folder) {
    std::vector<FolderLibrary> result;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(folder, ec)) {
        std::error_code e2;
        if (e.is_directory(e2) && fs::exists(e.path() / "_NVC_LIB", e2))
            result.push_back({e.path().filename().string(), e.path().generic_string()});
    }
    std::sort(result.begin(), result.end(),
              [](const FolderLibrary& a, const FolderLibrary& b) { return lower(a.name) < lower(b.name); });
    return result;
}

std::vector<std::string> searchPathArgs(const std::vector<std::string>& folders, const std::vector<std::string>& skip,
                                        std::vector<FolderLibrary>* libraries) {
    std::set<std::string> taken;
    for (const std::string& s : skip)
        taken.insert(lower(s));
    std::vector<std::string> args;
    for (const std::string& folder : folders) {
        for (const FolderLibrary& l : librariesInFolder(folder)) {
            if (!taken.insert(lower(l.name)).second)
                continue;
            args.push_back("-L");
            args.push_back(l.name);
            if (libraries)
                libraries->push_back(l);
        }
    }
    return args;
}

} // namespace lvd::tools
