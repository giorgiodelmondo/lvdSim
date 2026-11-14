// SPDX-License-Identifier: GPL-3.0-or-later
// SimWindow: projects. The Project menu, the Project dock (libraries, files, tests), the Problems dock
// (messages of the last analysis, clickable) and the Regression dock (batch runs of the tests). The work is
// done by ProjectSession with the Qt-free code of tools/ (project file, incremental analysis, regression
// runner).
#include "app/portable_mode.hpp"
#include "app/problems_pane.hpp"
#include "app/project_pane.hpp"
#include "app/project_session.hpp"
#include "app/regression_pane.hpp"
#include "app/sim_window.hpp"
#include "app/transcript_widget.hpp"

#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QStatusBar>

#include <do_import.hpp>
#include <environment.hpp>

namespace lvd::gui {

void SimWindow::createProjectDocks() {
    project_ = new ProjectSession(env_, this);
    projectPane_ = new ProjectPane(project_, this);
    auto* projectDock = new QDockWidget(tr("Project"), this);
    projectDock->setObjectName("projectDock");
    projectDock->setWidget(projectPane_);

    problems_ = new ProblemsPane(this);
    auto* problemsDock = new QDockWidget(tr("Problems"), this);
    problemsDock->setObjectName("problemsDock");
    problemsDock->setWidget(problems_);

    regressionPane_ = new RegressionPane(project_, this);
    auto* regressionDock = new QDockWidget(tr("Regression"), this);
    regressionDock->setObjectName("regressionDock");
    regressionDock->setWidget(regressionPane_);

    connect(project_, &ProjectSession::message, this,
            [this](const QString& t) { transcript_->appendNote(t); });
    connect(project_, &ProjectSession::projectChanged, this, [this] { updateProjectTitle(); });
    connect(project_, &ProjectSession::compileStarted, this, [this](int steps) {
        if (steps > 0)
            statusBar()->showMessage(tr("Analysing %n file(s)...", "", steps));
    });
    connect(project_, &ProjectSession::compileFinished, this, [this](int failed) {
        problems_->setProblems(project_->problems());
        refreshLibraries();
        statusBar()->showMessage(failed == 0 ? tr("Analysis done") : tr("Analysis failed"), 8000);
        if (problems_->count() > 0)
            showBottomDock(QStringLiteral("problems"));
        if (restartAfterCompile_) {
            restartAfterCompile_ = false;
            if (failed == 0)
                relaunchSimulation();
            else
                report(tr("the design did not analyse; the simulation was not started again"));
        }
        const QString test = pendingTest_;
        pendingTest_.clear();
        if (!test.isEmpty()) {
            if (failed == 0)
                startSimulation(project_->vsimArgsFor(test));
            else
                report(tr("the design did not analyse; test %1 was not started").arg(test));
        }
    });
    connect(project_, &ProjectSession::regressionStarted, this,
            [this](int) { showBottomDock(QStringLiteral("regression")); });
    connect(project_, &ProjectSession::regressionFinished, this, [this](int passed, int total) {
        statusBar()->showMessage(tr("Regression: %1 of %2 passed").arg(passed).arg(total), 10000);
    });
    connect(projectPane_, &ProjectPane::sourceRequested, this,
            [this](const QString& f, int line) { showSource(f, line); });
    connect(projectPane_, &ProjectPane::simulateRequested, this,
            [this](const QString& t) { simulateTest(t); });
    connect(projectPane_, &ProjectPane::regressionRequested, this, [this](const QStringList& tests) {
        if (!project_->runRegression(tests))
            report(project_->lastError());
    });
    connect(problems_, &ProblemsPane::sourceRequested, this,
            [this](const QString& f, int line) { showSource(f, line); });
    connect(regressionPane_, &RegressionPane::logRequested, this,
            [this](const QString& f) { showSource(f, 0); });
    connect(regressionPane_, &RegressionPane::waveRequested, this,
            [this](const QString& f, const QString& id) {
                QString name = id;
                name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
                if (!openDataset(f, name))
                    report(lastError_);
            });
    connect(regressionPane_, &RegressionPane::coverageRequested, this, [this](const QString& db) {
        if (!db.isEmpty() && openCoverage({db}))
            showCoverageDock();
    });
}

void SimWindow::addProjectMenu() {
    QMenu* m = menuBar()->addMenu(tr("&Project"));
    m->setObjectName("projectMenu");
    m->addAction(tr("&New project..."), this, [this] { newProjectDialog(); })->setObjectName("newProject");
    m->addAction(tr("&Open project..."), this, [this] { openProjectDialog(); })->setObjectName("openProject");
    recentProjectsMenu_ = m->addMenu(tr("&Recent projects"));
    recentProjectsMenu_->setObjectName("recentProjects");
    connect(recentProjectsMenu_, &QMenu::aboutToShow, this, [this] {
        recentProjectsMenu_->clear();
        for (const QString& f : recentProjects())
            recentProjectsMenu_->addAction(QDir::toNativeSeparators(f), this, [this, f] {
                if (maybeSaveProject() && !openProject(f))
                    report(lastError_);
            });
        if (recentProjectsMenu_->isEmpty())
            recentProjectsMenu_->addAction(tr("(none)"))->setEnabled(false);
    });
    QAction* save = m->addAction(tr("&Save project"), this, [this] {
        if (project_->isOpen() && !project_->save())
            report(project_->lastError());
    });
    save->setObjectName("saveProject");
    m->addAction(tr("Save project &as..."), this, [this] {
        if (!project_->isOpen() || !dialogs_)
            return;
        const QString f = QFileDialog::getSaveFileName(this, tr("Save project as"), project_->path(),
                                                       tr("lvdsim projects (*.lvdproj)"));
        if (!f.isEmpty() && !project_->saveAs(f))
            report(project_->lastError());
        else if (!f.isEmpty())
            addRecentProject(project_->path());
    });
    m->addAction(tr("&Close project"), this, [this] {
        if (maybeSaveProject())
            project_->close();
    });
    m->addSeparator();
    m->addAction(tr("&Import compile script (.do)..."), this, [this] {
         importProjectDialog();
     })->setObjectName("importProject");
    m->addSeparator();
    QAction* compile = m->addAction(tr("&Compile out-of-date"), this, [this] {
        if (!project_->compile(lvd::tools::build::Mode::OutOfDate))
            report(project_->lastError());
    });
    compile->setObjectName("compileProject");
    compile->setShortcut(QKeySequence(Qt::Key_F7));
    m->addAction(tr("Compile &all"), this, [this] {
        if (!project_->compile(lvd::tools::build::Mode::All))
            report(project_->lastError());
    });
    m->addSeparator();
    m->addAction(tr("Run &regression"), this, [this] {
         if (!project_->runRegression())
             report(project_->lastError());
     })->setObjectName("runRegression");
    m->addAction(tr("Rerun &failed tests"), this, [this] {
        if (!project_->rerunFailed())
            report(project_->lastError());
    });
    connect(m, &QMenu::aboutToShow, this, [this, m] {
        const bool open = project_->isOpen();
        for (QAction* a : m->actions()) {
            const QString n = a->objectName();
            if (n != "newProject" && n != "openProject" && n != "importProject" && a->menu() == nullptr &&
                !a->isSeparator())
                a->setEnabled(open);
        }
    });
}

QStringList SimWindow::recentProjects() const {
    auto s = openGuiSettings(settingsFile_);
    return s->value(kRecentProjectsKey).toStringList();
}

void SimWindow::addRecentProject(const QString& path) {
    auto s = openGuiSettings(settingsFile_);
    QStringList l = s->value(kRecentProjectsKey).toStringList();
    l.removeAll(path);
    l.prepend(path);
    while (l.size() > kRecentProjectsLimit)
        l.removeLast();
    s->setValue(kRecentProjectsKey, l);
    s->sync();
}

bool SimWindow::openProject(const QString& path) {
    if (!project_->open(path)) {
        lastError_ = project_->lastError();
        return false;
    }
    // Relative names (libraries, sources, the simulator's file names) are relative to the project.
    QDir::setCurrent(project_->dir());
    transcript_->appendNote(
        tr("Project %1 opened; working directory %2")
            .arg(QDir::toNativeSeparators(project_->path()), QDir::toNativeSeparators(project_->dir())));
    addRecentProject(project_->path());
    refreshLibraries();
    if (auto* d = findChild<QDockWidget*>("projectDock")) {
        d->show();
        d->raise();
    }
    return true;
}

bool SimWindow::importProject(const QString& script, const QString& projectFile) {
    const auto text = lvd::tools::readTextFile(script.toStdString());
    if (!text) {
        lastError_ = tr("cannot read %1").arg(script);
        return false;
    }
    lvd::tools::proj::ImportOptions io;
    io.scriptDir = QFileInfo(script).absolutePath().toStdString();
    io.projectDir = QFileInfo(projectFile).absolutePath().toStdString();
    io.iniText =
        lvd::tools::readTextFile(QDir(QFileInfo(script).absolutePath()).filePath("lvdsim.ini").toStdString())
            .value_or(lvd::tools::readTextFile(env_.iniPath).value_or(""));
    io.readFile = lvd::tools::readTextFile;
    lvd::tools::proj::ImportResult r = lvd::tools::proj::importDoScript(*text, io);
    for (const std::string& w : r.warnings)
        transcript_->appendNote("** Warning: (import) " + QString::fromStdString(w));
    r.project.settings.name = QFileInfo(projectFile).completeBaseName().toStdString();
    std::string err;
    if (!lvd::tools::proj::saveProject(r.project, projectFile.toStdString(), &err)) {
        lastError_ = QString::fromStdString(err);
        return false;
    }
    transcript_->appendNote(tr("Imported %1: %2 libraries, %3 files, %4 tests")
                                .arg(QFileInfo(script).fileName())
                                .arg(r.project.libraries.size())
                                .arg(r.project.fileCount())
                                .arg(r.project.tests.size()));
    return openProject(projectFile);
}

bool SimWindow::simulateTest(const QString& test) {
    if (!project_->isOpen() || project_->project().findTest(test.toStdString()) == nullptr) {
        report(tr("no test '%1' in the project").arg(test));
        return false;
    }
    pendingTest_ = test;
    if (!project_->compile(lvd::tools::build::Mode::OutOfDate)) {
        pendingTest_.clear();
        report(project_->lastError());
        return false;
    }
    return true;
}

void SimWindow::showBottomDock(const QString& name) {
    auto* dock = findChild<QDockWidget*>(name + "Dock");
    auto* transcriptDock = findChild<QDockWidget*>("transcriptDock");
    if (dock == nullptr)
        return;
    if (transcriptDock != nullptr && !transcriptDock->isHidden() && !transcriptDock->isFloating() &&
        !dock->isFloating() && !tabifiedDockWidgets(transcriptDock).contains(dock))
        tabifyDockWidget(transcriptDock, dock);
    dock->show();
    dock->raise();
}

bool SimWindow::maybeSaveProject() {
    if (!project_->isOpen() || !project_->modified())
        return true;
    if (!dialogs_)
        return true; // automated runs never write the project behind the user's back
    const auto b = QMessageBox::question(this, tr("lvdsim"),
                                         tr("Save the changes of project %1?").arg(project_->displayName()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (b == QMessageBox::Cancel)
        return false;
    if (b == QMessageBox::Save && !project_->save()) {
        report(project_->lastError());
        return false;
    }
    return true;
}

void SimWindow::newProjectDialog() {
    if (!dialogs_ || !maybeSaveProject())
        return;
    const QString f = QFileDialog::getSaveFileName(
        this, tr("New project"), QDir::currentPath() + "/project.lvdproj", tr("lvdsim projects (*.lvdproj)"));
    if (f.isEmpty())
        return;
    if (!project_->create(f)) {
        report(project_->lastError());
        return;
    }
    openProject(project_->path());
}

void SimWindow::openProjectDialog() {
    if (!dialogs_ || !maybeSaveProject())
        return;
    const QString f = QFileDialog::getOpenFileName(this, tr("Open project"), QDir::currentPath(),
                                                   tr("lvdsim projects (*.lvdproj);;All files (*)"));
    if (!f.isEmpty() && !openProject(f))
        report(lastError_);
}

void SimWindow::importProjectDialog() {
    if (!dialogs_ || !maybeSaveProject())
        return;
    const QString script = QFileDialog::getOpenFileName(
        this, tr("Import compile script"), QDir::currentPath(), tr("Do scripts (*.do *.tcl);;All files (*)"));
    if (script.isEmpty())
        return;
    const QFileInfo si(script);
    const QString f = QFileDialog::getSaveFileName(
        this, tr("Save the new project as"), si.absolutePath() + "/" + si.completeBaseName() + ".lvdproj",
        tr("lvdsim projects (*.lvdproj)"));
    if (f.isEmpty())
        return;
    if (!importProject(script, f))
        report(lastError_);
}

void SimWindow::updateProjectTitle() {
    if (auto* d = findChild<QDockWidget*>("projectDock"))
        d->setWindowTitle(
            project_->isOpen()
                ? tr("Project - %1%2").arg(project_->displayName(), project_->modified() ? "*" : "")
                : tr("Project"));
}

} // namespace lvd::gui
