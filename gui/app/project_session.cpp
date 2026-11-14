// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/project_session.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <coverage.hpp>
#include <fstream>
#include <thread>

namespace lvd::gui {

namespace proj = lvd::tools::proj;
namespace build = lvd::tools::build;
namespace regress = lvd::tools::regress;

namespace {

QString q(const std::string& s) {
    return QString::fromStdString(s);
}

QStringList qtList(const std::vector<std::string>& v) {
    QStringList l;
    for (const std::string& s : v)
        l << q(s);
    return l;
}

} // namespace

ProjectSession::ProjectSession(lvd::tools::Environment env, QObject* parent)
    : QObject(parent)
    , env_(std::move(env)) {
    compileTimer_.setInterval(15);
    connect(&compileTimer_, &QTimer::timeout, this, [this] { compileTick(); });
    regressionTimer_.setInterval(25);
    connect(&regressionTimer_, &QTimer::timeout, this, [this] { regressionTick(); });
    tool_ = new NvcTool(this);
    NvcTool::Setup s;
    s.nvc = q(env_.nvc);
    s.globalArgs = qtList(env_.ctx.nvcGlobalArgs);
    if (!env_.nvcDllDir.empty())
        s.pathPrepend << q(env_.nvcDllDir);
    tool_->setSetup(s);
}

ProjectSession::~ProjectSession() {
    compiler_.reset();
    regression_.reset();
}

void ProjectSession::setError(const QString& e) {
    error_ = e;
}

QString ProjectSession::displayName() const {
    if (!open_)
        return {};
    return !project_.settings.name.empty() ? q(project_.settings.name) : QFileInfo(path_).completeBaseName();
}

bool ProjectSession::open(const QString& path) {
    const proj::LoadedProject lp = proj::loadProject(QDir::cleanPath(path).toStdString());
    if (!lp.ok()) {
        setError(q(lp.error));
        return false;
    }
    close();
    project_ = lp.project;
    path_ = q(lp.path);
    dir_ = q(lp.dir);
    open_ = true;
    modified_ = false;
    for (const proj::ParseMessage& w : lp.warnings)
        emit message(tr("** Warning: %1:%2: %3").arg(path_).arg(w.line).arg(q(w.text)));
    refreshStatus();
    emit projectChanged();
    return true;
}

bool ProjectSession::create(const QString& path, const QString& name) {
    proj::Project p;
    p.settings.name = (name.isEmpty() ? QFileInfo(path).completeBaseName() : name).toStdString();
    p.libraries.push_back({});
    p.libraries.back().name = "work";
    std::string err;
    if (!proj::saveProject(p, path.toStdString(), &err)) {
        setError(q(err));
        return false;
    }
    return open(path);
}

bool ProjectSession::save() {
    if (!open_)
        return false;
    std::string err;
    if (!proj::saveProject(project_, path_.toStdString(), &err)) {
        setError(q(err));
        return false;
    }
    modified_ = false;
    emit projectChanged();
    return true;
}

bool ProjectSession::saveAs(const QString& path) {
    if (!open_)
        return false;
    const QString abs = QFileInfo(path).absoluteFilePath();
    const QString newDir = QFileInfo(abs).absolutePath();
    if (QDir(newDir) != QDir(dir_)) {
        // Project paths are relative to the project file: rewrite them for the new directory.
        auto rebase = [&](std::string& p) {
            if (p.empty() || QDir::isAbsolutePath(q(p)))
                return;
            p = QDir(newDir).relativeFilePath(QDir(dir_).absoluteFilePath(q(p))).toStdString();
        };
        for (proj::Library& l : project_.libraries) {
            rebase(l.path);
            for (proj::SourceFile& f : l.files) {
                rebase(f.path);
                for (std::string& d : f.incdirs)
                    rebase(d);
            }
        }
        for (proj::Test& t : project_.tests)
            rebase(t.doScript);
        rebase(project_.settings.libraryRoot);
        rebase(project_.settings.regressionDir);
    }
    path_ = abs;
    dir_ = newDir;
    return save();
}

void ProjectSession::close() {
    cancelCompile();
    cancelRegression();
    project_ = {};
    open_ = false;
    modified_ = false;
    path_.clear();
    dir_.clear();
    statuses_.clear();
    problems_.clear();
    mergedCoverage_.clear();
    emit projectChanged();
}

void ProjectSession::touched() {
    modified_ = true;
    refreshStatus();
    emit projectChanged();
}

QString ProjectSession::absolute(const QString& projectPath) const {
    return q(build::absolutePath(dir_.toStdString(), projectPath.toStdString()));
}

bool ProjectSession::addLibrary(const QString& name) {
    if (!open_ || !proj::isValidName(name.toStdString()) || project_.findLibrary(name.toStdString())) {
        setError(tr("'%1' is not a new library name").arg(name));
        return false;
    }
    project_.libraries.push_back({});
    project_.libraries.back().name = name.toStdString();
    touched();
    return true;
}

bool ProjectSession::removeLibrary(int library) {
    if (library < 0 || library >= static_cast<int>(project_.libraries.size()))
        return false;
    project_.libraries.erase(project_.libraries.begin() + library);
    touched();
    return true;
}

int ProjectSession::addFiles(int library, const QStringList& files) {
    if (library < 0 || library >= static_cast<int>(project_.libraries.size()))
        return 0;
    proj::Library& lib = project_.libraries[static_cast<std::size_t>(library)];
    int added = 0;
    for (const QString& f : files) {
        const QString abs = QFileInfo(f).absoluteFilePath();
        QString rel = QDir(dir_).relativeFilePath(abs);
        if (rel.startsWith(QLatin1String("../../../")) || QDir::isAbsolutePath(rel))
            rel = abs; // far away (or another drive): keep it absolute
        const bool known = std::any_of(lib.files.begin(), lib.files.end(),
                                       [&](const proj::SourceFile& s) { return q(s.path) == rel; });
        if (known)
            continue;
        lib.files.push_back({});
        lib.files.back().path = rel.toStdString();
        ++added;
    }
    if (added > 0)
        touched();
    return added;
}

bool ProjectSession::removeFile(int library, int file) {
    if (fileIndex(library, file) < 0)
        return false;
    auto& files = project_.libraries[static_cast<std::size_t>(library)].files;
    files.erase(files.begin() + file);
    touched();
    return true;
}

bool ProjectSession::moveFile(int library, int file, int delta) {
    if (fileIndex(library, file) < 0)
        return false;
    auto& files = project_.libraries[static_cast<std::size_t>(library)].files;
    const int to = file + delta;
    if (to < 0 || to >= static_cast<int>(files.size()) || delta == 0)
        return false;
    const proj::SourceFile f = files[static_cast<std::size_t>(file)];
    files.erase(files.begin() + file);
    files.insert(files.begin() + to, f);
    touched();
    return true;
}

bool ProjectSession::addTest(const proj::Test& test) {
    if (!open_ || !proj::isValidName(test.name) || project_.findTest(test.name) != nullptr ||
        test.top.empty()) {
        setError(tr("a test needs a new name and a top level"));
        return false;
    }
    project_.tests.push_back(test);
    touched();
    return true;
}

bool ProjectSession::removeTest(int test) {
    if (test < 0 || test >= static_cast<int>(project_.tests.size()))
        return false;
    project_.tests.erase(project_.tests.begin() + test);
    touched();
    return true;
}

void ProjectSession::setProject(proj::Project p) {
    project_ = std::move(p);
    touched();
}

int ProjectSession::fileIndex(int library, int file) const {
    int n = 0;
    for (int l = 0; l < static_cast<int>(project_.libraries.size()); ++l) {
        const int count = static_cast<int>(project_.libraries[static_cast<std::size_t>(l)].files.size());
        if (l == library)
            return (file >= 0 && file < count) ? n + file : -1;
        n += count;
    }
    return -1;
}

void ProjectSession::refreshStatus() {
    statuses_ = open_ ? build::assess(project_, dir_.toStdString()) : std::vector<build::FileStatus>{};
    emit statusChanged();
}

// ---------------------------------------------------------------------------------------------
// Analysis

bool ProjectSession::compile(build::Mode mode, const std::set<int>& selected) {
    if (!open_ || compiler_) {
        setError(open_ ? tr("an analysis is running") : tr("no project is open"));
        return false;
    }
    build::Setup s;
    s.projectDir = dir_.toStdString();
    s.ctx = env_.ctx;
    s.nvc = env_.nvc;
    s.process = env_.processOptions();
    auto runner = std::make_unique<build::Runner>(s);
    std::string err;
    if (!runner->prepare(project_, mode, selected, &err)) {
        setError(q(err));
        return false;
    }
    for (const std::string& w : runner->graph().warnings)
        emit message(tr("** Warning: (compile) %1").arg(q(w)));
    problems_.clear();
    const int steps = static_cast<int>(runner->plan().steps.size());
    if (steps == 0) {
        emit message(tr("All %n file(s) are up to date.", "", static_cast<int>(project_.fileCount())));
        lastFailures_ = 0;
        emit compileStarted(0);
        emit compileFinished(0);
        afterCompile();
        return true;
    }
    build::Runner* r = runner.get();
    r->onStarted = [this, r](std::size_t k) {
        compilingFile_ = r->plan().steps[k].file;
        emit message(tr("-- Analysing %1: %2").arg(q(r->libraryName(k)), q(r->fileName(k))));
        emit compileProgress(compilingFile_);
    };
    r->onFinished = [this, r](std::size_t k) {
        const QString file = q(r->fileName(k));
        if (r->state(k) == build::Runner::StepState::Skipped)
            emit message(tr("-- Skipped %1: a file it depends on failed").arg(file));
        else if (!r->output(k).empty())
            emit message(q(r->output(k)).trimmed());
        for (const lvd::tools::diag::Diagnostic& d : r->diagnostics(k)) {
            if (d.severity == lvd::tools::diag::Severity::Note)
                continue;
            Problem p;
            p.severity = d.severity;
            p.message = q(d.message);
            p.file = d.file.empty() ? QString() : absolute(q(d.file));
            p.line = d.line;
            p.column = d.column;
            p.detail = q(d.detail);
            p.source = file;
            problems_.push_back(p);
        }
        compilingFile_ = -1;
        emit compileProgress(r->plan().steps[k].file);
    };
    compiler_ = std::move(runner);
    emit compileStarted(steps);
    compileTimer_.start();
    compileTick();
    return true;
}

void ProjectSession::compileTick() {
    if (!compiler_)
        return;
    compiler_->step();
    if (!compiler_->finished())
        return;
    compileTimer_.stop();
    int skipped = 0;
    for (std::size_t k = 0; k < compiler_->plan().steps.size(); ++k)
        skipped += compiler_->state(k) == build::Runner::StepState::Skipped ? 1 : 0;
    lastFailures_ = compiler_->failedCount() + skipped;
    emit message(tr("Analysed %1 of %2 files%3.")
                     .arg(compiler_->okCount())
                     .arg(compiler_->plan().steps.size())
                     .arg(lastFailures_ > 0 ? tr(", %1 failed or skipped").arg(lastFailures_) : QString()));
    compiler_.reset();
    compilingFile_ = -1;
    refreshStatus();
    emit compileFinished(lastFailures_);
    afterCompile();
}

void ProjectSession::cancelCompile() {
    if (!compiler_)
        return;
    compileTimer_.stop();
    compiler_.reset(); // ends the running nvc
    compilingFile_ = -1;
    lastFailures_ = 1;
    refreshStatus();
    emit compileFinished(lastFailures_);
}

bool ProjectSession::waitForCompile(int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (compiler_ && t.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !compiler_;
}

// ---------------------------------------------------------------------------------------------
// Regression

std::vector<std::pair<std::string, std::string>> ProjectSession::libraryMappings() const {
    if (!open_)
        return {};
    const lvd::tools::Context ctx = build::projectContext(project_, dir_.toStdString(), env_.ctx);
    return build::projectLibraries(project_, dir_.toStdString(), ctx);
}

regress::PlanOptions ProjectSession::planOptions() const {
    regress::PlanOptions o;
    o.projectDir = dir_.toStdString();
    o.nvcGlobalArgs = env_.ctx.nvcGlobalArgs;
    o.compatTcl = env_.ctx.compatTcl;
    o.libraries = libraryMappings();
    return o;
}

void ProjectSession::setJobs(int n) {
    jobs_ = std::max(0, n);
}

bool ProjectSession::runRegression(const QStringList& only) {
    if (!open_ || regressing_ || regressionQueued_) {
        setError(open_ ? tr("a regression is running") : tr("no project is open"));
        return false;
    }
    refreshStatus();
    const bool stale = std::any_of(statuses_.begin(), statuses_.end(), [](const build::FileStatus& st) {
        return st.state != build::FileState::UpToDate;
    });
    if (!stale)
        return startRegression(only);
    regressionQueued_ = true;
    queuedTests_ = only;
    if (!compile(build::Mode::OutOfDate)) {
        regressionQueued_ = false;
        return false;
    }
    return true;
}

void ProjectSession::afterCompile() {
    if (!regressionQueued_)
        return;
    regressionQueued_ = false;
    if (lastFailures_ > 0) {
        emit message(tr("** Error: the design did not analyse; the regression was not started"));
        emit regressionFinished(0, 0);
        return;
    }
    if (!startRegression(queuedTests_)) {
        emit message(tr("** Error: %1").arg(error_));
        emit regressionFinished(0, 0);
    }
}

bool ProjectSession::startRegression(const QStringList& only) {
    if (!open_ || regressing_) {
        setError(open_ ? tr("a regression is running") : tr("no project is open"));
        return false;
    }
    regress::PlanOptions o = planOptions();
    for (const QString& s : only)
        o.only.push_back(s.toStdString());
    const regress::PlanResult plan = regress::planJobs(project_, o);
    if (!plan.ok()) {
        QStringList errs;
        for (const std::string& e : plan.errors)
            errs << q(e);
        setError(errs.join(QLatin1Char('\n')));
        return false;
    }
    if (plan.jobs.empty()) {
        setError(tr("the project has no tests"));
        return false;
    }
    outDir_ = absolute(q(project_.settings.regressionDir));
    regress::RunnerConfig rc;
    rc.nvc = env_.nvc;
    rc.process = env_.processOptions();
    rc.process.workingDir = dir_.toStdString();
    rc.jobs = jobs_ > 0                    ? jobs_
              : project_.settings.jobs > 0 ? project_.settings.jobs
                                           : std::max(1, QThread::idealThreadCount());
    regression_ = std::make_unique<regress::Runner>(plan.jobs, rc);
    regression_->onStarted = [this](int i) { emit jobChanged(i); };
    regression_->onFinished = [this](int i) { emit jobChanged(i); };
    regressing_ = true;
    mergedCoverage_.clear();
    emit message(
        tr("Regression: %n run(s), %1 at a time", "", static_cast<int>(plan.jobs.size())).arg(rc.jobs));
    emit regressionStarted(static_cast<int>(plan.jobs.size()));
    regressionTimer_.start();
    regressionTick();
    return true;
}

bool ProjectSession::rerunFailed() {
    const QStringList ids = failedIds();
    if (ids.isEmpty()) {
        setError(tr("no failed test to run again"));
        return false;
    }
    return runRegression(ids);
}

QStringList ProjectSession::failedIds() const {
    QStringList ids;
    if (!regression_)
        return ids;
    for (std::size_t i = 0; i < regression_->jobs().size(); ++i) {
        const regress::Status s = regression_->outcomes()[i].status;
        if (regress::isDone(s) && s != regress::Status::Passed)
            ids << q(regression_->jobs()[i].id);
    }
    return ids;
}

void ProjectSession::regressionTick() {
    if (!regression_ || !regressing_)
        return;
    regression_->step();
    if (regression_->finished())
        regressionDone();
}

void ProjectSession::regressionDone() {
    regressionTimer_.stop();
    regressing_ = false;
    int passed = 0;
    for (const regress::Outcome& o : regression_->outcomes())
        passed += o.status == regress::Status::Passed ? 1 : 0;
    const int total = static_cast<int>(regression_->jobs().size());
    QDir().mkpath(outDir_);
    std::ofstream(QDir(outDir_).filePath("results.txt").toStdString(), std::ios::binary | std::ios::trunc)
        << regress::writeResults(regression_->jobs(), regression_->outcomes());
    emit message(tr("Regression: %1 of %2 passed.").arg(passed).arg(total));
    emit regressionFinished(passed, total);

    const std::vector<std::string> dbs = regress::coverageDatabases(regression_->jobs());
    if (dbs.empty())
        return;
    const QString merged = QDir(outDir_).filePath("merged.ncdb");
    QFile::remove(merged);
    tool_->start(qtList(lvd::tools::cov::mergeArgs(dbs, merged.toStdString())),
                 [this, merged](int code, const QString& out) {
                     if (code != 0 || !QFileInfo::exists(merged)) {
                         emit message(tr("** Warning: merging coverage failed\n%1").arg(out));
                         return;
                     }
                     mergedCoverage_ = merged;
                     emit message(tr("Coverage of the regression merged into %1").arg(merged));
                     emit coverageMerged(merged);
                 });
}

void ProjectSession::cancelRegression() {
    if (!regression_ || !regressing_)
        return;
    regression_->cancel();
    regressionDone();
}

bool ProjectSession::waitForRegression(int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while ((regressing_ || regressionQueued_ || compiler_ || tool_->busy()) && t.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !regressing_ && !regressionQueued_ && !compiler_ && !tool_->busy();
}

QStringList ProjectSession::vsimArgsFor(const QString& name) const {
    const proj::Test* t = project_.findTest(name.toStdString());
    if (t == nullptr)
        return {};
    QStringList a;
    for (const std::string& g : t->generics)
        a << "-g" + q(g);
    if (!t->seeds.empty() && !t->seedGeneric.empty())
        a << QStringLiteral("-g%1=%2").arg(q(t->seedGeneric)).arg(t->seeds.front());
    if (!t->doScript.empty())
        a << QStringLiteral("-do") << absolute(q(t->doScript));
    else if (!t->commands.empty())
        a << QStringLiteral("-do") << q(t->commands);
    for (const std::string& top : lvd::tools::text::splitWords(t->top))
        a << q(top);
    return a;
}

} // namespace lvd::gui
