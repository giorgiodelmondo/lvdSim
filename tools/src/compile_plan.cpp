// SPDX-License-Identifier: GPL-3.0-or-later
#include "compile_plan.hpp"

#include "environment.hpp"
#include "text_util.hpp"

#include <queue>

#include <charconv>
#include <filesystem>

namespace fs = std::filesystem;

namespace lvd::tools::build {

using text::lower;

std::vector<FileRef> projectFiles(const proj::Project& p) {
    std::vector<FileRef> out;
    for (int l = 0; l < static_cast<int>(p.libraries.size()); ++l)
        for (int f = 0; f < static_cast<int>(p.libraries[static_cast<std::size_t>(l)].files.size()); ++f)
            out.push_back({l, f});
    return out;
}

namespace {

const proj::Library& libOf(const proj::Project& p, FileRef f) {
    return p.libraries.at(static_cast<std::size_t>(f.library));
}

const proj::SourceFile& fileOf(const proj::Project& p, FileRef f) {
    return libOf(p, f).files.at(static_cast<std::size_t>(f.file));
}

} // namespace

std::string fileKey(const proj::Project& p, FileRef f) {
    return lower(libOf(p, f).name) + "|" + fileOf(p, f).path;
}

int Graph::fileOfUnit(const std::string& library, const std::string& unit) const {
    const auto it = unitFile.find(lower(library) + "." + lower(unit));
    return it == unitFile.end() ? -1 : it->second;
}

Graph buildGraph(const proj::Project& p, const std::vector<scan::ScanResult>& scans) {
    Graph g;
    g.files = projectFiles(p);
    const int n = static_cast<int>(g.files.size());
    g.deps.assign(static_cast<std::size_t>(n), {});
    g.users.assign(static_cast<std::size_t>(n), {});

    for (int i = 0; i < n && i < static_cast<int>(scans.size()); ++i) {
        const std::string lib = lower(libOf(p, g.files[static_cast<std::size_t>(i)]).name);
        for (const scan::UnitDecl& u : scans[static_cast<std::size_t>(i)].units) {
            if (!scan::isPrimary(u.kind))
                continue;
            const std::string key = lib + "." + u.name;
            const auto [it, inserted] = g.unitFile.emplace(key, i);
            if (!inserted && it->second != i) {
                g.warnings.push_back("unit " + key + " is declared in " +
                                     fileOf(p, g.files[static_cast<std::size_t>(it->second)]).path +
                                     " and in " + fileOf(p, g.files[static_cast<std::size_t>(i)]).path +
                                     "; the later file is used");
                it->second = i;
            }
        }
    }

    for (int i = 0; i < n && i < static_cast<int>(scans.size()); ++i) {
        const FileRef f = g.files[static_cast<std::size_t>(i)];
        const std::string own = lower(libOf(p, f).name);
        std::set<int> deps;
        for (const scan::UnitRef& r : scans[static_cast<std::size_t>(i)].refs) {
            int target = -1;
            if (r.library.empty()) {
                target = g.fileOfUnit(own, r.name);
                for (std::size_t l = 0; target < 0 && l < p.libraries.size(); ++l)
                    target = g.fileOfUnit(p.libraries[l].name, r.name);
            } else {
                target = g.fileOfUnit(r.library == "work" ? own : r.library, r.name);
            }
            if (target >= 0 && target != i)
                deps.insert(target);
        }
        g.deps[static_cast<std::size_t>(i)].assign(deps.begin(), deps.end());
        for (const int d : deps)
            g.users[static_cast<std::size_t>(d)].push_back(i);
    }
    return g;
}

std::vector<int> compileOrder(Graph& g) {
    const int n = static_cast<int>(g.files.size());
    std::vector<int> pending(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i)
        pending[static_cast<std::size_t>(i)] = static_cast<int>(g.deps[static_cast<std::size_t>(i)].size());
    std::priority_queue<int, std::vector<int>, std::greater<>> ready;
    for (int i = 0; i < n; ++i)
        if (pending[static_cast<std::size_t>(i)] == 0)
            ready.push(i);
    std::vector<int> order;
    std::vector<bool> done(static_cast<std::size_t>(n), false);
    while (static_cast<int>(order.size()) < n) {
        if (ready.empty()) {
            // A cycle: take the first file left in project order and go on.
            int first = -1;
            for (int i = 0; i < n && first < 0; ++i)
                if (!done[static_cast<std::size_t>(i)])
                    first = i;
            g.warnings.push_back("dependency cycle involving file " + std::to_string(first + 1) +
                                 "; project order is used for it");
            pending[static_cast<std::size_t>(first)] = 0;
            ready.push(first);
        }
        const int i = ready.top();
        ready.pop();
        if (done[static_cast<std::size_t>(i)])
            continue;
        done[static_cast<std::size_t>(i)] = true;
        order.push_back(i);
        for (const int u : g.users[static_cast<std::size_t>(i)])
            if (!done[static_cast<std::size_t>(u)] && --pending[static_cast<std::size_t>(u)] <= 0)
                ready.push(u);
    }
    return order;
}

std::set<int> withDependents(const Graph& g, const std::set<int>& changed) {
    std::set<int> out = changed;
    std::vector<int> work(changed.begin(), changed.end());
    while (!work.empty()) {
        const int i = work.back();
        work.pop_back();
        for (const int u : g.users[static_cast<std::size_t>(i)])
            if (out.insert(u).second)
                work.push_back(u);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// State

std::string writeState(const State& s) {
    std::string out = "# lvdsim compile state; delete to analyse everything again\n";
    for (const auto& [key, st] : s.files) {
        out += key + '\t' + std::to_string(st.mtime) + '\t' + std::to_string(st.size) + '\t' +
               std::to_string(st.hash) + '\t' + std::to_string(st.optionsHash) + '\t' +
               (st.ok ? "ok" : "failed") + '\n';
    }
    return out;
}

namespace {

template <typename T>
bool number(std::string_view s, T& out) {
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size();
}

} // namespace

State parseState(std::string_view text) {
    State s;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line =
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.empty() || line.front() == '#')
            continue;
        std::vector<std::string_view> f;
        std::size_t a = 0;
        while (true) {
            const std::size_t t = line.find('\t', a);
            f.push_back(line.substr(a, t == std::string_view::npos ? std::string_view::npos : t - a));
            if (t == std::string_view::npos)
                break;
            a = t + 1;
        }
        if (f.size() != 6)
            continue;
        FileStamp st;
        if (!number(f[1], st.mtime) || !number(f[2], st.size) || !number(f[3], st.hash) ||
            !number(f[4], st.optionsHash))
            continue;
        st.ok = f[5] == "ok";
        s.files[std::string(f[0])] = st;
    }
    return s;
}

std::string statePath(const proj::Project& p) {
    std::string root = p.settings.libraryRoot.empty() ? std::string(".") : p.settings.libraryRoot;
    return root + "/.lvdsim-compile-state";
}

std::uint64_t optionsHash(const proj::Project& p, FileRef f) {
    const proj::Library& lib = libOf(p, f);
    const proj::SourceFile& sf = fileOf(p, f);
    std::vector<std::string> w = {lower(lib.name), p.libraryPath(lib),
                                  std::to_string(static_cast<int>(sf.effectiveLanguage()))};
    if (!sf.isVerilog()) {
        w.push_back(p.vhdlOf(sf));
        w.push_back(sf.relaxed ? "relaxed" : "");
    }
    w.push_back("defines");
    w.insert(w.end(), sf.defines.begin(), sf.defines.end());
    w.push_back("incdirs");
    w.insert(w.end(), sf.incdirs.begin(), sf.incdirs.end());
    return text::hashWords(w);
}

const char* reasonText(Reason r) {
    switch (r) {
    case Reason::New:
        return "not analysed yet";
    case Reason::Changed:
        return "changed";
    case Reason::Options:
        return "options changed";
    case Reason::Failed:
        return "failed last time";
    case Reason::LibraryMissing:
        return "library missing";
    case Reason::Dependency:
        return "depends on";
    case Reason::Requested:
        return "requested";
    }
    return "";
}

bool Plan::contains(int file) const {
    for (const Step& s : steps)
        if (s.file == file)
            return true;
    return false;
}

Plan planCompile(const proj::Project& p, Graph& g, const State& state,
                 const std::vector<std::optional<FileStamp>>& current, const std::vector<bool>& libraryExists,
                 Mode mode, const std::set<int>& selected) {
    Plan plan;
    plan.order = compileOrder(g);
    const int n = static_cast<int>(g.files.size());
    std::map<int, Reason> reasons;
    for (int i = 0; i < n; ++i) {
        const FileRef f = g.files[static_cast<std::size_t>(i)];
        std::optional<Reason> r;
        if (mode == Mode::All) {
            r = Reason::Requested;
        } else if (mode == Mode::Selected) {
            if (selected.count(i) != 0)
                r = Reason::Requested;
        } else {
            const auto it = state.files.find(fileKey(p, f));
            const std::optional<FileStamp>& cur =
                i < static_cast<int>(current.size()) ? current[static_cast<std::size_t>(i)] : std::nullopt;
            const bool libOk = f.library < static_cast<int>(libraryExists.size()) &&
                               libraryExists[static_cast<std::size_t>(f.library)];
            if (!libOk)
                r = Reason::LibraryMissing;
            else if (it == state.files.end())
                r = Reason::New;
            else if (!cur)
                r = Reason::Changed;
            else if (!it->second.ok)
                r = Reason::Failed;
            else if (it->second.optionsHash != cur->optionsHash)
                r = Reason::Options;
            else if (it->second.hash != cur->hash)
                r = Reason::Changed;
        }
        if (r)
            reasons[i] = *r;
    }
    std::set<int> direct;
    for (const auto& [i, r] : reasons)
        direct.insert(i);
    const std::set<int> all = withDependents(g, direct);
    for (const int i : plan.order) {
        if (all.count(i) == 0)
            continue;
        Step s;
        s.file = i;
        if (const auto it = reasons.find(i); it != reasons.end()) {
            s.reason = it->second;
        } else {
            s.reason = Reason::Dependency;
            for (const int d : g.deps[static_cast<std::size_t>(i)])
                if (all.count(d) != 0) {
                    s.cause = d;
                    break;
                }
        }
        plan.steps.push_back(s);
    }
    return plan;
}

// ---------------------------------------------------------------------------------------------
// Disk

std::string absolutePath(const std::string& projectDir, const std::string& path) {
    const fs::path pp(path);
    if (pp.is_absolute() || projectDir.empty())
        return pp.lexically_normal().generic_string();
    return (fs::path(projectDir) / pp).lexically_normal().generic_string();
}

namespace {

std::optional<std::string> findInclude(const std::string& name, const std::vector<std::string>& dirs) {
    for (const std::string& d : dirs) {
        const fs::path c = fs::path(d) / name;
        std::error_code ec;
        if (fs::is_regular_file(c, ec))
            return c.generic_string();
    }
    return std::nullopt;
}

std::uint64_t includeHash(const std::string& text, const std::vector<std::string>& dirs, std::uint64_t h,
                          int depth) {
    if (depth > 6)
        return h;
    for (const std::string& inc : scan::scanVerilog(text).includes) {
        h = text::fnv1a(inc, h);
        const auto path = findInclude(inc, dirs);
        if (!path)
            continue;
        const auto body = readTextFile(*path);
        if (!body)
            continue;
        h = text::fnv1a(*body, h);
        h = includeHash(*body, dirs, h, depth + 1);
    }
    return h;
}

} // namespace

std::optional<FileStamp> stampFile(const proj::Project& p, const std::string& projectDir, FileRef f,
                                   const FileStamp* previous) {
    const proj::SourceFile& sf = fileOf(p, f);
    const std::string path = absolutePath(projectDir, sf.path);
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec)
        return std::nullopt;
    const auto time = fs::last_write_time(path, ec);
    if (ec)
        return std::nullopt;
    FileStamp st;
    st.size = static_cast<std::uint64_t>(size);
    st.mtime = static_cast<std::int64_t>(time.time_since_epoch().count());
    st.optionsHash = optionsHash(p, f);
    // VHDL files have no include files: same time stamp and size means same contents. Verilog files are read
    // every time, as an include file may have changed.
    if (previous != nullptr && previous->mtime == st.mtime && previous->size == st.size && !sf.isVerilog()) {
        st.hash = previous->hash;
        return st;
    }
    const auto body = readTextFile(path);
    if (!body)
        return std::nullopt;
    st.hash = text::fnv1a(*body);
    if (sf.isVerilog()) {
        std::vector<std::string> dirs = {fs::path(path).parent_path().generic_string()};
        for (const std::string& d : sf.incdirs)
            dirs.push_back(absolutePath(projectDir, d));
        dirs.push_back(projectDir.empty() ? std::string(".") : projectDir);
        st.hash = includeHash(*body, dirs, st.hash, 0);
    }
    return st;
}

std::vector<scan::ScanResult> scanFiles(const proj::Project& p, const std::string& projectDir,
                                        const std::vector<FileRef>& files) {
    std::vector<scan::ScanResult> out;
    out.reserve(files.size());
    for (const FileRef f : files) {
        const proj::SourceFile& sf = fileOf(p, f);
        const auto body = readTextFile(absolutePath(projectDir, sf.path));
        if (!body)
            out.emplace_back();
        else
            out.push_back(sf.isVerilog() ? scan::scanVerilog(*body) : scan::scanVhdl(*body));
    }
    return out;
}

std::vector<bool> librariesPresent(const proj::Project& p, const std::string& projectDir) {
    std::vector<bool> out;
    for (const proj::Library& l : p.libraries) {
        std::error_code ec;
        out.push_back(fs::exists(fs::path(absolutePath(projectDir, p.libraryPath(l))) / "_NVC_LIB", ec));
    }
    return out;
}

} // namespace lvd::tools::build
