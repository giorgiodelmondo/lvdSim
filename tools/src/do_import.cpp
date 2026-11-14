// SPDX-License-Identifier: GPL-3.0-or-later
#include "do_import.hpp"

#include "library_map.hpp"
#include "text_util.hpp"
#include "translate.hpp"

#include <charconv>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace lvd::tools::proj {

using text::lower;

namespace {

bool varChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == ':';
}

class TclSplitter {
public:
    TclSplitter(std::string_view s, std::vector<std::pair<std::string, std::string>> vars)
        : s_(s)
        , vars_(std::move(vars)) {}

    std::vector<std::vector<std::string>> run() {
        std::vector<std::vector<std::string>> out;
        while (i_ < s_.size()) {
            std::vector<std::string> cmd = command();
            if (cmd.size() == 3 && cmd[0] == "set")
                setVar(cmd[1], cmd[2]);
            if (!cmd.empty())
                out.push_back(std::move(cmd));
        }
        return out;
    }

private:
    void setVar(const std::string& name, const std::string& value) {
        for (auto& [n, v] : vars_)
            if (n == name) {
                v = value;
                return;
            }
        vars_.emplace_back(name, value);
    }

    std::optional<std::string> var(const std::string& name) const {
        for (const auto& [n, v] : vars_)
            if (n == name)
                return v;
        return std::nullopt;
    }

    bool atContinuation() const { return s_[i_] == '\\' && i_ + 1 < s_.size() && s_[i_ + 1] == '\n'; }

    std::vector<std::string> command() {
        std::vector<std::string> words;
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == '\n' || c == ';') {
                ++i_;
                if (!words.empty())
                    return words;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') {
                ++i_;
                continue;
            }
            if (atContinuation()) {
                i_ += 2;
                continue;
            }
            if (c == '#' && words.empty()) {
                while (i_ < s_.size() && s_[i_] != '\n') {
                    if (atContinuation())
                        ++i_;
                    ++i_;
                }
                continue;
            }
            words.push_back(word());
        }
        return words;
    }

    void substitute(std::string& out) {
        // at '$'
        const std::size_t start = i_;
        ++i_;
        std::string name;
        if (i_ < s_.size() && s_[i_] == '{') {
            const std::size_t close = s_.find('}', i_);
            if (close == std::string_view::npos) {
                out += '$';
                return;
            }
            name = std::string(s_.substr(i_ + 1, close - i_ - 1));
            i_ = close + 1;
        } else {
            while (i_ < s_.size() && varChar(s_[i_]))
                name += s_[i_++];
        }
        if (name.empty()) {
            out += '$';
            return;
        }
        if (const auto v = var(name))
            out += *v;
        else
            out += std::string(s_.substr(start, i_ - start));
    }

    void bracket(std::string& out) {
        int depth = 0;
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            out += c;
            if (c == '[')
                ++depth;
            else if (c == ']' && --depth == 0)
                return;
        }
    }

    void escape(std::string& out) {
        // at '\\'
        if (i_ + 1 >= s_.size()) {
            ++i_;
            return;
        }
        const char n = s_[i_ + 1];
        i_ += 2;
        switch (n) {
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        default:
            out += n;
        }
    }

    std::string word() {
        std::string out;
        const char c = s_[i_];
        if (c == '{') {
            int depth = 0;
            while (i_ < s_.size()) {
                const char d = s_[i_++];
                if (d == '{' && depth++ == 0)
                    continue;
                if (d == '}' && --depth == 0)
                    break;
                out += d;
            }
            return out;
        }
        if (c == '"') {
            ++i_;
            while (i_ < s_.size() && s_[i_] != '"') {
                if (s_[i_] == '$')
                    substitute(out);
                else if (s_[i_] == '[')
                    bracket(out);
                else if (s_[i_] == '\\')
                    escape(out);
                else
                    out += s_[i_++];
            }
            ++i_;
            return out;
        }
        while (i_ < s_.size()) {
            const char d = s_[i_];
            if (d == ' ' || d == '\t' || d == '\r' || d == '\n' || d == ';' || atContinuation())
                break;
            if (d == '$')
                substitute(out);
            else if (d == '[')
                bracket(out);
            else if (d == '\\')
                escape(out);
            else
                out += s_[i_++];
        }
        return out;
    }

    std::string_view s_;
    std::size_t i_ = 0;
    std::vector<std::pair<std::string, std::string>> vars_;
};

struct LibEntry {
    std::string name;
    std::string vlibPath;
    std::string mapPath;
    std::vector<SourceFile> files;
};

class Importer {
public:
    explicit Importer(const ImportOptions& o)
        : opt_(o) {}

    ImportResult result;

    void script(std::string_view text, int depth) {
        if (depth > 8) {
            result.warnings.push_back("do scripts nested too deeply; the rest is left out");
            return;
        }
        for (const auto& cmd : TclSplitter(text, {}).run())
            command(cmd, depth);
    }

    void finish() {
        Project& p = result.project;
        // The most common standard is the project default; files with another one name it.
        std::map<std::string, int> stdUse;
        for (const LibEntry& l : libs_)
            for (const SourceFile& f : l.files)
                if (!f.isVerilog())
                    ++stdUse[f.vhdl];
        std::string def = "08";
        int best = 0;
        for (const auto& [s, n] : stdUse)
            if (n > best) {
                best = n;
                def = s;
            }
        p.settings.vhdl = def;

        for (LibEntry& l : libs_) {
            const std::string path = !l.mapPath.empty() ? l.mapPath : l.vlibPath;
            if (l.files.empty()) {
                if (!path.empty())
                    p.settings.mappings.emplace_back(l.name, path);
                else
                    result.warnings.push_back("library '" + l.name + "' has no source files; left out");
                continue;
            }
            Library lib;
            lib.name = l.name;
            lib.path = path;
            for (SourceFile f : l.files) {
                if (!f.isVerilog() && f.vhdl == def)
                    f.vhdl.clear();
                lib.files.push_back(std::move(f));
            }
            p.libraries.push_back(std::move(lib));
        }
        // lvdsim.ini mappings of libraries the script did not build.
        const LibraryMap ini = LibraryMap::parse(opt_.iniText);
        for (const auto& [name, path] : ini.entries()) {
            if (p.findLibrary(name) != nullptr)
                continue;
            bool known = false;
            for (const auto& m : p.settings.mappings)
                known = known || lower(m.first) == lower(name);
            if (!known)
                p.settings.mappings.emplace_back(name, rel(path));
        }
    }

private:
    std::string rel(const std::string& path) const {
        fs::path pp(path);
        if (!pp.is_absolute())
            pp = fs::path(opt_.scriptDir.empty() ? std::string(".") : opt_.scriptDir) / pp;
        pp = pp.lexically_normal();
        if (opt_.projectDir.empty())
            return pp.generic_string();
        const fs::path r = pp.lexically_relative(fs::path(opt_.projectDir).lexically_normal());
        if (r.empty())
            return pp.generic_string();
        return r.generic_string();
    }

    std::string scriptPath(const std::string& path) const {
        const fs::path pp(path);
        if (pp.is_absolute() || opt_.scriptDir.empty())
            return pp.generic_string();
        return (fs::path(opt_.scriptDir) / pp).lexically_normal().generic_string();
    }

    std::optional<std::string> read(const std::string& path) const {
        return opt_.readFile ? opt_.readFile(scriptPath(path)) : std::nullopt;
    }

    LibEntry& library(const std::string& name) {
        for (LibEntry& l : libs_)
            if (lower(l.name) == lower(name))
                return l;
        libs_.push_back({});
        libs_.back().name = name;
        if (const auto ini = LibraryMap::parse(opt_.iniText).pathOf(name))
            libs_.back().mapPath = rel(*ini);
        return libs_.back();
    }

    std::vector<std::string> expand(const std::vector<std::string>& args) {
        std::string err;
        auto r = expandArgFiles(
            args, [this](const std::string& f) { return read(f); }, &err);
        if (!err.empty())
            result.warnings.push_back(err);
        return r;
    }

    void command(const std::vector<std::string>& cmd, int depth) {
        const std::string name = lower(cmd[0]);
        const std::vector<std::string> args(cmd.begin() + 1, cmd.end());
        if (name == "vlib") {
            vlib(args);
        } else if (name == "vmap") {
            vmap(args);
        } else if (name == "vcom") {
            vcom(expand(args));
        } else if (name == "vlog") {
            vlog(expand(args));
        } else if (name == "vsim") {
            vsim(args);
        } else if (name == "do" || name == "source") {
            if (args.empty())
                return;
            const auto text = read(args[0]);
            if (!text) {
                result.warnings.push_back("cannot read '" + args[0] + "' (" + name + "); left out");
                return;
            }
            ++result.commands;
            script(*text, depth + 1);
        } else if (name == "set" || name == "quit" || name == "exit" || name == "onerror" ||
                   name == "onbreak" || name == "transcript" || name == "echo" || name == "puts" ||
                   name == "vdel") {
            // no effect on the project
        } else {
            result.warnings.push_back("command '" + cmd[0] + "' is not imported");
        }
    }

    // Positional words of a command, skipping options (and the values of those in `withValue`).
    static std::vector<std::string> positional(const std::vector<std::string>& args,
                                               const std::set<std::string>& withValue) {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a.size() > 1 && (a[0] == '-' || a[0] == '+')) {
                if (withValue.count(a) != 0)
                    ++i;
                continue;
            }
            out.push_back(a);
        }
        return out;
    }

    static std::string optionValue(const std::vector<std::string>& args, const std::string& opt,
                                   std::string fallback) {
        for (std::size_t i = 0; i + 1 < args.size(); ++i)
            if (args[i] == opt)
                fallback = args[i + 1];
        return fallback;
    }

    void vlib(const std::vector<std::string>& args) {
        for (const std::string& dir : positional(args, {"-type"})) {
            ++result.commands;
            const std::string name = fs::path(dir).filename().string();
            LibEntry& l = library(name);
            l.vlibPath = rel(dir);
        }
    }

    void vmap(const std::vector<std::string>& args) {
        const std::vector<std::string> pos = positional(args, {"-del", "-inifile"});
        if (pos.size() != 2)
            return;
        ++result.commands;
        library(pos[0]).mapPath = rel(pos[1]);
    }

    void vcom(const std::vector<std::string>& args) {
        static const std::set<std::string> withValue = {"-work", "-L", "-suppress", "-nowarn", "-noaccel"};
        std::string vhdlStd = "08";
        bool relaxed = false;
        for (const std::string& a : args) {
            if (const std::string s = normaliseVhdlStd(a); !s.empty() && a[0] == '-')
                vhdlStd = s;
            if (a == "-explicit" || a == "-relaxed")
                relaxed = true;
        }
        LibEntry& lib = library(optionValue(args, "-work", "work"));
        ++result.commands;
        for (const std::string& f : positional(args, withValue)) {
            SourceFile sf;
            sf.path = rel(f);
            sf.vhdl = vhdlStd;
            sf.relaxed = relaxed;
            if (sf.isVerilog())
                sf.language = Language::Vhdl;
            lib.files.push_back(std::move(sf));
        }
    }

    void vlog(const std::vector<std::string>& args) {
        static const std::set<std::string> withValue = {"-work",      "-L", "-suppress", "-nowarn",
                                                        "-timescale", "-y", "-v",        "-libmap"};
        bool sv = false;
        std::vector<std::string> defines, incdirs;
        for (const std::string& a : args) {
            if (a == "-sv")
                sv = true;
            for (const auto& [prefix, list] :
                 {std::pair<const char*, std::vector<std::string>*>{"+define+", &defines},
                  {"+incdir+", &incdirs}}) {
                if (!text::startsWith(a, prefix))
                    continue;
                std::string rest = a.substr(std::char_traits<char>::length(prefix));
                std::size_t pos = 0;
                while (pos <= rest.size()) {
                    const std::size_t plus = rest.find('+', pos);
                    const std::string part =
                        rest.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos);
                    if (!part.empty())
                        list->push_back(list == &incdirs ? rel(part) : part);
                    if (plus == std::string::npos)
                        break;
                    pos = plus + 1;
                }
            }
        }
        LibEntry& lib = library(optionValue(args, "-work", "work"));
        ++result.commands;
        for (const std::string& f : positional(args, withValue)) {
            SourceFile sf;
            sf.path = rel(f);
            if (sv && languageOf(f) != Language::SystemVerilog)
                sf.language = Language::SystemVerilog;
            else if (languageOf(f) == Language::Vhdl)
                sf.language = Language::Verilog;
            sf.defines = defines;
            sf.incdirs = incdirs;
            lib.files.push_back(std::move(sf));
        }
    }

    void vsim(const std::vector<std::string>& args) {
        static const std::set<std::string> withValue = {"-do",       "-lib",      "-L",       "-wlf",
                                                        "-l",        "-t",        "-sv_seed", "-title",
                                                        "-onfinish", "-suppress", "-g",       "-G"};
        Test t;
        std::vector<std::string> tops;
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& a = args[i];
            if ((a == "-g" || a == "-G") && i + 1 < args.size()) {
                t.generics.push_back(args[++i]);
            } else if (a.size() > 2 && (text::startsWith(a, "-g") || text::startsWith(a, "-G")) &&
                       a.find('=') != std::string::npos) {
                t.generics.push_back(a.substr(2));
            } else if (a == "-do" && i + 1 < args.size()) {
                const std::string d = args[++i];
                if (read(d))
                    t.doScript = rel(d);
                else
                    t.commands = d;
            } else if (a == "-sv_seed" && i + 1 < args.size()) {
                std::uint64_t n = 0;
                const std::string& v = args[++i];
                if (std::from_chars(v.data(), v.data() + v.size(), n).ec == std::errc())
                    t.seeds.push_back(n);
            } else if (a.size() > 1 && a[0] == '+') {
                t.plusargs.push_back(a);
            } else if (a.size() > 1 && a[0] == '-') {
                if (withValue.count(a) != 0)
                    ++i;
            } else {
                tops.push_back(a);
            }
        }
        if (tops.empty()) {
            result.warnings.push_back("vsim without a design unit is not imported");
            return;
        }
        ++result.commands;
        const std::string lib = optionValue(args, "-lib", "");
        for (const std::string& top : tops)
            t.top += (t.top.empty() ? "" : " ") +
                     ((lib.empty() || top.find('.') != std::string::npos) ? top : lib + "." + top);
        std::string base = parseDesignUnit(tops.front()).unit;
        for (char& c : base)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.')
                c = '_';
        t.name = base;
        for (int k = 2; result.project.findTest(t.name) != nullptr; ++k)
            t.name = base + "_" + std::to_string(k);
        result.project.tests.push_back(std::move(t));
    }

    const ImportOptions& opt_;
    std::vector<LibEntry> libs_;
};

} // namespace

std::vector<std::vector<std::string>>
splitTclCommands(std::string_view script, const std::vector<std::pair<std::string, std::string>>& vars) {
    return TclSplitter(script, vars).run();
}

ImportResult importDoScript(std::string_view script, const ImportOptions& options) {
    Importer imp(options);
    imp.script(script, 0);
    imp.finish();
    return std::move(imp.result);
}

} // namespace lvd::tools::proj
