// SPDX-License-Identifier: GPL-3.0-or-later
// SimWindow: code coverage. Collection is switched on with nvc's elaboration options (lvd::tools::cov plans
// them); databases are read through `nvc --cover-export --format=json` (XML for an older nvc), merged and
// reported with `nvc --cover-merge` and `nvc --cover-report`. The simulator is never linked: every step is a
// run of the nvc binary (NvcTool), or a command of its shell (`coverage save -json`).
#include "app/coverage_pane.hpp"
#include "app/portable_mode.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/transcript_widget.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <coverage.hpp>

namespace lvd::gui {

namespace cov = lvd::tools::cov;

namespace {

std::vector<std::string> stdList(const QStringList& l) {
    std::vector<std::string> v;
    v.reserve(static_cast<std::size_t>(l.size()));
    for (const QString& s : l)
        v.push_back(s.toStdString());
    return v;
}

QStringList qtList(const std::vector<std::string>& v) {
    QStringList l;
    for (const std::string& s : v)
        l << QString::fromStdString(s);
    return l;
}

QString shortList(const QStringList& files) {
    QStringList names;
    for (const QString& f : files)
        names << QFileInfo(f).fileName();
    return names.join(QStringLiteral(", "));
}

bool sameFile(const QString& a, const QString& b) {
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath())
               .compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()), cs) == 0;
}

/// Whether a file can be created in the folder (QFileInfo::isWritable does not see Windows ACLs).
bool canCreateFilesIn(const QString& dir) {
    QTemporaryFile probe(QDir(dir).absoluteFilePath(QStringLiteral("lvdsim-probe-XXXXXX")));
    return probe.open();
}

/// Where the databases go when the working directory is not writable (a program folder, for example).
QString fallbackCoverageDir() {
    const PortableDecision p = portableDecision();
    const QString base =
        p.portable ? p.dataDir : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).absoluteFilePath(QStringLiteral("coverage"));
}

} // namespace

void SimWindow::createCoverageDock() {
    coverage_ = new CoveragePane(this);
    auto* dock = new QDockWidget(tr("Coverage"), this);
    dock->setObjectName("coverageDock");
    dock->setWidget(coverage_);
    coverageTool_ = new NvcTool(this);
    connect(coverage_, &CoveragePane::instanceSelected, this, [this](int) { applyCoverageToSource(); });
    connect(coverage_, &CoveragePane::sourceRequested, this, [this](const QString& file, int line) {
        if (!file.isEmpty())
            showSource(coverageSourceFile(file), line);
    });
    connect(&session_, &NvcSession::promptShown, this, [this] { coverageSessionStarted(); });
    // The simulation reached its end (no more events, $finish, std.env.finish): show the counts at once, as
    // Simulation > Refresh coverage would.
    connect(this, &SimWindow::stopped, this, [this](const RunReply& run) {
        if (run.status == RunStatus::Finished && session_.running() && !sessionCoverFile_.isEmpty())
            refreshCoverage();
    });
}

QStringList SimWindow::coveragePlanArgs(QStringList args, const lvd::tools::Context& ctx) {
    if (coverageEnabled_ && !args.contains(QStringLiteral("-coverage")))
        args.prepend(QStringLiteral("-coverage"));
    if (!args.contains(QStringLiteral("-coverage")))
        return args;
    // The database planVsim chooses (<top>.ncdb, or -lvdcoverfile), made absolute: nvc then names it so in
    // its replies, and a project opened during the session (a new working directory) does not move it.
    const lvd::tools::Plan plan = lvd::tools::planVsim(stdList(args), ctx);
    if (!plan.ok() || plan.coverageFile.empty())
        return args; // startSimulation reports the error
    QString file = QDir::current().absoluteFilePath(QString::fromStdString(plan.coverageFile));
    const qsizetype given = args.indexOf(QStringLiteral("-lvdcoverfile"));
    if (given < 0 && !canCreateFilesIn(QFileInfo(file).absolutePath())) {
        // nvc stops the elaboration when it cannot create the database: use the data folder instead.
        const QString dir = fallbackCoverageDir();
        QDir().mkpath(dir);
        const QString moved = QDir(dir).absoluteFilePath(QFileInfo(file).fileName());
        transcript_->appendNote(tr("** Note: (lvdsim) the working directory %1 is not writable; the coverage "
                                   "database goes to %2")
                                    .arg(QDir::toNativeSeparators(QFileInfo(file).absolutePath()),
                                         QDir::toNativeSeparators(moved)));
        file = moved;
    }
    if (given >= 0 && given + 1 < args.size()) {
        args[given + 1] = file;
    } else {
        args.prepend(file);
        args.prepend(QStringLiteral("-lvdcoverfile"));
    }
    return args;
}

QString SimWindow::coverageSourceFile(const QString& name) const {
    if (name.isEmpty())
        return {};
    // nvc keeps the name the file was analysed with; a batch run analyses next to its database.
    if (QFileInfo(name).isRelative()) {
        for (const QString& dir : coverageDirs_) {
            const QString candidate = QDir(dir).absoluteFilePath(name);
            if (QFileInfo::exists(candidate))
                return QDir::cleanPath(candidate);
        }
    }
    return resolveSourceFile(name);
}

void SimWindow::finishCoverageCommand() {
    if (coverageTool_->busy())
        coverageTool_->waitForIdle(120000);
}

void SimWindow::addCoverageFileActions(QMenu* file) {
    QAction* open = file->addAction(tr("Open co&verage..."), this, [this] { openCoverageDialog(); });
    open->setObjectName("openCoverage");
    open->setToolTip(tr("Show coverage databases (.ncdb) in the Coverage dock; several are merged"));
    QAction* merge =
        file->addAction(tr("Save &merged coverage..."), this, [this] { saveMergedCoverageDialog(); });
    merge->setObjectName("saveMergedCoverage");
    QAction* report =
        file->addAction(tr("Coverage &HTML report..."), this, [this] { exportCoverageReportDialog(); });
    report->setObjectName("coverageReport");
    report->setToolTip(tr("Write the HTML coverage report of the loaded databases and open it"));
    connect(file, &QMenu::aboutToShow, this, [this, merge, report] {
        const bool any = !coverageDbs_.isEmpty() || QFileInfo::exists(sessionCoverFile_);
        merge->setEnabled(coverageDbs_.size() > 0);
        report->setEnabled(any);
    });
}

void SimWindow::addCoverageSimulationActions(QMenu* sim) {
    coverageAction_ = sim->addAction(tr("Collect &coverage"));
    coverageAction_->setObjectName("collectCoverage");
    coverageAction_->setCheckable(true);
    coverageAction_->setChecked(coverageEnabled_);
    coverageAction_->setToolTip(
        tr("Elaborate with code coverage (statement, branch, expression, toggle, FSM state, "
           "functional) into <top>.ncdb; applies from the next start or restart. The "
           "Coverage dock shows it when the simulation ends."));
    connect(coverageAction_, &QAction::toggled, this, [this](bool on) { setCoverageEnabled(on); });
    sim->addAction(tr("Show co&verage"), this, [this] { showCoverageDock(); });
    QAction* refresh = sim->addAction(tr("&Refresh coverage"), this, [this] { refreshCoverage(); });
    refresh->setObjectName("refreshCoverage");
    refresh->setToolTip(
        tr("Save the coverage counts of the running simulation so far (coverage save) and show "
           "them in the Coverage dock. Counts add up over restarts of the same simulation."));
    connect(sim, &QMenu::aboutToShow, this,
            [this, refresh] { refresh->setEnabled(session_.running() && !sessionCoverFile_.isEmpty()); });
}

bool SimWindow::refreshCoverage() {
    if (!session_.running() || sessionCoverFile_.isEmpty()) {
        report(tr("no running simulation collects coverage (Simulation > Collect coverage, then start it)"));
        return false;
    }
    request(
        QStringLiteral("coverage save -json"),
        [this](const QString& json) {
            const auto r = parseReply(json);
            const QString file = r.ok() ? r.value().value(QStringLiteral("file")).toString() : QString();
            if (file.isEmpty()) {
                report(tr("unreadable reply to coverage save: %1").arg(json));
                return;
            }
            // The GUI gives nvc an absolute database name (coveragePlanArgs); a relative one (a file named in
            // a typed `coverage save`) is relative to the simulator's working directory, the session's
            // folder.
            const QString abs = QFileInfo(file).isAbsolute()
                                    ? QDir::cleanPath(file)
                                    : QFileInfo(sessionCoverFile_).absoluteDir().absoluteFilePath(file);
            transcript_->appendNote(tr("# Coverage saved at %1 to %2")
                                        .arg(formatFemtoseconds(static_cast<qint64>(
                                            r.value().value(QStringLiteral("now")).toDouble())))
                                        .arg(QDir::toNativeSeparators(abs)));
            finishCoverageCommand();
            openCoverage({abs});
        },
        [this](const QString& error) {
            report(tr("coverage save failed: %1").arg(error.isEmpty() ? tr("no reply") : error));
        });
    return true;
}

void SimWindow::setCoverageEnabled(bool on) {
    coverageEnabled_ = on;
    if (coverageAction_ != nullptr && coverageAction_->isChecked() != on) {
        const QSignalBlocker block(coverageAction_);
        coverageAction_->setChecked(on);
    }
    auto s = openGuiSettings(settingsFile_);
    s->setValue(kCoverageSettingKey, on);
    s->sync();
}

void SimWindow::showCoverageDock() {
    auto* dock = findChild<QDockWidget*>("coverageDock");
    auto* transcriptDock = findChild<QDockWidget*>("transcriptDock");
    if (dock == nullptr)
        return;
    // A tab next to the Transcript, the widest bottom dock, the first time.
    if (transcriptDock != nullptr && !transcriptDock->isHidden() && !transcriptDock->isFloating() &&
        !dock->isFloating() && !tabifiedDockWidgets(transcriptDock).contains(dock))
        tabifyDockWidget(transcriptDock, dock);
    dock->show();
    dock->raise();
}

void SimWindow::setupCoverageTool() {
    NvcTool::Setup s;
    s.nvc = QString::fromStdString(env_.nvc);
    s.globalArgs = qtList(env_.ctx.nvcGlobalArgs);
    if (!env_.nvcDllDir.empty())
        s.pathPrepend << QString::fromStdString(env_.nvcDllDir);
    coverageTool_->setSetup(s);
}

void SimWindow::coverageSessionPlanned(const QString& databaseFile) {
    // planVsim added nvc's coverage options for -coverage (the menu setting or the command line); the
    // simulator runs in the current directory, which a relative database name is relative to.
    sessionCoverFile_.clear();
    sessionCoverStamped_ = false;
    if (databaseFile.isEmpty())
        return;
    sessionCoverFile_ = QDir::current().absoluteFilePath(databaseFile);
    // The elaboration about to start writes the database again. An export of the previous session's counts
    // must have read it first, and the dock must keep what it shows: merge, report and reload of the dock
    // use a copy from now on.
    finishCoverageCommand();
    for (QString& db : coverageDbs_) {
        if (!sameFile(db, sessionCoverFile_))
            continue;
        if (!coverageSnapshots_)
            coverageSnapshots_ = std::make_unique<QTemporaryDir>(
                QDir::temp().absoluteFilePath(QStringLiteral("lvdsim-coverage-XXXXXX")));
        static int serial = 0;
        const QString copy = coverageSnapshots_->filePath(
            QStringLiteral("%1-%2.ncdb").arg(QFileInfo(db).completeBaseName()).arg(++serial));
        if (coverageSnapshots_->isValid() && QFile::copy(db, copy))
            db = copy;
    }
    transcript_->appendNote(
        tr("# Collecting coverage into %1").arg(QDir::toNativeSeparators(sessionCoverFile_)));
}

void SimWindow::coverageSessionStarted() {
    // The elaboration wrote the database before the shell showed its first prompt: its time now tells whether
    // the simulator wrote run-time counts into it when it ends.
    if (sessionCoverFile_.isEmpty() || sessionCoverStamped_)
        return;
    sessionCoverStamped_ = true;
    sessionCoverTime_ = QFileInfo(sessionCoverFile_).lastModified();
}

void SimWindow::coverageSessionFinished() {
    if (sessionCoverFile_.isEmpty())
        return;
    const QFileInfo fi(sessionCoverFile_);
    if (!fi.exists()) {
        transcript_->appendNote(tr("** Warning: (lvdsim) the simulator wrote no coverage database (%1)")
                                    .arg(QDir::toNativeSeparators(sessionCoverFile_)));
        return;
    }
    // The simulator writes the counts when its shell ends. A database not written since the elaboration means
    // it was ended by force (or crashed); show it anyway (a time stamp of a coarse file system can be the
    // same for both writes) and say so.
    if (sessionCoverStamped_ && fi.lastModified() <= sessionCoverTime_)
        transcript_->appendNote(tr("** Warning: (lvdsim) the simulator may have ended without writing its "
                                   "coverage counts to %1; the counts shown can be those of the elaboration "
                                   "or of the last Refresh coverage")
                                    .arg(QDir::toNativeSeparators(sessionCoverFile_)));
    finishCoverageCommand();
    openCoverage({sessionCoverFile_});
}

bool SimWindow::openCoverage(const QStringList& databases) {
    if (databases.isEmpty())
        return false;
    QStringList abs;
    for (const QString& d : databases) {
        if (!QFileInfo(d).isFile()) {
            report(tr("coverage database not found: %1").arg(d));
            return false;
        }
        abs << QFileInfo(d).absoluteFilePath();
    }
    if (coverageTool_->busy()) {
        report(tr("a coverage command is still running"));
        return false;
    }
    setupCoverageTool();
    const QString dir = QString::fromStdString(env_.ctx.tempDir);
    QDir().mkpath(dir);
    const bool json = coverageJson_;
    const QString xml = QDir(dir).absoluteFilePath(json ? QStringLiteral("coverage_export.json")
                                                        : QStringLiteral("coverage_export.xml"));
    QFile::remove(xml);
    const QStringList args = qtList(cov::exportArgs(stdList(abs), xml.toStdString(),
                                                    json ? cov::ExportFormat::Json : cov::ExportFormat::Xml));
    statusBar()->showMessage(tr("Reading coverage %1 ...").arg(shortList(abs)));
    const bool started = coverageTool_->start(args, [this, abs, xml, json](int code, const QString& out) {
        statusBar()->clearMessage();
        if (code != 0 && json && out.contains(QLatin1String("unknown format"))) {
            // A kernel without the JSON export (change set 14): XML from now on.
            coverageJson_ = false;
            QTimer::singleShot(0, this, [this, abs] { openCoverage(abs); });
            return;
        }
        if (code != 0) {
            report(tr("reading coverage failed (nvc exit code %1): %2").arg(code).arg(out.trimmed()));
            return;
        }
        QFile f(xml);
        if (!f.open(QIODevice::ReadOnly)) {
            report(tr("reading coverage failed: nvc wrote no export (%1)").arg(out.trimmed()));
            return;
        }
        const QByteArray data = f.readAll();
        f.close();
        QFile::remove(xml);
        cov::ParseResult r =
            cov::parseExport(std::string_view(data.constData(), static_cast<std::size_t>(data.size())));
        if (!r.ok()) {
            report(tr("cannot read the coverage export: %1").arg(QString::fromStdString(r.error)));
            return;
        }
        coverageDbs_ = abs;
        coverageDirs_.clear();
        for (const QString& d : abs)
            if (!coverageSnapshots_ || !d.startsWith(coverageSnapshots_->path()))
                coverageDirs_ << QFileInfo(d).absolutePath();
        const QString previous = coverage_->selectedInstancePath();
        const cov::Counts c = r.db.totals();
        auto db = std::make_shared<const cov::Database>(std::move(r.db));
        const QString origin = abs.size() == 1
                                   ? QDir::toNativeSeparators(abs.front())
                                   : tr("%1 databases merged: %2").arg(abs.size()).arg(shortList(abs));
        coverage_->setDatabase(db, origin);
        transcript_->appendNote(tr("# Coverage %1: %2 of %3 items covered (%4)")
                                    .arg(shortList(abs))
                                    .arg(c.allHit())
                                    .arg(c.allTotal())
                                    .arg(CoveragePane::percentText(c.overall())));
        if (c.allTotal() == 0)
            transcript_->appendNote(
                tr("** Note: (lvdsim) the coverage database has no items: nvc collects code "
                   "coverage of VHDL only, Verilog and SystemVerilog modules have none"));
        showCoverageDock();
        // A reload (Refresh coverage) keeps the instance the user looks at.
        if (previous.isEmpty() || !coverage_->selectInstance(previous))
            if (const std::vector<int> tops = db->topInstances(); !tops.empty())
                coverage_->selectInstance(QString::fromStdString(cov::displayPath(*db, tops.front())));
        emit coverageLoaded();
    });
    if (!started)
        report(tr("a coverage command is still running"));
    return started;
}

bool SimWindow::saveMergedCoverage(const QString& outFile) {
    if (coverageDbs_.isEmpty()) {
        report(tr("no coverage database is loaded"));
        return false;
    }
    if (coverageTool_->busy()) {
        report(tr("a coverage command is still running"));
        return false;
    }
    setupCoverageTool();
    const QString out = QFileInfo(outFile).absoluteFilePath();
    const QStringList args = qtList(cov::mergeArgs(stdList(coverageDbs_), out.toStdString()));
    return coverageTool_->start(args, [this, out](int code, const QString& text) {
        if (code != 0) {
            report(tr("merging coverage failed (nvc exit code %1): %2").arg(code).arg(text.trimmed()));
            return;
        }
        transcript_->appendNote(tr("# Merged coverage saved to %1").arg(QDir::toNativeSeparators(out)));
    });
}

bool SimWindow::exportCoverageReport(const QString& dir) {
    QStringList dbs = coverageDbs_;
    if (dbs.isEmpty() && QFileInfo::exists(sessionCoverFile_))
        dbs << sessionCoverFile_;
    if (dbs.isEmpty()) {
        report(tr("no coverage database is loaded"));
        return false;
    }
    if (coverageTool_->busy()) {
        report(tr("a coverage command is still running"));
        return false;
    }
    setupCoverageTool();
    const QString outDir = QFileInfo(dir).absoluteFilePath();
    const QStringList args = qtList(cov::reportArgs(stdList(dbs), outDir.toStdString()));
    statusBar()->showMessage(tr("Writing the coverage report ..."));
    return coverageTool_->start(args, [this, outDir](int code, const QString& text) {
        statusBar()->clearMessage();
        const QString index = QDir(outDir).absoluteFilePath(QStringLiteral("index.html"));
        if (code != 0 || !QFileInfo::exists(index)) {
            report(tr("the coverage report failed (nvc exit code %1): %2").arg(code).arg(text.trimmed()));
            emit coverageReportWritten({});
            return;
        }
        transcript_->appendNote(tr("# Coverage report: %1").arg(QDir::toNativeSeparators(index)));
        if (dialogs_)
            QDesktopServices::openUrl(QUrl::fromLocalFile(index));
        emit coverageReportWritten(index);
    });
}

bool SimWindow::waitForCoverageTool(int timeoutMs) {
    return coverageTool_->waitForIdle(timeoutMs);
}

void SimWindow::applyCoverageToSource() {
    QHash<QString, QHash<int, CoverageMark>> byFile;
    const auto marks = coverage_->lineMarks();
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
        byFile.insert(coverageSourceFile(it.key()), it.value());
    source_->setCoverage(byFile);
}

void SimWindow::openCoverageDialog() {
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open coverage"), QDir::currentPath(),
                                                            tr("Coverage databases (*.ncdb);;All files (*)"));
    if (!files.isEmpty())
        openCoverage(files);
}

void SimWindow::saveMergedCoverageDialog() {
    const QString f = QFileDialog::getSaveFileName(
        this, tr("Save merged coverage"), QDir::current().absoluteFilePath(QStringLiteral("merged.ncdb")),
        tr("Coverage databases (*.ncdb)"));
    if (!f.isEmpty())
        saveMergedCoverage(f);
}

void SimWindow::exportCoverageReportDialog() {
    const QString d =
        QFileDialog::getExistingDirectory(this, tr("Folder for the coverage report"), QDir::currentPath());
    if (!d.isEmpty())
        exportCoverageReport(d);
}

} // namespace lvd::gui
