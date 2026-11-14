// SPDX-License-Identifier: GPL-3.0-or-later
#include "compile_runner.hpp"

#include "environment.hpp"
#include "libraries.hpp"
#include "regression.hpp"
#include "text_util.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

namespace lvd::tools::build {

using text::lower;

Context projectContext(const proj::Project& p, const std::string& projectDir, Context ctx) {
    for (const std::string& folder : p.settings.libraryFolders)
        for (const FolderLibrary& l : librariesInFolder(absolutePath(projectDir, folder)))
            if (!ctx.libs.pathOf(l.name))
                ctx.libs.set(l.name, l.path);
    for (const auto& [name, path] : p.settings.mappings)
        ctx.libs.set(name, absolutePath(projectDir, path));
    for (const proj::Library& l : p.libraries)
        ctx.libs.set(l.name, absolutePath(projectDir, p.libraryPath(l)));
    return ctx;
}

std::vector<std::pair<std::string, std::string>>
projectLibraries(const proj::Project& p, const std::string& projectDir, const Context& projectCtx) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const proj::Library& l : p.libraries)
        out.emplace_back(l.name, absolutePath(projectDir, p.libraryPath(l)));
    for (const auto& [name, path] : projectCtx.libs.entries()) {
        if (p.findLibrary(name) != nullptr)
            continue;
        std::error_code ec;
        if (fs::is_directory(path, ec))
            out.emplace_back(name, absolutePath(projectDir, path));
    }
    return out;
}

namespace {

std::string vcomStd(const std::string& s) {
    if (s == "93")
        return "-93";
    if (s == "02")
        return "-2002";
    if (s == "19")
        return "-2019";
    return "-2008";
}

} // namespace

FileCall analyseCall(const proj::Project& p, FileRef f, const Context& ctx) {
    FileCall out;
    const proj::Library& lib = p.libraries.at(static_cast<std::size_t>(f.library));
    const proj::SourceFile& sf = lib.files.at(static_cast<std::size_t>(f.file));
    std::vector<std::string> a = {"-work", lib.name};
    // Every other library is visible to the file; "work" always means the file's own library in nvc, so a
    // project library called work is never mapped under that name for files of other libraries.
    for (const auto& [name, path] : ctx.libs.entries())
        if (lower(name) != lower(lib.name) && lower(name) != "work") {
            a.push_back("-L");
            a.push_back(name);
        }
    lvd::tools::Plan plan;
    if (sf.isVerilog()) {
        if (sf.effectiveLanguage() == proj::Language::SystemVerilog)
            a.push_back("-sv");
        for (const std::string& d : sf.defines)
            a.push_back("+define+" + d);
        for (const std::string& d : sf.incdirs)
            a.push_back("+incdir+" + d);
        a.push_back(sf.path);
        plan = planVlog(a, ctx);
    } else {
        a.push_back(vcomStd(p.vhdlOf(sf)));
        if (sf.relaxed)
            a.push_back("-explicit");
        a.push_back(sf.path);
        plan = planVcom(a, ctx);
    }
    if (!plan.ok())
        out.error = plan.error;
    else if (plan.calls.size() != 1)
        out.error = "unexpected analysis plan";
    else
        out.args = plan.calls.front().args;
    return out;
}

std::vector<FileStatus> assess(const proj::Project& p, const std::string& projectDir) {
    const std::vector<FileRef> files = projectFiles(p);
    Graph g = buildGraph(p, scanFiles(p, projectDir, files));
    const State state = parseState(readTextFile(absolutePath(projectDir, statePath(p))).value_or(""));
    std::vector<std::optional<FileStamp>> stamps;
    for (const FileRef f : files) {
        const auto it = state.files.find(fileKey(p, f));
        stamps.push_back(stampFile(p, projectDir, f, it == state.files.end() ? nullptr : &it->second));
    }
    const Plan plan = planCompile(p, g, state, stamps, librariesPresent(p, projectDir), Mode::OutOfDate);
    std::vector<FileStatus> out(files.size());
    for (std::size_t i = 0; i < files.size(); ++i)
        out[i].state = FileState::UpToDate;
    for (const Step& s : plan.steps) {
        FileStatus& st = out[static_cast<std::size_t>(s.file)];
        st.state = s.reason == Reason::Failed ? FileState::Failed : FileState::OutOfDate;
        st.reason = s.reason;
        st.cause = s.cause;
    }
    for (std::size_t i = 0; i < files.size(); ++i)
        if (!stamps[i])
            out[i].state = FileState::Missing;
    return out;
}

// ---------------------------------------------------------------------------------------------

Runner::Runner(Setup setup)
    : setup_(std::move(setup)) {}

Runner::~Runner() {
    onStarted = nullptr;
    onFinished = nullptr;
    cancel();
}

bool Runner::prepare(const proj::Project& p, Mode mode, const std::set<int>& selected, std::string* error) {
    project_ = p;
    ctx_ = projectContext(p, setup_.projectDir, setup_.ctx);
    std::error_code ec;
    const std::string root =
        absolutePath(setup_.projectDir, p.settings.libraryRoot.empty() ? "." : p.settings.libraryRoot);
    fs::create_directories(root, ec);
    if (ec) {
        *error = "cannot create '" + root + "': " + ec.message();
        return false;
    }
    for (const proj::Library& l : p.libraries) {
        // As vlib does: the directory with nvc's library marker (nvc warns about a plain directory).
        const fs::path dir = absolutePath(setup_.projectDir, p.libraryPath(l));
        fs::create_directories(dir, ec);
        if (ec) {
            *error = "cannot create the directory of library " + l.name + ": " + ec.message();
            return false;
        }
        if (!fs::exists(dir / "_NVC_LIB", ec))
            std::ofstream(dir / "_NVC_LIB", std::ios::binary) << "lvdsim\n";
    }
    logFile_ = root + "/.lvdsim-compile.log";

    const std::vector<FileRef> files = projectFiles(p);
    graph_ = buildGraph(p, scanFiles(p, setup_.projectDir, files));
    state_ = parseState(readTextFile(absolutePath(setup_.projectDir, statePath(p))).value_or(""));
    stamps_.clear();
    for (const FileRef f : files) {
        const auto it = state_.files.find(fileKey(p, f));
        stamps_.push_back(
            stampFile(p, setup_.projectDir, f, it == state_.files.end() ? nullptr : &it->second));
    }
    plan_ = planCompile(p, graph_, state_, stamps_, librariesPresent(p, setup_.projectDir), mode, selected);
    states_.assign(plan_.steps.size(), StepState::Pending);
    outputs_.assign(plan_.steps.size(), {});
    args_.assign(plan_.steps.size(), {});
    failedFiles_.clear();
    next_ = 0;
    return true;
}

std::string Runner::fileName(std::size_t step) const {
    const FileRef f = graph_.files.at(static_cast<std::size_t>(plan_.steps.at(step).file));
    return project_.libraries.at(static_cast<std::size_t>(f.library))
        .files.at(static_cast<std::size_t>(f.file))
        .path;
}

std::string Runner::libraryName(std::size_t step) const {
    const FileRef f = graph_.files.at(static_cast<std::size_t>(plan_.steps.at(step).file));
    return project_.libraries.at(static_cast<std::size_t>(f.library)).name;
}

std::vector<diag::Diagnostic> Runner::diagnostics(std::size_t step) const {
    return diag::parse(outputs_.at(step));
}

int Runner::failedCount() const {
    int n = 0;
    for (const StepState s : states_)
        n += s == StepState::Failed ? 1 : 0;
    return n;
}

int Runner::okCount() const {
    int n = 0;
    for (const StepState s : states_)
        n += s == StepState::Ok ? 1 : 0;
    return n;
}

void Runner::saveState() {
    const std::string path = absolutePath(setup_.projectDir, statePath(project_));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << writeState(state_);
}

void Runner::startStep(std::size_t k) {
    current_ = k;
    const int file = plan_.steps[k].file;
    for (const int d : graph_.deps[static_cast<std::size_t>(file)]) {
        if (failedFiles_.count(d) == 0)
            continue;
        states_[k] = StepState::Skipped;
        outputs_[k] = "** Note: skipped: a file it depends on failed\n";
        failedFiles_.insert(file);
        if (onFinished)
            onFinished(k);
        return;
    }
    const FileCall call = analyseCall(project_, graph_.files[static_cast<std::size_t>(file)], ctx_);
    states_[k] = StepState::Running;
    if (onStarted)
        onStarted(k);
    if (!call.error.empty()) {
        outputs_[k] = "** Error: " + call.error + "\n";
        finishStep(false, 1);
        return;
    }
    args_[k] = call.args;
    std::vector<std::string> argv{setup_.nvc};
    argv.insert(argv.end(), call.args.begin(), call.args.end());
    ProcessOptions po = setup_.process;
    po.workingDir = setup_.projectDir;
    child_ = std::make_unique<ChildProcess>();
    std::string error;
    if (!child_->start(argv, po, logFile_, &error)) {
        child_.reset();
        outputs_[k] = "** Error: " + error + "\n";
        finishStep(false, -1);
    }
}

void Runner::finishStep(bool ok, int exitCode) {
    const std::size_t k = current_;
    const int file = plan_.steps[k].file;
    if (child_) {
        outputs_[k] = regress::readLog(logFile_);
        child_.reset();
    }
    ok = ok && exitCode == 0;
    states_[k] = ok ? StepState::Ok : StepState::Failed;
    const FileRef f = graph_.files[static_cast<std::size_t>(file)];
    FileStamp st = stamps_[static_cast<std::size_t>(file)].value_or(FileStamp{});
    st.ok = ok;
    state_.files[fileKey(project_, f)] = st;
    saveState();
    if (!ok)
        failedFiles_.insert(file);
    if (onFinished)
        onFinished(k);
}

void Runner::step() {
    if (child_) {
        if (!child_->poll())
            return;
        finishStep(true, child_->exitCode());
    }
    while (!child_ && next_ < plan_.steps.size())
        startStep(next_++);
}

void Runner::cancel() {
    if (child_) {
        child_->kill();
        child_.reset();
        states_[current_] = StepState::Skipped;
        outputs_[current_] += "** Note: cancelled\n";
    }
    for (; next_ < plan_.steps.size(); ++next_)
        states_[next_] = StepState::Skipped;
}

void Runner::runToEnd(int pollMs) {
    step();
    while (!finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        step();
    }
}

} // namespace lvd::tools::build
