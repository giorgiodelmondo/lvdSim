// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sim_window.hpp"
#include "app/portable_mode.hpp"

#include "app/breakpoint_pane.hpp"
#include "app/compile_libs_dialog.hpp"
#include "app/library_pane.hpp"
#include "app/objects_pane.hpp"
#include "app/processes_pane.hpp"
#include "app/source_pane.hpp"
#include "app/structure_pane.hpp"
#include "app/transcript_widget.hpp"
#include "rhi/rhi_wave_canvas.hpp"
#include "wave/icons.hpp"
#include "wave/render_policy.hpp"
#include "wave/view_state.hpp"
#include "wave/wave_toolbar.hpp"
#include "wave/wave_widget.hpp"

#include <lvdwave/wave_commands.hpp>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStatusBar>
#include <QThread>
#include <QToolBar>

#include <algorithm>
#include <libraries.hpp>

namespace lvd::gui {

using namespace lvd::wave;

namespace {


std::vector<std::string> toStd(const QStringList& l) {
    std::vector<std::string> v;
    for (const QString& s : l)
        v.push_back(s.toStdString());
    return v;
}

QStringList toQt(const std::vector<std::string>& v) {
    QStringList l;
    for (const std::string& s : v)
        l << QString::fromStdString(s);
    return l;
}

} // namespace

SimWindow::SimWindow(lvd::tools::Environment environment, QString settingsFile, QWidget* parent)
    : QMainWindow(parent)
    , env_(std::move(environment))
    , settingsFile_(std::move(settingsFile)) {
    buildUi();
    connect(structure_, &StructurePane::scopeSelected, objects_, &ObjectsPane::setScope);
    connect(library_, &LibraryPane::simulateRequested, this,
            [this](const QString& unit) { startSimulation({unit}); });
    connect(library_, &LibraryPane::refreshRequested, this, [this] { refreshLibraries(); });
    restoreSession();
    refreshLibraries();
    if (const PortableDecision pd = portableDecision(); !pd.note.isEmpty())
        transcript_->appendNote(pd.note);

    pollTimer_.setInterval(60);
    connect(&pollTimer_, &QTimer::timeout, this, [this] { pollNow(); });

    connect(&session_, &NvcSession::output, transcript_, &TranscriptWidget::append);
    connect(transcript_, &TranscriptWidget::timeActivated, wave_, &WaveWidget::showTimeFs);
    connect(transcript_, &TranscriptWidget::reportLogged, wave_,
            [this](qint64 fs, int severity, const QString& text) {
                wave_->addEventMarker({fs, static_cast<EventSeverity>(severity), text});
            });
    connect(&session_, &NvcSession::notice, this, &SimWindow::onNotice);
    connect(&session_, &NvcSession::busyChanged, this, &SimWindow::onBusyChanged);
    connect(&session_, &NvcSession::reply, this, &SimWindow::onReply);
    connect(processes_, &ProcessesPane::refreshRequested, this, [this] { refreshProcesses(); });
    connect(processes_, &ProcessesPane::openRequested, this, [this](const QString& f, int l) {
        if (f.isEmpty())
            transcript_->appendNote(tr("** The source file of this process is not known."));
        else
            showSource(f, l);
    });
    connect(processes_, &ProcessesPane::processSelected, this, &SimWindow::onProcessSelected);
    connect(breakpoints_, &BreakpointPane::addRequested, this,
            [this](const QString& t, const QString& u) { addBreakpoint(t, u); });
    connect(breakpoints_, &BreakpointPane::deleteRequested, this, [this](int id) { deleteBreakpoint(id); });
    connect(breakpoints_, &BreakpointPane::deleteAllRequested, this, [this] { deleteAllBreakpoints(); });
    connect(&session_, &NvcSession::finished, this, &SimWindow::onFinished);
    connect(&session_, &NvcSession::failed, this, [this](const QString& m) { report(m); });
    connect(transcript_, &TranscriptWidget::commandEntered, this, &SimWindow::onCommandEntered);
    connect(objects_, &ObjectsPane::addToWave, this, [this](const QStringList& paths) {
        AddWaveRequest req;
        for (const QString& p : paths) {
            QString pattern = p;
            pattern.replace('.', '/');
            req.patterns.push_back(("/" + pattern).toStdString());
        }
        wave_->addWaveSignals(req);
    });
    updateStatus();
}

SimWindow::~SimWindow() {
    session_.stop(1000);
    cleanupTemp();
}

void SimWindow::buildUi() {
    setWindowTitle(tr("lvdsim"));
    resize(1400, 800);

    wave_ = new WaveWidget(this);
    setCentralWidget(wave_);

    objects_ = new ObjectsPane(this);
    auto* objectsDock = new QDockWidget(tr("Objects"), this);
    objectsDock->setObjectName("objectsDock");
    objectsDock->setWidget(objects_);

    library_ = new LibraryPane(this);
    auto* libraryDock = new QDockWidget(tr("Library"), this);
    libraryDock->setObjectName("libraryDock");
    libraryDock->setWidget(library_);
    structure_ = new StructurePane(this);
    auto* structureDock = new QDockWidget(tr("Sim"), this);
    structureDock->setObjectName("structureDock");
    structureDock->setWidget(structure_);

    transcript_ = new TranscriptWidget(this);
    auto* transcriptDock = new QDockWidget(tr("Transcript"), this);
    transcriptDock->setObjectName("transcriptDock");
    transcriptDock->setWidget(transcript_);

    processes_ = new ProcessesPane(this);
    auto* processesDock = new QDockWidget(tr("Processes"), this);
    processesDock->setObjectName("processesDock");
    processesDock->setWidget(processes_);
    breakpoints_ = new BreakpointPane(this);
    auto* breakpointsDock = new QDockWidget(tr("Breakpoints"), this);
    breakpointsDock->setObjectName("breakpointsDock");
    breakpointsDock->setWidget(breakpoints_);

    source_ = new SourcePane(this);
    auto* sourceDock = new QDockWidget(tr("Source"), this);
    sourceDock->setObjectName("sourceDock");
    sourceDock->setWidget(source_);


    applyDefaultLayout();

    // Simulation controls
    auto* sim = addToolBar(tr("Simulation"));
    sim->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sim->setIconSize(QSize(20, 20));
    sim->setObjectName("simToolBar");
    sim->setMovable(false);
    runAction_ = sim->addAction(makeIcon(Icon::Run), tr("Run"));
    runAction_->setObjectName("run");
    runAction_->setToolTip(tr("Run for the time in the box"));
    runLength_ = new QLineEdit("100 ns", this);
    runLength_->setObjectName("runLength");
    runLength_->setFixedWidth(90);
    runLength_->setToolTip(tr("Run length, e.g. 100 ns or 1.5 us"));
    sim->addWidget(runLength_);
    runAllAction_ = sim->addAction(makeIcon(Icon::RunAll), tr("Run -all"));
    runAllAction_->setObjectName("runAll");
    runAllAction_->setToolTip(tr("Run until the design stops on its own"));
    restartAction_ = sim->addAction(makeIcon(Icon::Restart), tr("Restart"));
    restartAction_->setObjectName("restart");
    restartAction_->setToolTip(tr("Start the simulation again from time zero; the wave rows are kept"));
    stopAction_ = sim->addAction(makeIcon(Icon::Stop), tr("Stop"));
    stopAction_->setObjectName("stop");
    stopAction_->setToolTip(tr("End the simulation"));
    sim->addSeparator();
    stepDeltaAction_ = sim->addAction(makeIcon(Icon::StepDelta), tr("Step delta"));
    stepDeltaAction_->setObjectName("stepDelta");
    stepDeltaAction_->setToolTip(tr("Run one simulation cycle (a delta cycle or the first of a time step)"));
    stepTimeAction_ = sim->addAction(makeIcon(Icon::StepTime), tr("Step time"));
    stepTimeAction_->setObjectName("stepTime");
    stepTimeAction_->setToolTip(tr("Finish the current time step, or run the next one"));
    connect(stepDeltaAction_, &QAction::triggered, this, [this] { stepDelta(); });
    connect(stepTimeAction_, &QAction::triggered, this, [this] { stepTime(); });
    connect(runAction_, &QAction::triggered, this, [this] { sendCommand("run " + runLengthText()); });
    connect(runLength_, &QLineEdit::returnPressed, this, [this] { sendCommand("run " + runLengthText()); });
    connect(runAllAction_, &QAction::triggered, this, [this] { sendCommand("run -all"); });
    connect(restartAction_, &QAction::triggered, this, [this] { restartSimulation(); });
    connect(stopAction_, &QAction::triggered, this, [this] { stopSimulation(); });

    addToolBarBreak();
    addToolBar(new WaveToolBar(wave_, this));

    // Menus
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Save wave layout..."), this, [this] { saveLayout(); });
    file->addAction(tr("&Load wave layout..."), this, [this] { loadLayout(); });
    file->addAction(tr("Save wave &format (.do)..."), this, [this] { saveWaveFormatDialog(); });
    file->addAction(tr("Load wave f&ormat (.do)..."), this, [this] { loadWaveFormatDialog(); });
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    // The Wave menu: arranging the rows (the same actions as the wave window's context menu).
    QMenu* waveMenu = menuBar()->addMenu(tr("&Wave"));
    waveMenu->setObjectName("waveMenu");
    connect(waveMenu, &QMenu::aboutToShow, this, [this, waveMenu] {
        qDeleteAll(waveMenu->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly));
        waveMenu->clear();
        wave_->populateArrangeMenu(waveMenu);
    });
    connect(wave_, &WaveWidget::structureChanged, this, [this] { waveLayoutModified_ = true; });
    connect(wave_, &WaveWidget::editRejected, this, [this](const QString& why) { statusBar()->showMessage(why, 8000); });

    QMenu* simMenu = menuBar()->addMenu(tr("&Simulation"));
    simMenu->addAction(tr("&Start simulation..."), this, [this] { startDialog(); });
    simMenu->addAction(runAction_);
    simMenu->addAction(runAllAction_);
    simMenu->addAction(restartAction_);
    simMenu->addAction(stopAction_);
    simMenu->addSeparator();
    simMenu->addAction(stepDeltaAction_);
    simMenu->addAction(stepTimeAction_);
    simMenu->addSeparator();
    simMenu->addAction(tr("Add &breakpoint..."), this, [this, breakpointsDock] {
        breakpointsDock->show();
        breakpointsDock->raise();
        breakpoints_->timeEdit()->setFocus();
    });
    simMenu->addAction(tr("&Delete all breakpoints"), this, [this] { deleteAllBreakpoints(); });
    simMenu->addAction(tr("Refresh &processes"), this, [this] { refreshProcesses(); });

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("&Compile libraries..."), this, [this] { showCompileLibraries(); });
    tools->addAction(tr("&Library folders..."), this, [this] { showCompileLibraries(); });

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(libraryDock->toggleViewAction());
    view->addAction(structureDock->toggleViewAction());
    view->addAction(objectsDock->toggleViewAction());
    view->addAction(transcriptDock->toggleViewAction());
    view->addAction(processesDock->toggleViewAction());
    view->addAction(breakpointsDock->toggleViewAction());
    view->addAction(sourceDock->toggleViewAction());
    view->addSeparator();
    QAction* resetLayout = view->addAction(tr("&Reset layout"), this, [this] { applyDefaultLayout(); });
    resetLayout->setObjectName("resetLayout");
    view->addSeparator();
    view->addAction(tr("Zoom &full"), this, [this] { wave_->zoomFull(); });
    view->addSeparator();
    QMenu* renderer = view->addMenu(tr("&Waveform renderer"));
    renderer->setObjectName("rendererMenu");
    auto* rendererGroup = new QActionGroup(this);
    rendererAutoAction_ = renderer->addAction(tr("&Automatic (GPU, QPainter fallback)"));
    rendererCpuAction_ = renderer->addAction(tr("&QPainter only"));
    rendererGpuAction_ = renderer->addAction(tr("&GPU only (QPainter if it fails)"));
    rendererAutoAction_->setObjectName("rendererAuto");
    rendererCpuAction_->setObjectName("rendererCpu");
    rendererGpuAction_->setObjectName("rendererGpu");
    for (QAction* a : {rendererAutoAction_, rendererCpuAction_, rendererGpuAction_}) {
        a->setCheckable(true);
        rendererGroup->addAction(a);
    }
    connect(rendererAutoAction_, &QAction::triggered, this, [this] { setRenderPolicy(RenderPolicy::Auto, true); });
    connect(rendererCpuAction_, &QAction::triggered, this, [this] { setRenderPolicy(RenderPolicy::Cpu, true); });
    connect(rendererGpuAction_, &QAction::triggered, this, [this] { setRenderPolicy(RenderPolicy::Gpu, true); });
    rendererLabel_ = new QLabel(this);
    rendererLabel_->setObjectName("rendererLabel");
    // The check mark follows what is really used, so a fallback is visible.
    connect(wave_, &WaveWidget::renderBackendChanged, this, [this] { syncRenderer(); });
    {
        auto s = openGuiSettings(settingsFile_);
        const RenderDecision d = currentRenderDecision(s->value(kRenderSettingKey).toString());
        applyRenderDecision(d);
    }

    stateLabel_ = new QLabel(this);
    timeLabel_ = new QLabel(this);
    stopLabel_ = new QLabel(this);
    stopLabel_->setObjectName("stopLabel");
    statusBar()->addWidget(stateLabel_, 1);
    statusBar()->addPermanentWidget(rendererLabel_);
    statusBar()->addPermanentWidget(stopLabel_);
    statusBar()->addPermanentWidget(timeLabel_);
}

QString SimWindow::runLengthText() const {
    const QString t = runLength_->text().trimmed();
    return t.isEmpty() ? QStringLiteral("100 ns") : t;
}

void SimWindow::setRunLength(const QString& text) {
    runLength_->setText(text);
}

bool SimWindow::simulationRunning() const {
    return session_.running();
}

void SimWindow::report(const QString& error) {
    lastError_ = error;
    transcript_->appendNote("** Error: " + error);
    if (dialogs_)
        QMessageBox::warning(this, tr("lvdsim"), error);
}

void SimWindow::cleanupTemp() {
    for (const QString& f : tempFiles_)
        QFile::remove(f);
    tempFiles_.clear();
    if (!liveFile_.isEmpty())
        QFile::remove(liveFile_);
    if (!tempDir_.isEmpty())
        QDir(tempDir_).rmdir(".");
}

bool SimWindow::startSimulation(const QStringList& vsimArgs) {
    stopSimulation();
    cleanupTemp();
    lastError_.clear();
    lastArgs_ = vsimArgs;
    finished_ = false;

    tempDir_ = QString::fromStdString(env_.ctx.tempDir);
    QDir().mkpath(tempDir_);
    liveFile_ = tempDir_ + "/live.lvw";
    QFile::remove(liveFile_);

    QStringList folderArgs;
    lvd::tools::Context ctx = planContext(&folderArgs, vsimArgs);
    ctx.guiWaveFile = liveFile_.toStdString();
    const lvd::tools::Plan plan = lvd::tools::planVsim(toStd(folderArgs + vsimArgs), ctx);
    for (const std::string& w : plan.warnings)
        transcript_->appendNote("** Warning: " + QString::fromStdString(w));
    if (!plan.ok()) {
        report(QString::fromStdString(plan.error));
        return false;
    }
    if (plan.calls.empty()) {
        report(tr("nothing to start"));
        return false;
    }

    for (const lvd::tools::TempFile& f : plan.files) {
        QDir().mkpath(QFileInfo(QString::fromStdString(f.path)).absolutePath());
        QFile out(QString::fromStdString(f.path));
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            out.write(QByteArray::fromStdString(f.contents));
        tempFiles_ << QString::fromStdString(f.path);
    }

    wave_->clearEventMarkers(); // a new run (also a restart) has new reports
    store_ = std::make_shared<WaveStore>();
    reader_ = std::make_unique<LiveReader>(liveFile_.toStdString());
    storeAttached_ = false;
    resetDebugState();
    dirty_ = true; // the startup script may have added breakpoints or run
    objects_->setStore(nullptr);
    structure_->setStore(nullptr);

    NvcSession::Config cfg;
    cfg.nvc = QString::fromStdString(env_.nvc);
    cfg.args = toQt(plan.calls.front().args);
    if (!env_.nvcDllDir.empty())
        cfg.pathPrepend << QString::fromStdString(env_.nvcDllDir);
    cfg.startupScripts = toQt(plan.startupScripts);

    transcript_->setInputEnabled(true);
    if (!session_.start(cfg))
        return false;

    setWindowTitle(tr("lvdsim - %1").arg(vsimArgs.isEmpty() ? QString() : vsimArgs.last()));
    pollTimer_.start();
    previousArgs_ = vsimArgs;
    saveSession();
    refreshLibraries();
    // Show the design hierarchy (the Library tab was in front before a design existed).
    if (auto* sd = findChild<QDockWidget*>("structureDock"); sd && !sd->isHidden())
        sd->raise();
    emit simulationStarted();
    updateStatus();
    return true;
}

void SimWindow::restartSimulation() {
    if (lastArgs_.isEmpty()) {
        transcript_->appendNote(tr("** Nothing to restart: start a simulation first (Simulation > Start)."));
        return;
    }
    transcript_->appendNote(tr("---- restart ----"));
    // As in the classic flow, a restart starts over from time zero and keeps the wave rows. The startup
    // script is not run again: it would add every row a second time.
    QStringList args;
    for (int i = 0; i < lastArgs_.size(); ++i) {
        if (lastArgs_[i] == "-do" && i + 1 < lastArgs_.size()) {
            ++i;
            continue;
        }
        args << lastArgs_[i];
    }
    const QStringList keep =
        lastArgs_; // startSimulation replaces lastArgs_; a second restart must see the original
    startSimulation(args);
    lastArgs_ = keep;
}

void SimWindow::stopSimulation() {
    if (!session_.running())
        return;
    session_.stop();
}

void SimWindow::sendCommand(const QString& command) {
    if (!session_.running()) {
        transcript_->appendNote(tr("** No simulation is running (Simulation > Start)."));
        return;
    }
    const QStringList words = command.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const QString first = words.value(0);
    if ((first == "run" || first == "step") && !words.contains("-json")) {
        QStringList rewritten = words;
        rewritten.insert(1, "-json");
        request(rewritten.join(' '), [this](const QString& json) { onRunReply(json); });
        return;
    }
    if (first == "do" || first == "source" || first == "breakpoint")
        dirty_ = true;
    session_.send(command);
}

int SimWindow::request(const QString& command, std::function<void(const QString&)> handler) {
    const int index = session_.send(command);
    if (index >= 0 && handler)
        pending_.insert(index, std::move(handler));
    return index;
}

void SimWindow::onReply(int commandIndex, const QString& json) {
    auto it = pending_.find(commandIndex);
    if (it == pending_.end()) {
        // Typed by the user (examine -json ...) or printed by a script: show it as the shell printed it.
        transcript_->append(json + "\n");
        return;
    }
    const auto handler = std::move(it.value());
    pending_.erase(it);
    handler(json);
}

void SimWindow::onBusyChanged(bool busy) {
    if (!busy) {
        pending_.clear(); // commands that failed have no reply, and never will
        if (dirty_ && session_.running()) {
            dirty_ = false;
            refreshProcesses();
            refreshBreakpoints();
        }
    }
    updateStatus();
}

void SimWindow::resetDebugState() {
    pending_.clear();
    bpList_.clear();
    breakpoints_->setBreakpoints({});
    processes_->setProcesses({});
    source_->setCurrentLine(0);
    source_->setReadyLines({});
    haveStop_ = false;
    stopText_.clear();
    lastStatus_ = RunStatus::Paused;
}

QString SimWindow::stopText() const {
    return stopText_;
}

void SimWindow::onRunReply(const QString& json) {
    const auto r = parseRunReply(json);
    if (!r.ok()) {
        transcript_->appendNote(tr("** Unreadable reply from the simulator (%1): %2")
                                    .arg(QString::fromStdString(r.error()), json));
        return;
    }
    const RunReply& run = r.value();
    lastStatus_ = run.status;
    haveStop_ = true;
    const QString now = formatFemtoseconds(run.nowFs);
    QString note;
    switch (run.status) {
    case RunStatus::Paused:
        stopText_ = tr("Paused at %1").arg(now);
        note = tr("# Paused at %1, delta %2").arg(now).arg(run.delta);
        break;
    case RunStatus::Finished:
        stopText_ = tr("Finished at %1").arg(now);
        note = tr("# Simulation finished at %1, delta %2").arg(now).arg(run.delta);
        break;
    case RunStatus::Breakpoint: {
        const QString at = formatFemtoseconds(run.breakpoint->timeFs);
        stopText_ = tr("Breakpoint %1 at %2").arg(run.breakpoint->id).arg(at);
        note = tr("# Breakpoint %1 hit at %2 (now %3, delta %4)")
                   .arg(run.breakpoint->id)
                   .arg(at, now)
                   .arg(run.delta);
        QVector<BreakpointInfo> rest;
        for (const BreakpointInfo& b : bpList_)
            if (b.id != run.breakpoint->id)
                rest.push_back(b);
        setBreakpointList(rest); // a time breakpoint is deleted by the kernel when it is reached
        break;
    }
    }
    transcript_->appendNote(note);
    updateStatus();
    refreshProcesses();
    emit stopped(run);
}

void SimWindow::refreshProcesses() {
    request("processes -json", [this](const QString& json) {
        const auto r = parseProcessesReply(json);
        if (!r.ok()) {
            transcript_->appendNote(
                tr("** Unreadable process list (%1): %2").arg(QString::fromStdString(r.error()), json));
            return;
        }
        processes_->setProcesses(r.value());
        if (!source_->filePath().isEmpty())
            source_->setReadyLines(processes_->readyLines(source_->filePath()));
    });
}

void SimWindow::refreshBreakpoints() {
    request("breakpoint list -json", [this](const QString& json) {
        const auto r = parseBreakpointList(json);
        if (!r.ok()) {
            transcript_->appendNote(
                tr("** Unreadable breakpoint list (%1): %2").arg(QString::fromStdString(r.error()), json));
            return;
        }
        setBreakpointList(r.value());
    });
}

void SimWindow::setBreakpointList(QVector<BreakpointInfo> list) {
    std::stable_sort(list.begin(), list.end(), [](const BreakpointInfo& a, const BreakpointInfo& b) {
        return a.timeFs != b.timeFs ? a.timeFs < b.timeFs : a.id < b.id;
    });
    bpList_ = list;
    breakpoints_->setBreakpoints(list);
}

void SimWindow::addBreakpoint(const QString& time, const QString& unit) {
    if (!session_.running()) {
        transcript_->appendNote(tr("** No simulation is running (Simulation > Start)."));
        return;
    }
    // The kernel takes whole numbers: 0.5 us is sent as 500 ns.
    const auto fs = timeToFemtoseconds(time, unit);
    if (!fs || *fs <= 0) {
        transcript_->appendNote(
            tr("** Breakpoint time \"%1 %2\" must be a positive number of whole femtoseconds.")
                .arg(time, unit));
        return;
    }
    request(QStringLiteral("breakpoint add -json %1").arg(formatFemtoseconds(*fs)),
            [this](const QString& json) {
                const auto r = parseBreakpointAdded(json);
                if (!r.ok()) {
                    transcript_->appendNote(
                        tr("** Unreadable reply (%1): %2").arg(QString::fromStdString(r.error()), json));
                    return;
                }
                QVector<BreakpointInfo> l = bpList_;
                l.push_back(r.value());
                setBreakpointList(l);
            });
}

void SimWindow::deleteBreakpoint(int id) {
    request(QStringLiteral("breakpoint delete -json %1").arg(id), [this, id](const QString& json) {
        if (!parseDeleted(json).ok()) {
            transcript_->appendNote(tr("** Unreadable reply: %1").arg(json));
            return;
        }
        QVector<BreakpointInfo> l;
        for (const BreakpointInfo& b : bpList_)
            if (b.id != id)
                l.push_back(b);
        setBreakpointList(l);
    });
}

void SimWindow::deleteAllBreakpoints() {
    request("breakpoint delete -json all", [this](const QString& json) {
        if (!parseDeleted(json).ok()) {
            transcript_->appendNote(tr("** Unreadable reply: %1").arg(json));
            return;
        }
        setBreakpointList({});
    });
}

bool SimWindow::setDockVisible(const QString& name, bool visible) {
    QString n = name.toLower();
    if (n == "sim")
        n = "structure";
    QDockWidget* dock = findChild<QDockWidget*>(n + "Dock");
    if (!dock)
        return false;
    dock->setVisible(visible);
    if (visible)
        dock->raise();
    return true;
}

bool SimWindow::showSource(const QString& file, int line) {
    if (!source_->openFile(file, line)) {
        transcript_->appendNote("** " + source_->lastError());
        return false;
    }
    if (auto* dock = qobject_cast<QDockWidget*>(source_->parentWidget())) {
        dock->show();
        dock->raise();
    }
    return true;
}

void SimWindow::onProcessSelected(const QString& file, int line) {
    if (file.isEmpty() || line <= 0) {
        source_->setCurrentLine(0);
        return;
    }
    if (!source_->openFile(file, 0)) // a missing file is reported by the double-click, not by every selection
        return;
    source_->setReadyLines(processes_->readyLines(source_->filePath()));
    source_->setCurrentLine(line);
}

void SimWindow::onCommandEntered(const QString& command) {
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        sendCommand(command); // an empty line just brings the prompt back
        return;
    }

    const QStringList words = toQt(parseTclList(trimmed.toStdString()));
    const QString first = words.value(0);
    if (first == "restart") {
        restartSimulation();
    } else if (first == "quit" || first == "exit") {
        close();
    } else if (first == "vlib" || first == "vmap" || first == "vcom" || first == "vlog") {
        transcript_->appendNote("> " + trimmed);
        runExternalTool(first, words.mid(1));
    } else {
        sendCommand(command);
    }
}

void SimWindow::runExternalTool(const QString& tool, const QStringList& args) {
    const QString dir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString exe = dir + "/" + tool + ".exe";
#else
    const QString exe = dir + "/" + tool;
#endif
    auto* p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    if (!env_.nvcDllDir.empty())
        e.insert("PATH", QString::fromStdString(env_.nvcDllDir) + QDir::listSeparator() + e.value("PATH"));
    p->setProcessEnvironment(e);
    connect(p, &QProcess::readyRead, this,
            [this, p] { transcript_->append(QString::fromUtf8(p->readAll()).replace("\r\n", "\n")); });
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus) {
        transcript_->append(QString::fromUtf8(p->readAll()).replace("\r\n", "\n"));
        transcript_->appendNote(code == 0 ? tr("(done)") : tr("(failed with exit code %1)").arg(code));
        refreshLibraries();
        p->deleteLater();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, tool](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            report(tr("cannot run %1: %2").arg(tool, p->errorString()));
            p->deleteLater();
        }
    });
    p->start(exe, args);
}

void SimWindow::onNotice(const QStringList& words) {
    // "add wave ..." etc. need the signal definitions, which are in the stream before the shell is ready
    pollNow();
    if (words.size() >= 2 && words[0] == "add" && words[1] == "wave") {
        const auto req = parseAddWave(toStd(words.mid(2)));
        if (!req.ok()) {
            transcript_->appendNote("** " + QString::fromStdString(req.error()));
            return;
        }
        if (wave_->store() == nullptr) {
            transcript_->appendNote(tr("** add wave: the design has no signal definitions yet"));
            return;
        }
        if (wave_->addWaveSignals(req.value()) == 0 && !req.value().divider)
            transcript_->appendNote(tr("** add wave: no signal matches"));
    } else if (words.size() >= 2 && words[0] == "delete" && words[1] == "wave") {
        wave_->clearWave();
    }
}

void SimWindow::pollNow() {
    if (!reader_ || !store_)
        return;
    const auto res = reader_->poll(*store_);
    if (!res.ok())
        return; // the simulator may not have created the file yet
    const PollResult& r = res.value();

    if (r.definitionsCompleted) {
        wave_->setStore(store_);
        objects_->setStore(store_.get());
        structure_->setStore(store_.get());
        refreshLibraries(); // vsim has elaborated the design by now
        storeAttached_ = true;
    }
    if (storeAttached_ && r.changed())
        wave_->dataChanged();
    updateStatus();
}

bool SimWindow::waitUntilIdle(int timeoutMs) {
    if (!session_.running() && finished_) {
        pollNow();
        return true;
    }
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (session_.running() && !session_.busy()) {
            // Whatever the simulator wrote before its prompt is in the file; read it before returning.
            pollNow();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            pollNow();
            return true;
        }
        if (!session_.running() && finished_) {
            pollNow();
            return true;
        }
        QThread::msleep(5);
    }
    return false;
}

void SimWindow::onFinished(int exitCode) {
    finished_ = true;
    pollNow();
    pollTimer_.stop();
    transcript_->appendNote(tr("---- simulation ended (exit code %1) ----").arg(exitCode));
    transcript_->setInputEnabled(false);
    updateStatus();
    emit simulationFinished(exitCode);
}

void SimWindow::setRenderPolicy(RenderPolicy policy, bool persist) {
    if (persist) {
        auto s = openGuiSettings(settingsFile_);
        s->setValue(kRenderSettingKey, renderPolicyName(policy));
        s->sync();
    }
    // The menu choice is explicit: it beats LVDSIM_RENDERER for this session.
    applyRenderDecision(resolveRenderPolicy(renderPolicyName(policy), {}, QGuiApplication::platformName()));
}

void SimWindow::applyRenderDecision(const RenderDecision& d) {
    renderPolicy_ = d.policy;
    startupReason_ = d.cpuReason;
    wave_->setRenderBackend(d.requestGpu ? WaveWidget::Backend::Gpu : WaveWidget::Backend::Cpu);
    syncRenderer();
}

void SimWindow::syncRenderer() {
    const bool gpu = wave_->renderBackend() == WaveWidget::Backend::Gpu;
    const QString reason = !startupReason_.isEmpty() ? startupReason_ : wave_->fallbackReason();
    const bool fellBack = !gpu && renderPolicy_ != RenderPolicy::Cpu && !reason.isEmpty();
    QAction* policyAction = renderPolicy_ == RenderPolicy::Gpu    ? rendererGpuAction_
                            : renderPolicy_ == RenderPolicy::Cpu ? rendererCpuAction_
                                                                 : rendererAutoAction_;
    // Checked: the choice, or QPainter when that is what is really drawing.
    (gpu || renderPolicy_ == RenderPolicy::Cpu ? policyAction : rendererCpuAction_)->setChecked(true);
    const QString tip = fellBack ? tr("GPU renderer not available (%1), using QPainter").arg(reason) : QString();
    rendererAutoAction_->setToolTip(tip);
    rendererGpuAction_->setToolTip(tip);
    QString text;
    if (gpu) {
        const QString name = wave_->gpuCanvas() != nullptr ? wave_->gpuCanvas()->backendName() : QString();
        text = name.isEmpty() ? tr("Renderer: GPU") : tr("Renderer: GPU (%1)").arg(name);
    } else if (fellBack) {
        text = tr("Renderer: QPainter (GPU not available: %1)").arg(reason);
    } else {
        text = tr("Renderer: QPainter");
    }
    rendererLabel_->setText(text);
    rendererLabel_->setToolTip(tip);
    // One line in the transcript the first time the GPU was wanted but cannot be used. An automatic choice on
    // a headless platform is expected, so it stays quiet there.
    const bool quiet = renderPolicy_ == RenderPolicy::Auto && platformHasNoGpu(QGuiApplication::platformName());
    if (fellBack && !fallbackNoted_ && !quiet) {
        fallbackNoted_ = true;
        transcript_->appendNote(tr("GPU renderer not available (%1), using QPainter").arg(reason));
    }
}

void SimWindow::updateStatus() {
    QString state;
    if (session_.running())
        state = session_.busy() ? tr("Running...") : tr("Ready");
    else if (finished_)
        state = tr("Simulation ended");
    else
        state = tr("No simulation");
    stateLabel_->setText(state);

    if (store_ && storeAttached_) {
        timeLabel_->setText(tr("Now: %1").arg(QString::fromStdString(
            formatTime(static_cast<double>(store_->endTime()), store_->timescaleExponent()))));
    } else {
        timeLabel_->setText({});
    }
    const bool live = session_.running();
    runAction_->setEnabled(live);
    runAllAction_->setEnabled(live);
    stopAction_->setEnabled(live);
    restartAction_->setEnabled(!lastArgs_.isEmpty());
    stepDeltaAction_->setEnabled(live);
    stepTimeAction_->setEnabled(live);
    stopLabel_->setText(stopText_);
}

void SimWindow::startDialog() {
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, tr("Start simulation"),
        tr("vsim options and design unit, e.g.  -do \"add wave -r /*; run 100 ns\" work.top"),
        QLineEdit::Normal, (lastArgs_.isEmpty() ? previousArgs_ : lastArgs_).join(' '), &ok);
    if (!ok || text.trimmed().isEmpty())
        return;
    startSimulation(toQt(parseTclList(text.toStdString())));
}

void SimWindow::saveLayout() {
    const QString p =
        QFileDialog::getSaveFileName(this, tr("Save wave layout"), {}, tr("Layouts (*.lvdlayout)"));
    if (p.isEmpty())
        return;
    QFile f(p);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(wave_->displayModel().serialize().c_str());
        waveLayoutModified_ = false;
    }
}

void SimWindow::loadLayout() {
    const QString p =
        QFileDialog::getOpenFileName(this, tr("Load wave layout"), {}, tr("Layouts (*.lvdlayout)"));
    if (p.isEmpty())
        return;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    auto parsed = DisplayModel::parse(f.readAll().toStdString());
    if (!parsed.ok()) {
        report(QString::fromStdString(parsed.error()));
        return;
    }
    wave_->setDisplayModel(std::move(parsed).value());
    waveLayoutModified_ = false;
}

void SimWindow::closeEvent(QCloseEvent* e) {
    saveSession();
    pollTimer_.stop();
    session_.stop();
    QMainWindow::closeEvent(e);
}

// ---------------------------------------------------------------------------------------------
// Libraries, session, wave format

void SimWindow::refreshLibraries() {
    library_->setLibraries(lvd::tools::scanLibraries(planContext(), "."));
}

lvd::tools::Context SimWindow::planContext(QStringList* extraArgs, const QStringList& userArgs) const {
    lvd::tools::Context ctx = env_.ctx;
    QStringList skip = {"work"};
    for (int i = 0; i + 1 < userArgs.size(); ++i)
        if (userArgs[i] == "-L" || userArgs[i] == "-work" || userArgs[i] == "-lib")
            skip << userArgs[i + 1];
    std::vector<lvd::tools::FolderLibrary> libs;
    const std::vector<std::string> args =
        lvd::tools::searchPathArgs(toStd(libraryFolders_), toStd(skip), &libs);
    for (const lvd::tools::FolderLibrary& l : libs)
        if (!ctx.libs.pathOf(l.name))
            ctx.libs.set(l.name, l.path);
    if (extraArgs)
        *extraArgs = toQt(args);
    return ctx;
}

void SimWindow::setLibraryFolders(const QStringList& folders) {
    QStringList clean;
    for (const QString& f : folders) {
        const QString n = QDir::cleanPath(f.trimmed());
        if (!n.isEmpty() && !clean.contains(n, Qt::CaseInsensitive))
            clean << n;
    }
    libraryFolders_ = clean;
    auto s = openGuiSettings(settingsFile_);
    s->setValue("libraries/folders", libraryFolders_);
    s->sync();
    if (compileDialog_)
        compileDialog_->setLibraryFolders(libraryFolders_);
    refreshLibraries();
}

void SimWindow::addLibraryFolder(const QString& folder) {
    setLibraryFolders(libraryFolders_ + QStringList{folder});
}

void SimWindow::removeLibraryFolder(const QString& folder) {
    QStringList l;
    for (const QString& f : std::as_const(libraryFolders_))
        if (QDir::cleanPath(f).compare(QDir::cleanPath(folder), Qt::CaseInsensitive) != 0)
            l << f;
    setLibraryFolders(l);
}

CompileLibsDialog* SimWindow::showCompileLibraries() {
    if (!compileDialog_) {
        compileDialog_ = new CompileLibsDialog(CompileLibsDialog::configFor(env_, settingsFile_), this);
        compileDialog_->setAttribute(Qt::WA_DeleteOnClose);
        compileDialog_->setLibraryFolders(libraryFolders_);
        connect(compileDialog_, &CompileLibsDialog::librariesCompiled, this, [this](const QString& dir) {
            addLibraryFolder(dir);
            transcript_->appendNote(
                tr("Library folder added: %1 (simulations started from now on search it with -L)").arg(dir));
        });
        connect(compileDialog_, &CompileLibsDialog::libraryFolderRemoved, this,
                [this](const QString& f) { removeLibraryFolder(f); });
    }
    compileDialog_->show();
    compileDialog_->raise();
    compileDialog_->activateWindow();
    return compileDialog_;
}

void SimWindow::applyDefaultLayout() {
    auto dock = [this](const char* name) { return findChild<QDockWidget*>(name); };
    QDockWidget* objectsDock = dock("objectsDock");
    QDockWidget* libraryDock = dock("libraryDock");
    QDockWidget* structureDock = dock("structureDock");
    QDockWidget* transcriptDock = dock("transcriptDock");
    QDockWidget* processesDock = dock("processesDock");
    QDockWidget* breakpointsDock = dock("breakpointsDock");
    QDockWidget* sourceDock = dock("sourceDock");
    const QList<QDockWidget*> all = {libraryDock,    structureDock,   objectsDock, transcriptDock,
                                     processesDock, breakpointsDock, sourceDock};
    for (QDockWidget* d : all) {
        d->setFloating(false);
        removeDockWidget(d); // also hides it; makes this usable for a reset
    }

    // Left column: Library | Sim as tabs, Objects below them.
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);
    splitDockWidget(libraryDock, objectsDock, Qt::Vertical); // before tabify: splitting a tab group makes a tab
    tabifyDockWidget(libraryDock, structureDock);
    // Bottom: Transcript beside Processes | Breakpoints. Right: Source.
    addDockWidget(Qt::BottomDockWidgetArea, transcriptDock);
    splitDockWidget(transcriptDock, processesDock, Qt::Horizontal);
    tabifyDockWidget(processesDock, breakpointsDock);
    addDockWidget(Qt::RightDockWidgetArea, sourceDock);
    for (QDockWidget* d : all)
        d->show();

    // Library is in front until a design is loaded (startSimulation raises Sim).
    libraryDock->raise();
    processesDock->raise();

    resizeDocks({libraryDock, objectsDock}, {320, 240}, Qt::Vertical);
    resizeDocks({libraryDock}, {300}, Qt::Horizontal);
    resizeDocks({sourceDock}, {440}, Qt::Horizontal);
    resizeDocks({transcriptDock, processesDock}, {640, 560}, Qt::Horizontal);
    resizeDocks({transcriptDock}, {190}, Qt::Vertical);
}

// Bump when docks are added or removed: a saved layout of another version is ignored, not half applied.
constexpr int kLayoutVersion = 3; // 3: Objects below Library|Sim (2: Processes, Breakpoints and Source docks)

void SimWindow::saveSession() {
    auto s = openGuiSettings(settingsFile_);
    s->setValue("window/geometry", saveGeometry());
    s->setValue("window/state", saveState(kLayoutVersion));
    s->setValue("simulation/runLength", runLength_->text());
    s->setValue("simulation/arguments", previousArgs_);
    s->sync();
}

void SimWindow::restoreSession() {
    auto s = openGuiSettings(settingsFile_);
    if (s->contains("window/geometry"))
        restoreGeometry(s->value("window/geometry").toByteArray());
    if (s->contains("window/state"))
        restoreState(s->value("window/state").toByteArray(), kLayoutVersion);
    if (s->contains("simulation/runLength"))
        runLength_->setText(s->value("simulation/runLength").toString());
    previousArgs_ = s->value("simulation/arguments").toStringList();
    libraryFolders_ = s->value("libraries/folders").toStringList();
}

bool SimWindow::saveWaveFormat(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        report(tr("cannot write %1: %2").arg(path, f.errorString()));
        return false;
    }
    f.write(wave_->doScript().c_str());
    waveLayoutModified_ = false;
    return true;
}

bool SimWindow::loadWaveFormat(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        report(tr("cannot read %1: %2").arg(path, f.errorString()));
        return false;
    }
    if (wave_->store() == nullptr) {
        report(tr("no signals are known yet: start a simulation before loading a wave format"));
        return false;
    }
    const lvd::wave::ImportResult r = wave_->loadDoScript(f.readAll().toStdString());
    transcript_->appendNote(tr("Loaded %1 row(s) from %2").arg(r.rowsAdded).arg(path));
    for (const std::string& e : r.errors)
        transcript_->appendNote("** " + QString::fromStdString(e));
    return r.errors.empty();
}

void SimWindow::saveWaveFormatDialog() {
    const QString p =
        QFileDialog::getSaveFileName(this, tr("Save wave format"), {}, tr("Do files (*.do);;All files (*)"));
    if (!p.isEmpty())
        saveWaveFormat(p);
}

void SimWindow::loadWaveFormatDialog() {
    const QString p =
        QFileDialog::getOpenFileName(this, tr("Load wave format"), {}, tr("Do files (*.do);;All files (*)"));
    if (!p.isEmpty())
        loadWaveFormat(p);
}

} // namespace lvd::gui

namespace lvd::gui {
void SimWindow::settleLayout() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}
} // namespace lvd::gui

namespace lvd::gui {

bool SimWindow::applyWaveOp(const QString& op) {
    const QStringList a = QProcess::splitCommand(op);
    lastError_.clear();
    auto fail = [&](const QString& why) {
        lastError_ = why;
        report(why);
        return false;
    };
    if (a.isEmpty())
        return fail(tr("empty wave op"));
    auto rowByName = [&](const QString& name) {
        for (int i = 0; i < wave_->rowCount(); ++i)
            if (wave_->rowBit(i) < 0 && QString::fromStdString(wave_->rowName(i)) == name)
                return i;
        return -1;
    };
    auto selectNames = [&](int argIndex) {
        if (a.size() <= argIndex)
            return true;  // the current selection
        bool first = true;
        for (const QString& name : a[argIndex].split(',', Qt::SkipEmptyParts)) {
            const int r = rowByName(name);
            if (r < 0) {
                fail(tr("wave op: no row named %1").arg(name));
                return false;
            }
            wave_->selectRow(r, !first);
            first = false;
        }
        return true;
    };
    auto result = [&](bool ok) { return ok ? true : fail(wave_->lastEditError().isEmpty() ? tr("wave op changed nothing") : wave_->lastEditError()); };

    const QString& cmd = a[0];
    if (cmd == "select")
        return selectNames(1);
    if (cmd == "move-up" || cmd == "move-down") {
        if (!selectNames(1))
            return false;
        const int steps = a.size() > 2 ? std::max(1, a[2].toInt()) : 1;
        return result(wave_->moveSelectedRows(cmd == "move-up" ? -steps : steps));
    }
    if (cmd == "move-top" || cmd == "move-bottom")
        return selectNames(1) && result(wave_->moveSelectedRowsToEdge(cmd == "move-top"));
    if (cmd == "group")
        return selectNames(1) && result(wave_->groupSelected(a.size() > 2 ? a[2].toStdString() : std::string()));
    if (cmd == "ungroup")
        return selectNames(1) && result(wave_->ungroupSelected());
    if (cmd == "delete") {
        if (!selectNames(1))
            return false;
        const int before = wave_->rowCount();
        wave_->removeSelection();
        return wave_->rowCount() != before;
    }
    if (cmd == "rename" && a.size() >= 3) {
        const int r = rowByName(a[1]);
        if (r < 0)
            return fail(tr("wave op: no row named %1").arg(a[1]));
        return result(wave_->renameItem(wave_->itemIdAtRow(r), a[2].toStdString()));
    }
    if (cmd == "move-to-group" && a.size() >= 3) {
        if (!selectNames(1))
            return false;
        lvd::wave::ItemId parent = lvd::wave::kTopLevel;
        if (a[2] != "-") {
            const int g = rowByName(a[2]);
            if (g < 0)
                return fail(tr("wave op: no group named %1").arg(a[2]));
            parent = wave_->itemIdAtRow(g);
        }
        return result(wave_->moveSelectedToGroup(parent));
    }
    if ((cmd == "divider-above" || cmd == "divider-below") && a.size() >= 2) {
        if (!selectNames(1))
            return false;
        const std::string label = a.size() > 2 ? a[2].toStdString() : std::string();
        return result(cmd == "divider-above" ? wave_->insertDividerAbove(label) : wave_->insertDividerBelow(label));
    }
    if (cmd == "toggle" && a.size() >= 2) {
        const int r = rowByName(a[1]);
        if (r < 0)
            return fail(tr("wave op: no row named %1").arg(a[1]));
        wave_->toggleGroup(r);
        return true;
    }
    if (cmd == "undo")
        return result(wave_->undo());
    if (cmd == "redo")
        return result(wave_->redo());
    if (cmd == "edit" && a.size() >= 2) {
        const int r = rowByName(a[1]);
        if (r < 0)
            return fail(tr("wave op: no row named %1").arg(a[1]));
        wave_->selectRow(r);
        return wave_->beginRename(r);
    }
    if (cmd == "drag" && a.size() >= 4) {
        const int target = rowByName(a[2]);
        if (target < 0)
            return fail(tr("wave op: no row named %1").arg(a[2]));
        if (!selectNames(1) || !wave_->startRowDrag())
            return false;
        const int y = wave_->rowTop(wave_->rowOfItem(wave_->itemIdAtRow(target))) +
                      static_cast<int>(a[3].toDouble() * wave_->rowHeight());
        wave_->updateRowDrag(QPoint(60, y));
        return true;
    }
    return fail(tr("unknown wave op: %1").arg(op));
}

} // namespace lvd::gui
