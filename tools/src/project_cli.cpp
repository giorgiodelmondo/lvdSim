// SPDX-License-Identifier: GPL-3.0-or-later
#include "project_cli.hpp"

#include "compile_runner.hpp"
#include "coverage.hpp"
#include "do_import.hpp"
#include "project.hpp"
#include "regression.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

namespace lvd::tools {

namespace {

// ---------------------------------------------------------------------------------------------
// Option tables: one row per option.

template <typename Opts>
struct OptionRow {
    const char* name;
    bool takesValue;
    std::function<bool(Opts&, const std::string&)> apply; ///< false: bad value
    const char* help;
};

template <typename Opts>
bool parseOptions(const std::vector<std::string>& args, const std::vector<OptionRow<Opts>>& table, Opts& opts,
                  std::vector<std::string>& positional, const char* tool) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a.size() < 2 || a[0] != '-') {
            positional.push_back(a);
            continue;
        }
        const auto row =
            std::find_if(table.begin(), table.end(), [&](const OptionRow<Opts>& r) { return a == r.name; });
        if (row == table.end()) {
            std::cerr << "** Error: (" << tool << ") unknown option '" << a << "'\n";
            return false;
        }
        std::string value;
        if (row->takesValue) {
            if (i + 1 >= args.size()) {
                std::cerr << "** Error: (" << tool << ") option " << a << " needs a value\n";
                return false;
            }
            value = args[++i];
        }
        if (!row->apply(opts, value)) {
            std::cerr << "** Error: (" << tool << ") bad value '" << value << "' for " << a << "\n";
            return false;
        }
    }
    return true;
}

bool toInt(const std::string& s, int& out, int min) {
    try {
        std::size_t used = 0;
        const int n = std::stoi(s, &used);
        if (used != s.size() || n < min)
            return false;
        out = n;
        return true;
    } catch (...) {
        return false;
    }
}

bool loadOrReport(const std::string& path, proj::LoadedProject& out, const char* tool) {
    out = proj::loadProject(path);
    for (const proj::ParseMessage& w : out.warnings)
        std::cerr << "** Warning: (" << tool << ") " << path << ":" << w.line << ": " << w.text << "\n";
    if (!out.ok()) {
        std::cerr << "** Error: (" << tool << ") " << out.error << "\n";
        return false;
    }
    return true;
}

build::Setup setupFor(const proj::LoadedProject& lp, const Environment& env) {
    build::Setup s;
    s.projectDir = lp.dir;
    s.ctx = env.ctx;
    s.nvc = env.nvc;
    s.process = env.processOptions();
    return s;
}

/// Runs the compile step; prints what it does. Returns the number of files that failed (-1: could not start).
int compileProject(const proj::LoadedProject& lp, const Environment& env, build::Mode mode,
                   const std::set<int>& selected, bool dryRun, bool quietWhenNothing) {
    build::Runner runner(setupFor(lp, env));
    std::string error;
    if (!runner.prepare(lp.project, mode, selected, &error)) {
        std::cerr << "** Error: (compile) " << error << "\n";
        return -1;
    }
    for (const std::string& w : runner.graph().warnings)
        std::cerr << "** Warning: (compile) " << w << "\n";
    const build::Plan& plan = runner.plan();
    if (plan.steps.empty()) {
        if (!quietWhenNothing)
            std::cout << "All " << lp.project.fileCount() << " files are up to date.\n";
        return 0;
    }
    auto describe = [&](std::size_t k) {
        const build::Step& s = plan.steps[k];
        std::string why = build::reasonText(s.reason);
        if (s.reason == build::Reason::Dependency && s.cause >= 0) {
            const build::FileRef c = runner.graph().files[static_cast<std::size_t>(s.cause)];
            why += " " + lp.project.libraries[static_cast<std::size_t>(c.library)]
                             .files[static_cast<std::size_t>(c.file)]
                             .path;
        }
        return runner.libraryName(k) + ": " + runner.fileName(k) + " (" + why + ")";
    };
    if (dryRun) {
        for (std::size_t k = 0; k < plan.steps.size(); ++k)
            std::cout << "-- Would analyse " << describe(k) << "\n";
        return 0;
    }
    runner.onStarted = [&](std::size_t k) {
        std::cout << "-- Analysing " << describe(k) << "\n" << std::flush;
    };
    runner.onFinished = [&](std::size_t k) {
        if (runner.state(k) == build::Runner::StepState::Skipped)
            std::cout << "-- Skipped " << describe(k) << ": a file it depends on failed\n";
        else
            std::cout << runner.output(k);
        std::cout << std::flush;
    };
    runner.runToEnd();
    const int failed = runner.failedCount();
    int skipped = 0;
    for (std::size_t k = 0; k < plan.steps.size(); ++k)
        skipped += runner.state(k) == build::Runner::StepState::Skipped ? 1 : 0;
    std::cout << "Analysed " << runner.okCount() << " of " << plan.steps.size() << " files";
    if (failed > 0)
        std::cout << ", " << failed << " failed";
    if (skipped > 0)
        std::cout << ", " << skipped << " skipped";
    std::cout << ".\n";
    return failed + skipped;
}

// ---------------------------------------------------------------------------------------------

struct CompileOpts {
    bool all = false;
    bool dryRun = false;
};

const std::vector<OptionRow<CompileOpts>>& compileTable() {
    static const std::vector<OptionRow<CompileOpts>> t = {
        {"--all", false, [](CompileOpts& o, const std::string&) { return o.all = true; },
         "analyse every file"},
        {"--dry-run", false, [](CompileOpts& o, const std::string&) { return o.dryRun = true; },
         "only list what would be analysed"},
    };
    return t;
}

struct RegressOpts {
    int jobs = 0;
    std::string junit;
    std::vector<std::string> tests;
    bool rerunFailed = false;
    bool noCompile = false;
    int timeout = 0;
    std::optional<bool> wave, coverage;
    std::string out;
};

const std::vector<OptionRow<RegressOpts>>& regressTable() {
    static const std::vector<OptionRow<RegressOpts>> t = {
        {"-j", true, [](RegressOpts& o, const std::string& v) { return toInt(v, o.jobs, 1); },
         "parallel runs"},
        {"--jobs", true, [](RegressOpts& o, const std::string& v) { return toInt(v, o.jobs, 1); },
         "parallel runs"},
        {"--junit", true, [](RegressOpts& o, const std::string& v) { return !(o.junit = v).empty(); },
         "JUnit XML"},
        {"--test", true,
         [](RegressOpts& o, const std::string& v) {
             o.tests.push_back(v);
             return true;
         },
         "run only this test (repeatable)"},
        {"--rerun-failed", false, [](RegressOpts& o, const std::string&) { return o.rerunFailed = true; },
         "run the tests that did not pass last time"},
        {"--no-compile", false, [](RegressOpts& o, const std::string&) { return o.noCompile = true; },
         "do not analyse out-of-date files first"},
        {"--timeout", true, [](RegressOpts& o, const std::string& v) { return toInt(v, o.timeout, 1); },
         "seconds per run"},
        {"--wave", false, [](RegressOpts& o, const std::string&) { return *(o.wave = true); },
         "record waveforms"},
        {"--coverage", false, [](RegressOpts& o, const std::string&) { return *(o.coverage = true); },
         "collect and merge coverage"},
        {"--out", true, [](RegressOpts& o, const std::string& v) { return !(o.out = v).empty(); },
         "output directory"},
    };
    return t;
}

std::string nowIso() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    return buf;
}

void mergeCoverage(const std::vector<std::string>& dbs, const std::string& outDir, const Environment& env) {
    if (dbs.empty())
        return;
    const std::string merged = outDir + "/merged.ncdb";
    std::vector<std::string> argv{env.nvc};
    argv.insert(argv.end(), env.ctx.nvcGlobalArgs.begin(), env.ctx.nvcGlobalArgs.end());
    const std::vector<std::string> m = cov::mergeArgs(dbs, merged);
    argv.insert(argv.end(), m.begin(), m.end());
    ProcessOptions po = env.processOptions();
    po.capture = true;
    const ProcessResult r = runProcess(argv, po);
    if (!r.started || r.exitCode != 0) {
        std::cerr << "** Warning: (regress) merging coverage failed\n" << r.output;
        return;
    }
    // Summary through the XML export.
    const std::string xml = outDir + "/merged.xml";
    argv = {env.nvc};
    argv.insert(argv.end(), env.ctx.nvcGlobalArgs.begin(), env.ctx.nvcGlobalArgs.end());
    const std::vector<std::string> e = cov::exportArgs({merged}, xml);
    argv.insert(argv.end(), e.begin(), e.end());
    const ProcessResult x = runProcess(argv, po);
    std::cout << "Coverage of " << dbs.size() << " runs merged into " << merged;
    if (x.started && x.exitCode == 0) {
        const cov::ParseResult pr = cov::parseExportXml(readTextFile(xml).value_or(""));
        if (pr.ok()) {
            const cov::Counts c = pr.db.totals();
            char buf[64];
            std::snprintf(buf, sizeof buf, ": %.1f %% (%lld of %lld items)",
                          c.overall() < 0 ? 0.0 : c.overall(), static_cast<long long>(c.allHit()),
                          static_cast<long long>(c.allTotal()));
            std::cout << buf;
        }
    }
    std::cout << "\n";
}

} // namespace

int runCompileCommand(const std::vector<std::string>& args, const Environment& env) {
    CompileOpts opts;
    std::vector<std::string> pos;
    if (!parseOptions(args, compileTable(), opts, pos, "compile"))
        return 2;
    if (pos.empty()) {
        std::cerr << "usage: lvdsim-cli compile <project.lvdproj> [--all] [--dry-run] [FILE...]\n";
        return 2;
    }
    proj::LoadedProject lp;
    if (!loadOrReport(pos[0], lp, "compile"))
        return 2;
    std::set<int> selected;
    const std::vector<build::FileRef> files = build::projectFiles(lp.project);
    for (std::size_t i = 1; i < pos.size(); ++i) {
        bool found = false;
        const std::string want = fs::absolute(pos[i]).lexically_normal().generic_string();
        for (std::size_t k = 0; k < files.size(); ++k) {
            const auto& f = lp.project.libraries[static_cast<std::size_t>(files[k].library)]
                                .files[static_cast<std::size_t>(files[k].file)];
            if (f.path == pos[i] || build::absolutePath(lp.dir, f.path) == want) {
                selected.insert(static_cast<int>(k));
                found = true;
            }
        }
        if (!found) {
            std::cerr << "** Error: (compile) '" << pos[i] << "' is not a file of the project\n";
            return 2;
        }
    }
    const build::Mode mode = !selected.empty() ? build::Mode::Selected
                             : opts.all        ? build::Mode::All
                                               : build::Mode::OutOfDate;
    const int r = compileProject(lp, env, mode, selected, opts.dryRun, false);
    return r < 0 ? 2 : r > 0 ? 1 : 0;
}

int runRegressCommand(const std::vector<std::string>& args, const Environment& env) {
    RegressOpts opts;
    std::vector<std::string> pos;
    if (!parseOptions(args, regressTable(), opts, pos, "regress"))
        return 2;
    if (pos.size() != 1) {
        std::cerr << "usage: lvdsim-cli regress <project.lvdproj> [-j N] [--junit FILE] [--test NAME]... "
                     "[--rerun-failed] [--no-compile] [--timeout S] [--wave] [--coverage] [--out DIR]\n";
        return 2;
    }
    proj::LoadedProject lp;
    if (!loadOrReport(pos[0], lp, "regress"))
        return 2;
    if (lp.project.tests.empty()) {
        std::cerr << "** Error: (regress) the project has no tests\n";
        return 2;
    }

    if (!opts.noCompile) {
        const int failed = compileProject(lp, env, build::Mode::OutOfDate, {}, false, true);
        if (failed != 0) {
            std::cerr << "** Error: (regress) the design did not analyse; no test was run\n";
            return 2;
        }
    }

    regress::PlanOptions po;
    po.projectDir = lp.dir;
    po.outDir = opts.out.empty() ? std::string() : fs::absolute(opts.out).lexically_normal().generic_string();
    po.nvcGlobalArgs = env.ctx.nvcGlobalArgs;
    po.compatTcl = env.ctx.compatTcl;
    const Context pctx = build::projectContext(lp.project, lp.dir, env.ctx);
    po.libraries = build::projectLibraries(lp.project, lp.dir, pctx);
    po.only = opts.tests;
    po.wave = opts.wave;
    po.coverage = opts.coverage;
    po.timeoutSec = opts.timeout;
    const std::string outDir =
        !po.outDir.empty() ? po.outDir : build::absolutePath(lp.dir, lp.project.settings.regressionDir);
    if (opts.rerunFailed) {
        const std::vector<std::string> ids =
            regress::failedIds(readTextFile(outDir + "/results.txt").value_or(""));
        if (ids.empty()) {
            std::cout << "No failed test in the last regression (" << outDir << "/results.txt).\n";
            return 0;
        }
        for (const std::string& id : ids)
            if (std::find(po.only.begin(), po.only.end(), id) == po.only.end())
                po.only.push_back(id);
    }
    const regress::PlanResult plan = regress::planJobs(lp.project, po);
    for (const std::string& e : plan.errors)
        std::cerr << "** Error: (regress) " << e << "\n";
    if (!plan.ok())
        return 2;

    regress::RunnerConfig rc;
    rc.nvc = env.nvc;
    rc.process = env.processOptions();
    rc.process.workingDir = lp.dir;
    rc.jobs = opts.jobs > 0                  ? opts.jobs
              : lp.project.settings.jobs > 0 ? lp.project.settings.jobs
                                             : std::max(1u, std::thread::hardware_concurrency());
    regress::Runner runner(plan.jobs, rc);
    const std::size_t total = runner.jobs().size();
    std::size_t done = 0;
    std::cout << "Running " << total << " test run" << (total == 1 ? "" : "s") << " with " << rc.jobs
              << " job" << (rc.jobs == 1 ? "" : "s") << ".\n";
    runner.onFinished = [&](int i) {
        const regress::Outcome& o = runner.outcomes()[static_cast<std::size_t>(i)];
        const regress::Job& j = runner.jobs()[static_cast<std::size_t>(i)];
        char head[64];
        std::snprintf(head, sizeof head, "[%zu/%zu] %-7s ", ++done, total,
                      o.status == regress::Status::Passed     ? "PASS"
                      : o.status == regress::Status::TimedOut ? "TIMEOUT"
                                                              : "FAIL");
        std::cout << head << j.id << " (" << static_cast<int>(o.seconds * 1000) / 1000.0 << " s)";
        if (o.status != regress::Status::Passed)
            std::cout << ": " << o.message << (o.firstProblem.empty() ? "" : " - " + o.firstProblem)
                      << "\n    log: " << j.logFile;
        std::cout << "\n" << std::flush;
    };
    runner.runToEnd();

    int passed = 0;
    for (const regress::Outcome& o : runner.outcomes())
        passed += o.status == regress::Status::Passed ? 1 : 0;
    std::error_code ec;
    fs::create_directories(outDir, ec);
    // results.txt keeps the earlier results of runs that were not repeated, so --rerun-failed works twice.
    {
        std::vector<regress::Job> jobs = runner.jobs();
        std::vector<regress::Outcome> outcomes = runner.outcomes();
        if (!po.only.empty()) {
            const std::string old = readTextFile(outDir + "/results.txt").value_or("");
            std::string merged = regress::writeResults(jobs, outcomes);
            std::size_t pos2 = 0;
            while (pos2 < old.size()) {
                const std::size_t nl = old.find('\n', pos2);
                const std::string line =
                    old.substr(pos2, nl == std::string::npos ? std::string::npos : nl - pos2);
                pos2 = nl == std::string::npos ? old.size() : nl + 1;
                const std::string id = line.substr(0, line.find('\t'));
                const bool rerun =
                    std::any_of(jobs.begin(), jobs.end(), [&](const regress::Job& j) { return j.id == id; });
                if (!rerun && !line.empty())
                    merged += line + "\n";
            }
            std::ofstream(outDir + "/results.txt", std::ios::binary | std::ios::trunc) << merged;
        } else {
            std::ofstream(outDir + "/results.txt", std::ios::binary | std::ios::trunc)
                << regress::writeResults(jobs, outcomes);
        }
    }
    if (!opts.junit.empty()) {
        std::ofstream out(opts.junit, std::ios::binary | std::ios::trunc);
        out << regress::junitXml(lp.project.settings.name.empty() ? fs::path(lp.path).stem().string()
                                                                  : lp.project.settings.name,
                                 runner.jobs(), runner.outcomes(), nowIso());
        if (!out) {
            std::cerr << "** Error: (regress) cannot write '" << opts.junit << "'\n";
            return 2;
        }
    }
    mergeCoverage(regress::coverageDatabases(runner.jobs()), outDir, env);
    std::cout << passed << " of " << total << " passed";
    if (passed != static_cast<int>(total))
        std::cout << ", " << (total - static_cast<std::size_t>(passed)) << " did not";
    std::cout << ". Results: " << outDir << "/results.txt\n";
    return passed == static_cast<int>(total) ? 0 : 1;
}

namespace {

struct ImportOpts {
    std::string out;
    std::string ini;
    std::string name;
};

const std::vector<OptionRow<ImportOpts>>& importTable() {
    static const std::vector<OptionRow<ImportOpts>> t = {
        {"-o", true, [](ImportOpts& o, const std::string& v) { return !(o.out = v).empty(); },
         "project file"},
        {"--ini", true, [](ImportOpts& o, const std::string& v) { return !(o.ini = v).empty(); },
         "lvdsim.ini"},
        {"--name", true, [](ImportOpts& o, const std::string& v) { return !(o.name = v).empty(); },
         "project name"},
    };
    return t;
}

int importCommand(const std::vector<std::string>& args, const Environment& env) {
    ImportOpts opts;
    std::vector<std::string> pos;
    if (!parseOptions(args, importTable(), opts, pos, "project import"))
        return 2;
    if (pos.size() != 1) {
        std::cerr << "usage: lvdsim-cli project import <script.do> [-o project.lvdproj] [--ini lvdsim.ini] "
                     "[--name N]\n";
        return 2;
    }
    const auto script = readTextFile(pos[0]);
    if (!script) {
        std::cerr << "** Error: (project import) cannot read '" << pos[0] << "'\n";
        return 2;
    }
    const fs::path scriptPath = fs::absolute(pos[0]).lexically_normal();
    const std::string out =
        opts.out.empty() ? (scriptPath.parent_path() / (scriptPath.stem().string() + ".lvdproj")).string()
                         : opts.out;
    const fs::path outPath = fs::absolute(out).lexically_normal();
    if (fs::exists(outPath)) {
        std::cerr << "** Error: (project import) '" << out << "' exists; choose another name with -o\n";
        return 2;
    }
    proj::ImportOptions io;
    io.scriptDir = scriptPath.parent_path().generic_string();
    io.projectDir = outPath.parent_path().generic_string();
    const std::string iniPath =
        !opts.ini.empty() ? opts.ini : (scriptPath.parent_path() / "lvdsim.ini").string();
    io.iniText = readTextFile(iniPath).value_or(readTextFile(env.iniPath).value_or(""));
    io.readFile = readTextFile;
    proj::ImportResult r = proj::importDoScript(*script, io);
    for (const std::string& w : r.warnings)
        std::cerr << "** Warning: (project import) " << w << "\n";
    r.project.settings.name = !opts.name.empty() ? opts.name : scriptPath.stem().string();
    std::string error;
    if (!proj::saveProject(r.project, outPath.string(), &error)) {
        std::cerr << "** Error: (project import) " << error << "\n";
        return 2;
    }
    std::cout << "Wrote " << outPath.generic_string() << ": " << r.project.libraries.size() << " libraries, "
              << r.project.fileCount() << " files, " << r.project.tests.size() << " tests.\n";
    return 0;
}

int statusCommand(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::cerr << "usage: lvdsim-cli project status <project.lvdproj>\n";
        return 2;
    }
    proj::LoadedProject lp;
    if (!loadOrReport(args[0], lp, "project status"))
        return 2;
    const std::vector<build::FileRef> files = build::projectFiles(lp.project);
    const std::vector<build::FileStatus> st = build::assess(lp.project, lp.dir);
    build::Graph g = build::buildGraph(lp.project, build::scanFiles(lp.project, lp.dir, files));
    const std::vector<int> order = build::compileOrder(g);
    int stale = 0;
    for (const int i : order) {
        const build::FileRef f = files[static_cast<std::size_t>(i)];
        const build::FileStatus& s = st[static_cast<std::size_t>(i)];
        const char* word = s.state == build::FileState::UpToDate  ? "ok      "
                           : s.state == build::FileState::Failed  ? "failed  "
                           : s.state == build::FileState::Missing ? "missing "
                                                                  : "stale   ";
        stale += s.state == build::FileState::UpToDate ? 0 : 1;
        std::cout << word << lp.project.libraries[static_cast<std::size_t>(f.library)].name << ": "
                  << lp.project.libraries[static_cast<std::size_t>(f.library)]
                         .files[static_cast<std::size_t>(f.file)]
                         .path;
        if (s.state == build::FileState::OutOfDate)
            std::cout << " (" << build::reasonText(s.reason) << ")";
        std::cout << "\n";
    }
    for (const std::string& w : g.warnings)
        std::cerr << "** Warning: (project status) " << w << "\n";
    std::cout << stale << " of " << files.size() << " files need analysis.\n";
    return 0;
}

} // namespace

int runProjectCommand(const std::vector<std::string>& args, const Environment& env) {
    const std::string sub = args.empty() ? std::string() : args[0];
    const std::vector<std::string> rest(args.begin() + (args.empty() ? 0 : 1), args.end());
    if (sub == "import")
        return importCommand(rest, env);
    if (sub == "status")
        return statusCommand(rest);
    std::cerr << "usage: lvdsim-cli project import <script.do> [-o project.lvdproj] | project status "
                 "<project.lvdproj>\n";
    return 2;
}

} // namespace lvd::tools
