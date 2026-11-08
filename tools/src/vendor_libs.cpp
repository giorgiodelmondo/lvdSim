// SPDX-License-Identifier: GPL-3.0-or-later
#include "vendor_libs.hpp"

#include "environment.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <system_error>

namespace lvd::tools {

namespace fs = std::filesystem;

namespace {

std::string lowerCase(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string slashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    while (s.size() > 1 && s.back() == '/' && !(s.size() == 3 && s[1] == ':'))
        s.pop_back();
    return s;
}

bool isDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// "2022.10" sorts after "2022.2": digit runs compare as numbers.
bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) &&
            std::isdigit(static_cast<unsigned char>(b[j]))) {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && std::isdigit(static_cast<unsigned char>(a[i2])))
                ++i2;
            while (j2 < b.size() && std::isdigit(static_cast<unsigned char>(b[j2])))
                ++j2;
            const std::string na = a.substr(i, i2 - i), nb = b.substr(j, j2 - j);
            const std::string ta = na.substr(std::min(na.find_first_not_of('0'), na.size()));
            const std::string tb = nb.substr(std::min(nb.find_first_not_of('0'), nb.size()));
            if (ta.size() != tb.size())
                return ta.size() < tb.size();
            if (ta != tb)
                return ta < tb;
            i = i2;
            j = j2;
        } else {
            const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
            const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[j])));
            if (ca != cb)
                return ca < cb;
            ++i;
            ++j;
        }
    }
    return (a.size() - i) < (b.size() - j);
}

std::string labelFor(const std::string& path) {
    const fs::path p(path);
    std::string name = p.filename().string();
    if (name.empty())
        name = p.parent_path().filename().string();
    return name + "  (" + path + ")";
}

// <version>/quartus -> <version> when that is an install as well.
std::string normalised(Vendor vendor, std::string path) {
    path = slashes(std::move(path));
    if (vendor == Vendor::Altera) {
        const std::string lower = lowerCase(path);
        const std::string tail = "/quartus";
        if (lower.size() > tail.size() && lower.compare(lower.size() - tail.size(), tail.size(), tail) == 0) {
            const std::string parent = path.substr(0, path.size() - tail.size());
            if (looksLikeInstall(vendor, parent))
                return parent;
        }
    }
    return path;
}

void scan(Vendor vendor, const fs::path& dir, int depth, std::vector<std::string>& found) {
    std::error_code ec;
    std::vector<std::string> names;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2))
            names.push_back(it->path().filename().string());
    }
    for (const std::string& n : names) {
        const fs::path child = dir / n;
        const std::string s = slashes(child.generic_string());
        if (looksLikeInstall(vendor, s))
            found.push_back(s);
        else if (depth > 1)
            scan(vendor, child, depth - 1, found);
    }
}

} // namespace

const std::vector<LibraryChoice>& libraryChoices(Vendor vendor) {
    static const std::vector<LibraryChoice> xilinx = {
        {"unisim", "unisim: VHDL primitives (about 2 minutes)", true},
        {"unimacro", "unimacro: VHDL macros, needs unisim", true},
        {"unisim_verilog", "unisim_v: Verilog primitive models", false},
        {"xpm", "xpm: parameterised macros (CDC, memory, FIFO)", false},
    };
    static const std::vector<LibraryChoice> altera = {
        {"altera_mf", "altera_mf: megafunctions (altsyncram, altpll, ...)", true},
        {"lpm", "lpm: library of parameterised modules (lpm_add_sub, lpm_counter, ...)", true},
        {"sgate", "sgate: DSP Builder gate library, needs lpm", true},
        {"altera_primitives", "altera_primitives: lcell, dffeas, ...", true},
        {"altera_lnsim",
         "altera_lnsim: IP simulation components (the SystemVerilog part does not analyse yet)", false},
        {"cyclone10gx", "cyclone10gx: device atoms, needs altera_lnsim", false},
        {"twentynm", "twentynm: Arria 10 device atoms, needs altera_lnsim", false},
        {"tennm", "tennm: Stratix 10 / Agilex device atoms, needs altera_lnsim", false},
        {"altera_verilog",
         "Verilog models (altera_ver, sgate_ver; lpm_ver and altera_mf_ver do not analyse yet)", false},
    };
    return vendor == Vendor::Xilinx ? xilinx : altera;
}

std::vector<std::string> defaultLibraries(Vendor vendor) {
    std::vector<std::string> ids;
    for (const LibraryChoice& c : libraryChoices(vendor))
        if (c.defaultOn)
            ids.push_back(c.id);
    return ids;
}

int expectedLibraryCount(const std::vector<std::string>& ids) {
    int n = 0;
    for (const std::string& id : ids)
        n += id == "altera_verilog" ? 4 : 1;
    return n;
}

bool looksLikeInstall(Vendor vendor, const std::string& dir) {
    if (dir.empty())
        return false;
    const fs::path d(dir);
    if (vendor == Vendor::Xilinx)
        return isDir(d / "data" / "vhdl" / "src" / "unisims");
    return isDir(d / "eda" / "sim_lib") || isDir(d / "quartus" / "eda" / "sim_lib");
}

std::vector<std::string> installEnvironmentVariables(Vendor vendor) {
    return vendor == Vendor::Xilinx ? std::vector<std::string>{"XILINX_VIVADO"}
                                    : std::vector<std::string>{"QUARTUS_ROOTDIR", "ALTERA_ROOT"};
}

std::vector<std::string> defaultInstallRoots(Vendor vendor) {
    std::vector<std::string> roots;
    const std::vector<std::string> bases =
#ifdef _WIN32
        {"C:", "D:", "E:", "C:/cad", "D:/cad", "E:/cad", "C:/tools", "D:/tools"};
#else
        {"/opt", "/tools", "/usr/local", envVar("HOME")};
#endif
    const std::vector<std::string> names =
        vendor == Vendor::Xilinx
            ? std::vector<std::string>{"Xilinx"}
            : std::vector<std::string>{"intelFPGA_pro", "intelFPGA", "altera", "intelFPGA_lite"};
    for (const std::string& b : bases) {
        if (b.empty())
            continue;
        for (const std::string& n : names) {
            const std::string r = b + "/" + n;
            if (isDir(r))
                roots.push_back(r);
        }
    }
    return roots;
}

std::vector<InstallCandidate> detectInstalls(Vendor vendor, const std::vector<std::string>& envValues,
                                             const std::vector<std::string>& roots) {
    std::vector<InstallCandidate> out;
    std::vector<std::string> seen;
    auto add = [&](std::string path, bool fromEnv) {
        path = normalised(vendor, std::move(path));
        std::error_code ec;
        std::string key = lowerCase(slashes(fs::weakly_canonical(path, ec).generic_string()));
        if (ec)
            key = lowerCase(path);
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            return;
        seen.push_back(key);
        out.push_back({path, labelFor(path), fromEnv});
    };
    for (const std::string& v : envValues)
        if (!v.empty() && looksLikeInstall(vendor, v))
            add(v, true);
    std::vector<std::string> found;
    for (const std::string& r : roots) {
        if (!isDir(r))
            continue;
        if (looksLikeInstall(vendor, r)) {
            found.push_back(slashes(r));
            continue;
        }
        std::vector<std::string> here;
        scan(vendor, r, 2, here);
        std::sort(here.begin(), here.end(),
                  [](const std::string& a, const std::string& b) { return naturalLess(b, a); });
        found.insert(found.end(), here.begin(), here.end());
    }
    for (const std::string& f : found)
        add(f, false);
    return out;
}

std::vector<InstallCandidate> detectInstalls(Vendor vendor) {
    std::vector<std::string> values;
    for (const std::string& n : installEnvironmentVariables(vendor))
        values.push_back(envVar(n.c_str()));
    return detectInstalls(vendor, values, defaultInstallRoots(vendor));
}

std::vector<std::string> buildScriptArgs(const ScriptRequest& r, std::string* error) {
    auto fail = [&](const char* text) {
        if (error)
            *error = text;
        return std::vector<std::string>{};
    };
    if (r.installDir.empty())
        return fail("no installation folder given");
    if (r.outDir.empty())
        return fail("no output folder given");
    if (r.libraries.empty())
        return fail("no library selected");
    std::string only;
    for (const std::string& l : r.libraries)
        only += (only.empty() ? "" : ",") + l;
    std::vector<std::string> a = {r.vendor == Vendor::Xilinx ? "--vivado" : "--quartus",
                                  r.installDir,
                                  "--out",
                                  r.outDir,
                                  "--only",
                                  only};
    if (r.quiet)
        a.push_back("--quiet");
    if (error)
        error->clear();
    return a;
}

std::string findLibsScript(const std::string& appDir, bool windows,
                           const std::function<bool(const std::string&)>& fileExists) {
    const std::string name = windows ? "compile_libs.bat" : "compile_libs.sh";
    fs::path dir = fs::path(appDir);
    for (int i = 0; i < 8; ++i) {
        const std::string candidate = slashes((dir / "scripts" / "libs" / name).generic_string());
        if (fileExists(candidate))
            return candidate;
        if (!dir.has_parent_path() || dir.parent_path() == dir)
            break;
        dir = dir.parent_path();
    }
    return {};
}

ScriptCommand scriptCommand(bool windows, const std::string& script, const std::vector<std::string>& args) {
    ScriptCommand c;
    if (!windows) {
        c.program = "sh";
        c.args.push_back(script);
        c.args.insert(c.args.end(), args.begin(), args.end());
        return c;
    }
    std::string s = script;
    std::replace(s.begin(), s.end(), '/', '\\');
    c.program = "cmd.exe";
    c.nativeArguments = "/d /s /c \"\"" + s + "\"";
    for (const std::string& a : args) {
        std::string w = a;
        std::replace(w.begin(), w.end(), '/', '\\');
        c.nativeArguments += " \"" + w + "\"";
    }
    c.nativeArguments += "\"";
    c.args = {"/d", "/s", "/c", s};
    c.args.insert(c.args.end(), args.begin(), args.end());
    return c;
}

ScriptLine parseScriptLine(const std::string& raw) {
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    ScriptLine r;
    static const std::regex done(R"(^(\w+): (\d+) ok, (\d+) failed, (\d+) skipped in (\d+)s$)");
    static const std::regex row(R"(^(\w+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)s$)");
    static const std::regex started(R"(^(\w+)( \(.*\))?$)");
    std::smatch m;
    auto fill = [&](LineKind k) {
        r.kind = k;
        r.library = m[1];
        r.ok = std::stoi(m[2]);
        r.failed = std::stoi(m[3]);
        r.skipped = std::stoi(m[4]);
        r.seconds = std::stoi(m[5]);
    };
    if (std::regex_match(line, m, done))
        fill(LineKind::LibraryDone);
    else if (std::regex_match(line, m, row))
        fill(LineKind::SummaryRow);
    else if (line.rfind("  skipped", 0) == 0)
        r.kind = LineKind::Skipped;
    else if (line.rfind("  FAILED", 0) == 0)
        r.kind = LineKind::Failed;
    else if (std::regex_match(line, m, started) && line != "library") {
        r.kind = LineKind::Started;
        r.library = m[1];
    }
    return r;
}

Summary parseSummary(const std::string& output) {
    Summary s;
    bool inTable = false;
    size_t pos = 0;
    while (pos <= output.size()) {
        size_t nl = output.find('\n', pos);
        if (nl == std::string::npos)
            nl = output.size();
        const std::string line = output.substr(pos, nl - pos);
        pos = nl + 1;
        if (!inTable) {
            if (line.rfind("library", 0) == 0 && line.find("files ok") != std::string::npos)
                inTable = true;
            continue;
        }
        const ScriptLine l = parseScriptLine(line);
        if (l.kind != LineKind::SummaryRow)
            break;
        s.rows.push_back(l);
        (l.failed > 0 ? s.librariesFailed : s.librariesOk) += 1;
        s.filesOk += l.ok;
        s.filesFailed += l.failed;
        s.filesSkipped += l.skipped;
    }
    return s;
}

std::string summaryText(const Summary& s) {
    if (s.rows.empty())
        return "no summary printed";
    return std::to_string(s.rows.size()) + (s.rows.size() == 1 ? " library (" : " libraries (") +
           std::to_string(s.librariesOk) + " ok, " + std::to_string(s.librariesFailed) +
           " with failed files): " + std::to_string(s.filesOk) + " files ok, " +
           std::to_string(s.filesFailed) + " failed, " + std::to_string(s.filesSkipped) + " skipped";
}

} // namespace lvd::tools
