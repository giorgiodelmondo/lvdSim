// SPDX-License-Identifier: GPL-3.0-or-later
#include "translate.hpp"

#include <algorithm>
#include <cctype>

namespace lvd::tools {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= s.size()) {
        const std::size_t e = s.find(sep, pos);
        const std::string part = s.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (!part.empty())
            out.push_back(part);
        if (e == std::string::npos)
            break;
        pos = e + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Option parsing

/// A known option. handled=false options are accepted and have no effect on nvc. A name ending in '*'
/// matches any option with that prefix (for example "-voptargs=*").
struct OptSpec {
    const char* name;
    int nargs;
    bool handled;
};

struct Option {
    std::string name;
    std::vector<std::string> values;
};

struct Parsed {
    std::vector<Option> options;
    std::vector<std::string> positional;

    const Option* find(const std::string& name) const {
        for (const Option& o : options)
            if (o.name == name)
                return &o;
        return nullptr;
    }
    bool has(const std::string& name) const { return find(name) != nullptr; }
    std::string value(const std::string& name, const std::string& fallback = {}) const {
        const Option* o = find(name);
        return (o != nullptr && !o->values.empty()) ? o->values.front() : fallback;
    }
    std::vector<std::string> allValues(const std::string& name) const {
        std::vector<std::string> v;
        for (const Option& o : options)
            if (o.name == name)
                v.insert(v.end(), o.values.begin(), o.values.end());
        return v;
    }
};

bool looksLikeOption(const std::string& a) {
    return a.size() > 1 && a[0] == '-' && !std::isdigit(static_cast<unsigned char>(a[1]));
}

// -93, -2008 and friends are options even though they start with a digit.
bool isStdOption(const std::string& a) {
    return a == "-87" || a == "-93" || a == "-2002" || a == "-2008" || a == "-2019";
}

const OptSpec* findSpec(const std::vector<OptSpec>& specs, const std::string& a) {
    for (const OptSpec& s : specs) {
        const std::string n = s.name;
        if (!n.empty() && n.back() == '*') {
            if (startsWith(a, n.substr(0, n.size() - 1)))
                return &s;
        } else if (a == n) {
            return &s;
        }
    }
    return nullptr;
}

void unsupported(const std::string& what, const Context& ctx, bool strict, Plan& plan) {
    const std::string msg = "(lvdsim) " + what + " is not supported";
    if (strict || ctx.strict) {
        if (plan.error.empty())
            plan.error = msg + " (strict mode)";
    } else {
        plan.warnings.push_back(msg + " and is ignored");
    }
}

Parsed parseArgs(const std::vector<std::string>& in, const std::vector<OptSpec>& specs, const Context& ctx,
                 bool strict, Plan& plan) {
    Parsed out;
    for (std::size_t i = 0; i < in.size() && plan.error.empty(); ++i) {
        const std::string& a = in[i];

        if (a.size() > 1 && a[0] == '+') {
            if (startsWith(a, "+define+"))
                out.options.push_back({"+define", splitOn(a.substr(8), '+')});
            else if (startsWith(a, "+incdir+"))
                out.options.push_back({"+incdir", splitOn(a.substr(8), '+')});
            else if (!startsWith(a, "+acc") && !startsWith(a, "+nowarn") && a != "+notimingchecks" &&
                     a != "+nospecify")
                unsupported("option '" + a + "'", ctx, strict, plan);
            continue;
        }

        if (!looksLikeOption(a) && !isStdOption(a)) {
            out.positional.push_back(a);
            continue;
        }

        const OptSpec* spec = findSpec(specs, a);
        std::string inlineValue;
        bool hasInline = false;
        if (spec == nullptr) { // -opt=value for options that take one argument
            const std::size_t eq = a.find('=');
            if (eq != std::string::npos) {
                const OptSpec* s = findSpec(specs, a.substr(0, eq));
                if (s != nullptr && s->nargs == 1) {
                    spec = s;
                    inlineValue = a.substr(eq + 1);
                    hasInline = true;
                }
            }
        }
        if (spec == nullptr) {
            unsupported("option '" + a + "'", ctx, strict, plan);
            continue;
        }

        Option opt;
        opt.name = hasInline ? a.substr(0, a.find('=')) : a;
        if (hasInline) {
            opt.values.push_back(inlineValue);
        } else {
            for (int k = 0; k < spec->nargs; ++k) {
                if (i + 1 >= in.size()) {
                    plan.error = "option " + a + " requires an argument";
                    break;
                }
                opt.values.push_back(in[++i]);
            }
        }
        if (spec->handled)
            out.options.push_back(std::move(opt));
    }
    return out;
}

// -strict is ours: it turns unsupported options into errors.
bool takeStrict(std::vector<std::string>& args) {
    const auto it = std::remove(args.begin(), args.end(), "-strict");
    const bool found = it != args.end();
    args.erase(it, args.end());
    return found;
}

// ---------------------------------------------------------------------------------------------
// Libraries and standards

// Adds --work and --map arguments. Returns false (with plan.error set) if the work library is missing.
bool addLibraryArgs(const std::string& work, const std::vector<std::string>& others, const Context& ctx,
                    Plan& plan, std::vector<std::string>& args) {
    const auto p = resolveLibrary(work, ctx.libs, ctx.dirExists);
    if (!p) {
        plan.error = "library '" + work + "' not found; create it with: vlib " + work;
        return false;
    }
    args.push_back("--work=" + work + ":" + *p);
    for (const std::string& lib : others) {
        if (lower(lib) == lower(work))
            continue;
        if (const auto q = resolveLibrary(lib, ctx.libs, ctx.dirExists))
            args.push_back("--map=" + lib + ":" + *q);
    }
    return true;
}

std::string pickStandard(const Parsed& p, const Context& ctx, Plan& plan) {
    std::string std = ctx.defaultStd;
    for (const Option& o : p.options) {
        if (o.name == "-87") {
            plan.warnings.push_back("(lvdsim) VHDL-87 is not available, using VHDL-93");
            std = "93";
        } else if (o.name == "-93")
            std = "93";
        else if (o.name == "-2002")
            std = "02";
        else if (o.name == "-2008")
            std = "08";
        else if (o.name == "-2019")
            std = "19";
    }
    return std;
}

} // namespace

// ---------------------------------------------------------------------------------------------

std::vector<std::string>
expandArgFiles(const std::vector<std::string>& args,
               const std::function<std::optional<std::string>(const std::string&)>& readFile,
               std::string* error) {
    std::function<bool(const std::vector<std::string>&, std::vector<std::string>&, int)> expand;
    expand = [&](const std::vector<std::string>& in, std::vector<std::string>& out, int depth) {
        for (std::size_t i = 0; i < in.size(); ++i) {
            if (in[i] != "-f" || i + 1 >= in.size()) {
                out.push_back(in[i]);
                continue;
            }
            const std::string& file = in[++i];
            if (depth > 8) {
                *error = "-f files nested too deeply: " + file;
                return false;
            }
            const auto text = readFile(file);
            if (!text) {
                *error = "cannot read argument file '" + file + "'";
                return false;
            }
            std::vector<std::string> words;
            std::size_t pos = 0;
            const std::string& t = *text;
            while (pos < t.size()) {
                const char c = t[pos];
                if (std::isspace(static_cast<unsigned char>(c))) {
                    ++pos;
                } else if (c == '#' || (c == '/' && pos + 1 < t.size() && t[pos + 1] == '/')) {
                    while (pos < t.size() && t[pos] != '\n')
                        ++pos;
                } else if (c == '"') {
                    std::string w;
                    for (++pos; pos < t.size() && t[pos] != '"'; ++pos)
                        w += t[pos];
                    ++pos;
                    words.push_back(w);
                } else {
                    std::string w;
                    while (pos < t.size() && !std::isspace(static_cast<unsigned char>(t[pos])))
                        w += t[pos++];
                    words.push_back(w);
                }
            }
            if (!expand(words, out, depth + 1))
                return false;
        }
        return true;
    };

    std::vector<std::string> out;
    if (!expand(args, out, 0))
        return args;
    return out;
}

DesignUnitName parseDesignUnit(const std::string& text) {
    DesignUnitName r;
    std::string t = text;
    const std::size_t paren = t.find('(');
    if (paren != std::string::npos && t.back() == ')') {
        r.architecture = t.substr(paren + 1, t.size() - paren - 2);
        t = t.substr(0, paren);
    }
    const std::size_t dot = t.find('.');
    if (dot != std::string::npos) {
        r.library = t.substr(0, dot);
        r.unit = t.substr(dot + 1);
    } else {
        r.unit = t;
    }
    return r;
}

// ---------------------------------------------------------------------------------------------

Plan planVlib(std::vector<std::string> args, const Context& ctx) {
    Plan plan;
    const bool strict = takeStrict(args);
    const std::vector<OptSpec> specs = {
        {"-type", 1, false}, {"-unix", 0, false}, {"-dos", 0, false}, {"-short", 0, false}};
    const Parsed p = parseArgs(args, specs, ctx, strict, plan);
    if (!plan.ok())
        return plan;
    if (p.positional.empty()) {
        plan.error = "usage: vlib [-type directory] <library_name>...";
        return plan;
    }
    for (const std::string& name : p.positional) {
        if (ctx.dirExists(name)) {
            plan.message += "Library '" + name + "' already exists.\n";
        } else {
            plan.directories.push_back(name);
            // The marker NVC looks for; without it NVC warns that the directory is not a library.
            plan.keepFiles.push_back({name + "/_NVC_LIB", "lvdsim\n"});
            plan.message += "Creating library '" + name + "'.\n";
        }
    }
    return plan;
}

Plan planVmap(std::vector<std::string> args, const Context& ctx) {
    Plan plan;
    const bool strict = takeStrict(args);
    const std::vector<OptSpec> specs = {{"-c", 0, false}, {"-del", 1, false}, {"-inifile", 1, false}};
    const Parsed p = parseArgs(args, specs, ctx, strict, plan);
    if (!plan.ok())
        return plan;

    if (p.positional.empty()) {
        for (const auto& [name, path] : ctx.libs.entries())
            plan.message += name + " = " + path + "\n";
        if (plan.message.empty())
            plan.message = "No library mappings.\n";
    } else if (p.positional.size() == 1) {
        const auto path = ctx.libs.pathOf(p.positional[0]);
        if (!path)
            plan.error = "library '" + p.positional[0] + "' is not mapped";
        else
            plan.message = p.positional[0] + " = " + *path + "\n";
    } else if (p.positional.size() == 2) {
        plan.mappings.emplace_back(p.positional[0], p.positional[1]);
        plan.message = "Mapping library '" + p.positional[0] + "' to '" + p.positional[1] + "'.\n";
    } else {
        plan.error = "usage: vmap [<logical_name> [<path>]]";
    }
    return plan;
}

Plan planVcom(std::vector<std::string> args, const Context& ctx) {
    Plan plan;
    const bool strict = takeStrict(args);
    std::string fileError;
    args = expandArgFiles(args, ctx.readFile, &fileError);
    if (!fileError.empty()) {
        plan.error = fileError;
        return plan;
    }

    const std::vector<OptSpec> specs = {
        {"-work", 1, true},      {"-L", 1, true},
        {"-87", 0, true},        {"-93", 0, true},
        {"-2002", 0, true},      {"-2008", 0, true},
        {"-2019", 0, true},      {"-quiet", 0, false},
        {"-novopt", 0, false},   {"-explicit", 0, true},
        {"-nocheck", 0, false},  {"-check_synthesis", 0, false},
        {"-lint", 0, false},     {"-pedanticerrors", 0, false},
        {"-suppress", 1, false}, {"-nowarn", 1, false},
        {"-O0", 0, false},       {"-O1", 0, false},
        {"-O2", 0, false},       {"-O3", 0, false},
        {"-O4", 0, false},       {"-O5", 0, false},
        {"-nologo", 0, false},   {"-noaccel", 1, false},
    };
    const Parsed p = parseArgs(args, specs, ctx, strict, plan);
    if (!plan.ok())
        return plan;
    if (p.positional.empty()) {
        plan.error = "no VHDL source files given";
        return plan;
    }

    NvcCall call;
    call.args = ctx.nvcGlobalArgs;
    call.args.push_back("--std=" + pickStandard(p, ctx, plan));
    if (!addLibraryArgs(p.value("-work", "work"), p.allValues("-L"), ctx, plan, call.args))
        return plan;
    call.args.push_back("-a");
    // -explicit: ambiguous operators resolve in favour of the explicitly declared one, which is what nvc does with --relaxed.
    if (p.has("-explicit"))
        call.args.push_back("--relaxed");
    call.args.insert(call.args.end(), p.positional.begin(), p.positional.end());
    plan.calls.push_back(std::move(call));
    return plan;
}

Plan planVlog(std::vector<std::string> args, const Context& ctx) {
    Plan plan;
    const bool strict = takeStrict(args);
    std::string fileError;
    args = expandArgFiles(args, ctx.readFile, &fileError);
    if (!fileError.empty()) {
        plan.error = fileError;
        return plan;
    }

    const std::vector<OptSpec> specs = {
        {"-work", 1, true},       {"-L", 1, true},         {"-sv", 0, true},
        {"-quiet", 0, false},     {"-novopt", 0, false},   {"-incr", 0, false},
        {"-mfcu", 0, false},      {"-lint", 0, false},     {"-pedanticerrors", 0, false},
        {"-timescale", 1, false}, {"-suppress", 1, false}, {"-nowarn", 1, false},
        {"-vopt", 0, false},      {"-nologo", 0, false},   {"-v2k5", 0, false},
        {"-O0", 0, false},        {"-O1", 0, false},       {"-O2", 0, false},
        {"-O3", 0, false},        {"-O4", 0, false},       {"-O5", 0, false},
    };
    const Parsed p = parseArgs(args, specs, ctx, strict, plan);
    if (!plan.ok())
        return plan;
    if (p.positional.empty()) {
        plan.error = "no Verilog source files given";
        return plan;
    }

    NvcCall call;
    call.args = ctx.nvcGlobalArgs;
    if (!addLibraryArgs(p.value("-work", "work"), p.allValues("-L"), ctx, plan, call.args))
        return plan;
    call.args.push_back("-a");
    if (p.has("-sv"))
        call.args.push_back("--keywords=1800-2017");
    for (const std::string& d : p.allValues("+define")) {
        call.args.push_back("-D");
        call.args.push_back(d);
    }
    for (const std::string& d : p.allValues("+incdir")) {
        call.args.push_back("-I");
        call.args.push_back(d);
    }
    call.args.insert(call.args.end(), p.positional.begin(), p.positional.end());
    plan.calls.push_back(std::move(call));
    return plan;
}

Plan planVsim(std::vector<std::string> args, const Context& ctx) {
    Plan plan;
    const bool strict = takeStrict(args);

    const std::vector<OptSpec> specs = {
        {"-c", 0, true},
        {"-batch", 0, true},
        {"-gui", 0, true},
        {"-do", 1, true},
        {"-lib", 1, true},
        {"-L", 1, true},
        {"-g", 1, true},
        {"-G", 1, true},
        {"-wlf", 1, true},
        {"-lvdwave", 1, true},
        {"-lvdlive", 1, true},
        {"-l", 1, true},
        {"-sdfmin", 1, true},
        {"-sdftyp", 1, true},
        {"-sdfmax", 1, true},
        {"-pli", 1, true},
        {"-foreign", 1, true},
        {"-t", 1, false},
        {"-quiet", 0, false},
        {"-novopt", 0, false},
        {"-voptargs=*", 0, false},
        {"-sv_seed", 1, false},
        {"-sv_lib", 1, false},
        {"-msgmode", 1, false},
        {"-title", 1, false},
        {"-onfinish", 1, false},
        {"-coverage", 0, false},
        {"-assertdebug", 0, false},
        {"-nolog", 0, false},
        {"-wlfdeleteonquit", 0, false},
        {"-nowlf", 0, false},
        {"-displaymsgmode", 1, false},
        {"-suppress", 1, false},
    };
    const Parsed p = parseArgs(args, specs, ctx, strict, plan);
    if (!plan.ok())
        return plan;

    for (const char* n : {"-sdfmin", "-sdftyp", "-sdfmax", "-pli", "-foreign"})
        if (p.has(n))
            unsupported(std::string("option '") + n + "'", ctx, strict, plan);
    if (p.has("-l"))
        plan.warnings.push_back("(lvdsim) -l <logfile> is ignored; redirect the output instead");
    if (!plan.ok())
        return plan;

    if (p.positional.empty()) {
        plan.error = "no design unit given; usage: vsim -c [options] [<library>.]<top>";
        return plan;
    }
    const bool batch = p.has("-c") || p.has("-batch");
    const bool gui = !batch;

    // Several design units are elaborated together as top levels, e.g. `vsim work.top work.glbl`. NVC elaborates
    // all of them from the work library, so every unit must come from the same library.
    std::vector<DesignUnitName> units;
    for (const std::string& text : p.positional) {
        const DesignUnitName u = parseDesignUnit(text);
        if (u.unit.empty()) {
            plan.error = "invalid design unit '" + text + "'";
            return plan;
        }
        if (!u.architecture.empty())
            plan.warnings.push_back("(lvdsim) architecture selection '(" + u.architecture +
                                    ")' is ignored, the last analysed one is used");
        units.push_back(u);
    }
    const DesignUnitName& first = units.front();
    const std::string lib = !first.library.empty() ? first.library : p.value("-lib", "work");
    for (const DesignUnitName& u : units) {
        if (!u.library.empty() && u.library != lib) {
            plan.error = "design units '" + lib + "." + first.unit + "' and '" + u.library + "." + u.unit +
                         "' are in different libraries; all top levels must be in one library";
            return plan;
        }
    }

    NvcCall call;
    call.args = ctx.nvcGlobalArgs;
    call.args.push_back("--std=" + ctx.defaultStd);
    if (!addLibraryArgs(lib, p.allValues("-L"), ctx, plan, call.args))
        return plan;

    call.args.push_back("-e");
    for (const char* opt : {"-g", "-G"}) {
        for (const std::string& g : p.allValues(opt)) {
            call.args.push_back("-g");
            call.args.push_back(g);
        }
    }
    for (const DesignUnitName& u : units)
        call.args.push_back(u.unit);

    // Waveform output: -lvdlive (live stream), -lvdwave (FST as named), -wlf (FST, extension replaced).
    std::string wave = p.value("-lvdwave");
    bool live = false;
    if (wave.empty() && p.has("-lvdlive")) {
        wave = p.value("-lvdlive");
        live = true;
    }
    if (wave.empty() && p.has("-wlf")) {
        wave = p.value("-wlf");
        wave = endsWith(lower(wave), ".wlf") ? wave.substr(0, wave.size() - 4) + ".fst" : wave + ".fst";
    }
    if (gui) {
        if (!wave.empty())
            plan.warnings.push_back("(lvdsim) waveform file options are ignored in GUI mode; the GUI records "
                                    "its own live waveform");
        wave = ctx.guiWaveFile;
        live = true;
    }
    auto addWave = [&] {
        if (live)
            call.args.push_back("--format=live");
        if (!wave.empty())
            call.args.push_back("--wave=" + wave);
    };

    // Scripts: the command compatibility layer first, then the user's.
    std::vector<std::string> scripts;
    if (p.has("-do") || gui) {
        if (!ctx.compatTcl.empty())
            scripts.push_back(ctx.compatTcl);
        else
            plan.warnings.push_back("(lvdsim) Command compatibility script not found: quit, run -all, add "
                                    "wave and similar commands are unavailable");
    }
    if (p.has("-do")) {
        const std::string doArg = p.value("-do");
        if (ctx.readFile(doArg).has_value()) {
            scripts.push_back(doArg);
        } else {
            TempFile f;
            f.path = ctx.tempDir + "/lvdsim_vsim_do.tcl";
            f.contents = doArg + "\n";
            scripts.push_back(f.path);
            plan.files.push_back(std::move(f));
        }
    }

    if (gui) {
        call.args.push_back("-i");
        addWave();
        plan.gui = true;
        plan.guiArgs = args;
        if (!ctx.guiWaveFile.empty()) {
            plan.startupScripts = scripts;
            plan.calls.push_back(std::move(call));
        } else {
            plan.files.clear();  // the command line tool only validates; the GUI writes its own
        }
        return plan;
    }

    if (!p.has("-do")) {
        call.args.push_back("-i");
        addWave();
    } else {
        call.args.push_back("--do");
        addWave();
        call.args.insert(call.args.end(), scripts.begin(), scripts.end());
    }
    plan.calls.push_back(std::move(call));
    return plan;
}

} // namespace lvd::tools
