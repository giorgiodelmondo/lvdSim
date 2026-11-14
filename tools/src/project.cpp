// SPDX-License-Identifier: GPL-3.0-or-later
#include "project.hpp"

#include "text_util.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace lvd::tools::proj {

using text::lower;
using text::quoteWord;
using text::splitWords;
using text::trim;

Language languageOf(std::string_view path) {
    const std::size_t dot = path.rfind('.');
    const std::string ext = dot == std::string_view::npos ? std::string() : lower(path.substr(dot + 1));
    if (ext == "sv" || ext == "svh" || ext == "svp")
        return Language::SystemVerilog;
    if (ext == "v" || ext == "vh" || ext == "vlog" || ext == "vp" || ext == "verilog")
        return Language::Verilog;
    return Language::Vhdl;
}

std::string normaliseVhdlStd(std::string_view t) {
    std::string s(trim(t));
    if (!s.empty() && s[0] == '-')
        s.erase(0, 1);
    if (s == "87" || s == "1987")
        return {}; // nvc has no VHDL-87
    if (s == "93" || s == "1993")
        return "93";
    if (s == "02" || s == "2002")
        return "02";
    if (s == "08" || s == "2008")
        return "08";
    if (s == "19" || s == "2019")
        return "19";
    return {};
}

bool isValidName(std::string_view name) {
    if (name.empty())
        return false;
    for (const char c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.')
            return false;
    return true;
}

Library* Project::findLibrary(std::string_view name) {
    for (Library& l : libraries)
        if (lower(l.name) == lower(name))
            return &l;
    return nullptr;
}

const Library* Project::findLibrary(std::string_view name) const {
    return const_cast<Project*>(this)->findLibrary(name);
}

const Test* Project::findTest(std::string_view name) const {
    for (const Test& t : tests)
        if (t.name == name)
            return &t;
    return nullptr;
}

std::string Project::libraryPath(const Library& lib) const {
    if (!lib.path.empty())
        return lib.path;
    std::string root = settings.libraryRoot.empty() ? std::string(".") : settings.libraryRoot;
    if (root.back() == '/' || root.back() == '\\')
        root.pop_back();
    return root + "/" + lib.name;
}

std::string Project::vhdlOf(const SourceFile& f) const {
    return !f.vhdl.empty() ? f.vhdl : settings.vhdl;
}

std::size_t Project::fileCount() const {
    std::size_t n = 0;
    for (const Library& l : libraries)
        n += l.files.size();
    return n;
}

namespace {

std::optional<bool> parseBool(std::string_view v) {
    const std::string s = lower(trim(v));
    if (s == "on" || s == "true" || s == "yes" || s == "1")
        return true;
    if (s == "off" || s == "false" || s == "no" || s == "0")
        return false;
    return std::nullopt;
}

std::optional<long long> parseInt(std::string_view v) {
    const std::string_view s = trim(v);
    long long n = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (ec != std::errc() || p != s.data() + s.size())
        return std::nullopt;
    return n;
}

/// A quoted value is unquoted (so leading or trailing spaces can be kept); others are taken as they are.
std::string plainValue(std::string_view v) {
    const std::string_view t = trim(v);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        const std::vector<std::string> w = splitWords(t);
        if (w.size() == 1)
            return w.front();
    }
    return std::string(t);
}

std::string onOff(bool b) {
    return b ? "on" : "off";
}

// A value written so plainValue() gives it back.
std::string valueText(const std::string& v) {
    if (v.empty() || v != std::string(trim(v)) || (v.front() == '"' && v.back() == '"' && v.size() >= 2))
        return quoteWord(v);
    return v;
}

enum class Section { None, Project, Library, Test };

class Reader {
public:
    ParseResult result;

    void run(std::string_view input) {
        int lineNo = 0;
        std::size_t pos = 0;
        while (pos <= input.size()) {
            const std::size_t nl = input.find('\n', pos);
            std::string_view line =
                input.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
            pos = nl == std::string_view::npos ? input.size() + 1 : nl + 1;
            ++lineNo;
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (lineNo == 1 && text::startsWith(line, "\xEF\xBB\xBF"))
                line.remove_prefix(3);
            const std::string_view t = trim(line);
            if (t.empty() || t.front() == '#' || t.front() == ';')
                continue;
            line_ = lineNo;
            if (t.front() == '[')
                header(t);
            else
                keyValue(t);
        }
    }

private:
    void error(std::string msg) { result.errors.push_back({line_, std::move(msg)}); }
    void warning(std::string msg) { result.warnings.push_back({line_, std::move(msg)}); }

    void header(std::string_view t) {
        if (t.back() != ']') {
            error("section header must end with ']'");
            section_ = Section::None;
            return;
        }
        const std::vector<std::string> w = splitWords(t.substr(1, t.size() - 2));
        const std::string kind = w.empty() ? std::string() : lower(w[0]);
        Project& p = result.project;
        if (kind == "project" && w.size() == 1) {
            section_ = Section::Project;
        } else if ((kind == "library" || kind == "test") && w.size() == 2) {
            if (!isValidName(w[1])) {
                error("invalid " + kind + " name '" + w[1] + "' (letters, digits, '_', '-', '.')");
                section_ = Section::None;
                return;
            }
            if (kind == "library") {
                if (p.findLibrary(w[1]) != nullptr) {
                    error("library '" + w[1] + "' is defined twice");
                    section_ = Section::None;
                    return;
                }
                p.libraries.push_back({});
                p.libraries.back().name = w[1];
                section_ = Section::Library;
            } else {
                if (p.findTest(w[1]) != nullptr) {
                    error("test '" + w[1] + "' is defined twice");
                    section_ = Section::None;
                    return;
                }
                p.tests.push_back({});
                p.tests.back().name = w[1];
                section_ = Section::Test;
            }
        } else {
            error("unknown section '" + std::string(t) +
                  "' (expected [project], [library NAME] or [test NAME])");
            section_ = Section::None;
        }
    }

    void keyValue(std::string_view t) {
        const std::size_t eq = t.find('=');
        if (eq == std::string_view::npos) {
            error("expected 'key = value'");
            return;
        }
        const std::string key = lower(trim(t.substr(0, eq)));
        const std::string_view value = trim(t.substr(eq + 1));
        if (key.empty()) {
            error("missing key before '='");
            return;
        }
        switch (section_) {
        case Section::None:
            error("'" + key + "' outside of a section");
            break;
        case Section::Project:
            projectKey(key, value);
            break;
        case Section::Library:
            libraryKey(key, value);
            break;
        case Section::Test:
            testKey(key, value);
            break;
        }
    }

    bool boolValue(const std::string& key, std::string_view v, bool& out) {
        const auto b = parseBool(v);
        if (!b) {
            error("'" + key + "' must be on or off");
            return false;
        }
        out = *b;
        return true;
    }

    bool intValue(const std::string& key, std::string_view v, int& out, int min) {
        const auto n = parseInt(v);
        if (!n || *n < min || *n > 1000000) {
            error("'" + key + "' must be a whole number" + (min > 0 ? " greater than 0" : ""));
            return false;
        }
        out = static_cast<int>(*n);
        return true;
    }

    void projectKey(const std::string& key, std::string_view v) {
        Settings& s = result.project.settings;
        if (key == "name") {
            s.name = plainValue(v);
        } else if (key == "vhdl") {
            s.vhdl = normaliseVhdlStd(v);
            if (s.vhdl.empty()) {
                error("unknown VHDL standard '" + std::string(v) + "' (93, 2002, 2008, 2019)");
                s.vhdl = "08";
            }
        } else if (key == "library_root") {
            s.libraryRoot = plainValue(v);
        } else if (key == "library_folder") {
            s.libraryFolders.push_back(plainValue(v));
        } else if (key == "map") {
            const std::vector<std::string> w = splitWords(v);
            if (w.size() != 2 || !isValidName(w[0]))
                error("'map' needs a library name and a directory");
            else
                s.mappings.emplace_back(w[0], w[1]);
        } else if (key == "jobs") {
            intValue(key, v, s.jobs, 0);
        } else if (key == "timeout") {
            intValue(key, v, s.timeoutSec, 1);
        } else if (key == "wave") {
            boolValue(key, v, s.wave);
        } else if (key == "coverage") {
            boolValue(key, v, s.coverage);
        } else if (key == "coverage_kinds") {
            s.coverageKinds = plainValue(v);
        } else if (key == "regression_dir") {
            s.regressionDir = plainValue(v);
        } else {
            warning("unknown key '" + key + "' in [project] (kept)");
            s.extra.emplace_back(key, std::string(v));
        }
    }

    void libraryKey(const std::string& key, std::string_view v) {
        Library& lib = result.project.libraries.back();
        if (key == "path") {
            lib.path = plainValue(v);
        } else if (key == "file") {
            const std::vector<std::string> w = splitWords(v);
            if (w.empty()) {
                error("'file' needs a path");
                return;
            }
            SourceFile f;
            f.path = w[0];
            for (std::size_t i = 1; i < w.size(); ++i)
                fileOption(f, w[i]);
            lib.files.push_back(std::move(f));
        } else {
            warning("unknown key '" + key + "' in [library " + lib.name + "] (kept)");
            lib.extra.emplace_back(key, std::string(v));
        }
    }

    void fileOption(SourceFile& f, const std::string& opt) {
        const std::size_t eq = opt.find('=');
        const std::string name = lower(opt.substr(0, eq));
        const std::string value = eq == std::string::npos ? std::string() : opt.substr(eq + 1);
        if (name == "vhdl" && eq != std::string::npos) {
            f.vhdl = normaliseVhdlStd(value);
            if (f.vhdl.empty())
                error("unknown VHDL standard '" + value + "' for " + f.path);
        } else if (name == "relaxed" && eq == std::string::npos) {
            f.relaxed = true;
        } else if (name == "define" && !value.empty()) {
            f.defines.push_back(value);
        } else if (name == "incdir" && !value.empty()) {
            f.incdirs.push_back(value);
        } else if (name == "sv" && eq == std::string::npos) {
            f.language = Language::SystemVerilog;
        } else if (name == "verilog" && eq == std::string::npos) {
            f.language = Language::Verilog;
        } else if (name == "vhdl" && eq == std::string::npos) {
            f.language = Language::Vhdl;
        } else {
            error("unknown file option '" + opt + "' for " + f.path +
                  " (vhdl=STD, relaxed, define=X, incdir=DIR, sv, verilog, vhdl)");
        }
    }

    void testKey(const std::string& key, std::string_view v) {
        Test& t = result.project.tests.back();
        if (key == "top") {
            t.top = plainValue(v);
        } else if (key == "generic") {
            const std::string g = plainValue(v);
            if (g.find('=') == std::string::npos || g.front() == '=')
                error("'generic' must be NAME=VALUE");
            else
                t.generics.push_back(g);
        } else if (key == "plusarg") {
            std::string a = plainValue(v);
            if (!a.empty() && a.front() != '+')
                a.insert(a.begin(), '+');
            t.plusargs.push_back(a);
        } else if (key == "do") {
            t.doScript = plainValue(v);
        } else if (key == "commands") {
            t.commands = plainValue(v);
        } else if (key == "seeds" || key == "seed") {
            for (const std::string& w : splitWords(v)) {
                std::uint64_t n = 0;
                const auto [p, ec] = std::from_chars(w.data(), w.data() + w.size(), n);
                if (ec != std::errc() || p != w.data() + w.size()) {
                    error("seed '" + w + "' is not a whole number");
                    continue;
                }
                t.seeds.push_back(n);
            }
        } else if (key == "seed_generic") {
            t.seedGeneric = plainValue(v);
        } else if (key == "timeout") {
            intValue(key, v, t.timeoutSec, 1);
        } else if (key == "expect") {
            t.expect = plainValue(v);
        } else if (key == "stop_time") {
            t.stopTime = plainValue(v);
        } else if (key == "wave" || key == "coverage") {
            bool b = false;
            if (boolValue(key, v, b))
                (key == "wave" ? t.wave : t.coverage) = b;
        } else {
            warning("unknown key '" + key + "' in [test " + t.name + "] (kept)");
            t.extra.emplace_back(key, std::string(v));
        }
    }

    Section section_ = Section::None;
    int line_ = 0;
};

void line(std::string& out, const std::string& key, const std::string& value) {
    out += key;
    out += " = ";
    out += value;
    out += '\n';
}

} // namespace

ParseResult parseProject(std::string_view input) {
    Reader r;
    r.run(input);
    for (const Test& t : r.result.project.tests)
        if (t.top.empty())
            r.result.errors.push_back({0, "test '" + t.name + "' has no 'top'"});
    return std::move(r.result);
}

std::string writeProject(const Project& p) {
    const Settings& s = p.settings;
    const Settings d; // defaults are not written
    std::string out = "# lvdsim project (format: docs/projects.md)\n[project]\n";
    if (!s.name.empty())
        line(out, "name", valueText(s.name));
    line(out, "vhdl", s.vhdl.empty() ? std::string("08") : s.vhdl);
    if (s.libraryRoot != d.libraryRoot)
        line(out, "library_root", valueText(s.libraryRoot));
    for (const std::string& f : s.libraryFolders)
        line(out, "library_folder", valueText(f));
    for (const auto& [name, path] : s.mappings)
        line(out, "map", name + " " + quoteWord(path));
    if (s.jobs != d.jobs)
        line(out, "jobs", std::to_string(s.jobs));
    if (s.timeoutSec != d.timeoutSec)
        line(out, "timeout", std::to_string(s.timeoutSec));
    if (s.wave != d.wave)
        line(out, "wave", onOff(s.wave));
    if (s.coverage != d.coverage)
        line(out, "coverage", onOff(s.coverage));
    if (!s.coverageKinds.empty())
        line(out, "coverage_kinds", valueText(s.coverageKinds));
    if (s.regressionDir != d.regressionDir)
        line(out, "regression_dir", valueText(s.regressionDir));
    for (const auto& [k, v] : s.extra)
        line(out, k, v);

    for (const Library& lib : p.libraries) {
        out += "\n[library " + lib.name + "]\n";
        if (!lib.path.empty())
            line(out, "path", valueText(lib.path));
        for (const SourceFile& f : lib.files) {
            std::string v = quoteWord(f.path);
            if (!f.vhdl.empty())
                v += " vhdl=" + f.vhdl;
            if (f.relaxed)
                v += " relaxed";
            if (f.language && *f.language != languageOf(f.path))
                v += *f.language == Language::SystemVerilog ? " sv"
                     : *f.language == Language::Verilog     ? " verilog"
                                                            : " vhdl";
            for (const std::string& def : f.defines)
                v += " " + quoteWord("define=" + def);
            for (const std::string& inc : f.incdirs)
                v += " " + quoteWord("incdir=" + inc);
            line(out, "file", v);
        }
        for (const auto& [k, v] : lib.extra)
            line(out, k, v);
    }

    for (const Test& t : p.tests) {
        out += "\n[test " + t.name + "]\n";
        line(out, "top", valueText(t.top));
        for (const std::string& g : t.generics)
            line(out, "generic", valueText(g));
        for (const std::string& a : t.plusargs)
            line(out, "plusarg", valueText(a));
        if (!t.doScript.empty())
            line(out, "do", valueText(t.doScript));
        if (!t.commands.empty())
            line(out, "commands", valueText(t.commands));
        if (!t.seeds.empty()) {
            std::string v;
            for (const std::uint64_t n : t.seeds)
                v += (v.empty() ? "" : " ") + std::to_string(n);
            line(out, "seeds", v);
        }
        if (!t.seedGeneric.empty())
            line(out, "seed_generic", valueText(t.seedGeneric));
        if (t.timeoutSec > 0)
            line(out, "timeout", std::to_string(t.timeoutSec));
        if (!t.expect.empty())
            line(out, "expect", valueText(t.expect));
        if (!t.stopTime.empty())
            line(out, "stop_time", valueText(t.stopTime));
        if (t.wave)
            line(out, "wave", onOff(*t.wave));
        if (t.coverage)
            line(out, "coverage", onOff(*t.coverage));
        for (const auto& [k, v] : t.extra)
            line(out, k, v);
    }
    return out;
}

LoadedProject loadProject(const std::string& path) {
    LoadedProject out;
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(path, ec).lexically_normal();
    out.path = abs.generic_string();
    out.dir = abs.parent_path().generic_string();
    std::ifstream in(abs, std::ios::binary);
    if (!in) {
        out.error = "cannot read project file '" + path + "'";
        return out;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    ParseResult r = parseProject(ss.str());
    out.project = std::move(r.project);
    out.warnings = std::move(r.warnings);
    for (const ParseMessage& m : r.errors)
        out.error += (out.error.empty() ? "" : "\n") + path +
                     (m.line > 0 ? ":" + std::to_string(m.line) : "") + ": " + m.text;
    return out;
}

bool saveProject(const Project& p, const std::string& path, std::string* error) {
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        *error = "cannot write '" + path + "'";
        return false;
    }
    out << writeProject(p);
    out.close();
    if (!out) {
        *error = "cannot write '" + path + "'";
        return false;
    }
    return true;
}

} // namespace lvd::tools::proj
