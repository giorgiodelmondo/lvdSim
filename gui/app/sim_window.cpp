// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sim_window.hpp"

#include "app/breakpoint_pane.hpp"
#include "app/call_stack_pane.hpp"
#include "app/compile_libs_dialog.hpp"
#include "app/console_text.hpp"
#include "app/coverage_pane.hpp"
#include "app/library_pane.hpp"
#include "app/locals_pane.hpp"
#include "app/objects_pane.hpp"
#include "app/portable_mode.hpp"
#include "app/processes_pane.hpp"
#include "app/project_session.hpp"
#include "app/source_pane.hpp"
#include "app/structure_pane.hpp"
#include "app/transcript_widget.hpp"
#include "rhi/rhi_wave_canvas.hpp"
#include "wave/find_value_dialog.hpp"
#include "wave/icons.hpp"
#include "wave/list_window.hpp"
#include "wave/memory_window.hpp"
#include "wave/render_policy.hpp"
#include "wave/view_state.hpp"
#include "wave/wave_toolbar.hpp"
#include "wave/wave_widget.hpp"

#include <lvdwave/fst_loader.hpp>
#include <lvdwave/memory.hpp>
#include <lvdwave/wave_commands.hpp>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
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
    loadLineBreakpoints(previousArgs_);
    refreshLibraries();
    if (const PortableDecision pd = portableDecision(); !pd.note.isEmpty())
        transcript_->appendNote(pd.note);

    pollTimer_.setInterval(60);
    connect(&pollTimer_, &QTimer::timeout, this, [this] { pollNow(); });

    connect(&session_, &NvcSession::output, transcript_, &TranscriptWidget::append);
    connect(&session_, &NvcSession::output, this, &SimWindow::onShellOutput);
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
            jumpToSource({f, l, 0, {}});
    });
    connect(processes_, &ProcessesPane::processSelected, this, &SimWindow::onProcessSelected);
    connect(breakpoints_, &BreakpointPane::addRequested, this,
            [this](const QString& t, const QString& u) { addBreakpoint(t, u); });
    connect(breakpoints_, &BreakpointPane::deleteRequested, this, [this](int id) { deleteBreakpoint(id); });
    connect(breakpoints_, &BreakpointPane::deleteAllRequested, this, [this] { deleteAllBreakpoints(); });
    connect(breakpoints_, &BreakpointPane::lineEnableRequested, this,
            [this](const QString& f, int l, bool on) { setLineBreakpointEnabled(f, l, on); });
    connect(breakpoints_, &BreakpointPane::lineDeleteRequested, this,
            [this](const QString& f, int l) { deleteLineBreakpoint(f, l); });
    connect(breakpoints_, &BreakpointPane::lineActivated, this,
            [this](const QString& f, int l) { showSource(f, l); });
    connect(source_, &SourcePane::breakpointToggleRequested, this,
            [this](const QString& f, int l) { toggleLineBreakpoint(f, l); });
    connect(stack_, &CallStackPane::frameSelected, this, [this](int level) { selectFrame(level); });
    connect(&session_, &NvcSession::finished, this, &SimWindow::onFinished);
    connect(&session_, &NvcSession::promptShown, this, [this] {
        if (!locatorProbed_) {
            locatorProbed_ = true;
            probeDeclarations();
        }
        if (!libraryStampPending_)
            return;
        libraryStampPending_ = false;
        stampLibraries(sessionArgs_);
    });
    connect(&session_, &NvcSession::failed, this, [this](const QString& m) { report(m); });
    session_.setWaitPolicy(
        [this](NvcSession::Unresponsive what, int waitedMs) { return askWaitOrKill(what, waitedMs); });
    connect(transcript_, &TranscriptWidget::commandEntered, this, &SimWindow::onCommandEntered);
    {
        // The console: command history per user, transcript size, shell command list, links, dropped scripts.
        auto s = openGuiSettings(settingsFile_);
        transcript_->setHistoryLimit(s->value(kHistoryLimitKey, ConsoleHistory::kDefaultLimit).toInt());
        transcript_->setHistory(s->value(ConsoleHistory::kSettingsKey).toStringList());
        transcript_->setMaximumLines(
            s->value(kTranscriptLinesKey, TranscriptWidget::kDefaultMaximumLines).toInt());
    }
    connect(transcript_, &TranscriptWidget::historyChanged, this, [this] {
        auto s = openGuiSettings(settingsFile_);
        s->setValue(ConsoleHistory::kSettingsKey, transcript_->history());
    });
    connect(transcript_, &TranscriptWidget::shellCommandsWanted, this, [this] {
        if (session_.running())
            session_.send(shellCommandsQuery());
        else
            transcript_->setShellCommands({}); // no shell: the built-in list is all there is
    });
    connect(transcript_, &TranscriptWidget::sourceActivated, this,
            [this](const QString& file, int line) { showSource(resolveSourceFile(file), line); });
    connect(transcript_, &TranscriptWidget::doFileDropped, this, [this](const QString& f) { runDoFile(f); });
    connect(objects_, &ObjectsPane::addToWave, this, [this](const QStringList& paths) {
        AddWaveRequest req;
        for (const QString& p : paths) {
            QString pattern = p;
            pattern.replace('.', '/');
            req.patterns.push_back(("/" + pattern).toStdString());
        }
        wave_->addWaveSignals(req);
    });
    connect(objects_, &ObjectsPane::openMemory, this, [this](const QString& path) { showMemoryDock(path); });
    connectSourceNavigation();
    updateStatus();
}

SimWindow::~SimWindow() {
    session_.setWaitPolicy({}); // no dialog from a window that is going away
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

    stack_ = new CallStackPane(this);
    auto* stackDock = new QDockWidget(tr("Call Stack"), this);
    stackDock->setObjectName("stackDock");
    stackDock->setWidget(stack_);
    locals_ = new LocalsPane(this);
    auto* localsDock = new QDockWidget(tr("Locals"), this);
    localsDock->setObjectName("localsDock");
    localsDock->setWidget(locals_);

    list_ = new ListWindow(this);
    auto* listDock = new QDockWidget(tr("List"), this);
    listDock->setObjectName("listDock");
    listDock->setWidget(list_);
    // List <-> wave cursor: a selected row moves the active cursor, a moved cursor selects its row.
    connect(list_, &ListWindow::timeSelected, wave_, &WaveWidget::moveActiveCursorTo);
    connect(wave_, &WaveWidget::cursorsChanged, list_, [this] {
        if (const auto t = wave_->cursorTime(wave_->activeCursor()))
            list_->showTime(*t);
    });
    connect(list_, &ListWindow::signalsAdded, this, [this] { showListDock(); });
    // Dataset paths and expression columns of the list are looked up like the wave rows.
    list_->setSpace(&wave_->space());
    connect(wave_, &WaveWidget::datasetsChanged, list_, [this] { list_->model()->datasetsChanged(); });

    memory_ = new MemoryWindow(this);
    auto* memoryDock = new QDockWidget(tr("Memory"), this);
    memoryDock->setObjectName("memoryDock");
    memoryDock->setWidget(memory_);
    // Memory <-> wave cursor: stepping through writes moves the cursor, a moved cursor is the time shown
    // (when the window follows the cursor).
    connect(memory_, &MemoryWindow::timeSelected, wave_, &WaveWidget::moveActiveCursorTo);
    connect(wave_, &WaveWidget::cursorsChanged, memory_,
            [this] { memory_->showCursorTime(wave_->cursorTime(wave_->activeCursor())); });
    connect(memory_, &MemoryWindow::addToWave, this, [this](const QStringList& paths) {
        AddWaveRequest req;
        for (const QString& p : paths)
            req.patterns.push_back(p.toStdString());
        wave_->addWaveSignals(req);
    });

    createCoverageDock();
    createProjectDocks();

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
    breakAction_ = sim->addAction(makeIcon(Icon::Break), tr("Break"));
    breakAction_->setObjectName("break");
    // Ctrl+Break arrives as Ctrl+Cancel on Windows keyboards, as Ctrl+Pause elsewhere.
    breakAction_->setShortcuts(
        {QKeySequence(Qt::CTRL | Qt::Key_Pause), QKeySequence(Qt::CTRL | Qt::Key_Cancel)});
    breakAction_->setShortcutContext(Qt::WindowShortcut);
    breakAction_->setToolTip(tr("End the run in progress at the current simulation time (Ctrl+Break); Run "
                                "goes on from there"));
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
    sim->addSeparator();
    continueAction_ = sim->addAction(makeIcon(Icon::Continue), tr("Continue"));
    continueAction_->setObjectName("continue");
    continueAction_->setShortcut(QKeySequence(Qt::Key_F5));
    continueAction_->setToolTip(tr("Continue the run stopped at a source line (F5)"));
    stepIntoAction_ = sim->addAction(makeIcon(Icon::StepInto), tr("Step Into"));
    stepIntoAction_->setObjectName("stepInto");
    stepIntoAction_->setShortcut(QKeySequence(Qt::Key_F11));
    stepIntoAction_->setToolTip(
        tr("Run to the next statement of any process, entering called subprograms (F11)"));
    stepOverAction_ = sim->addAction(makeIcon(Icon::StepOver), tr("Step Over"));
    stepOverAction_->setObjectName("stepOver");
    stepOverAction_->setShortcut(QKeySequence(Qt::Key_F10));
    stepOverAction_->setToolTip(tr("Run the stopped process to its next statement, calls included (F10)"));
    stepOutAction_ = sim->addAction(makeIcon(Icon::StepOut), tr("Step Out"));
    stepOutAction_->setObjectName("stepOut");
    stepOutAction_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    stepOutAction_->setToolTip(tr("Run until the current subprogram returns to its caller (Shift+F11)"));
    for (QAction* a : {continueAction_, stepIntoAction_, stepOverAction_, stepOutAction_})
        a->setShortcutContext(Qt::WindowShortcut);
    connect(continueAction_, &QAction::triggered, this, [this] { continueRun(); });
    connect(stepIntoAction_, &QAction::triggered, this, [this] { stepStatement(StepKind::Into); });
    connect(stepOverAction_, &QAction::triggered, this, [this] { stepStatement(StepKind::Over); });
    connect(stepOutAction_, &QAction::triggered, this, [this] { stepStatement(StepKind::Out); });
    connect(stepDeltaAction_, &QAction::triggered, this, [this] { stepDelta(); });
    connect(stepTimeAction_, &QAction::triggered, this, [this] { stepTime(); });
    connect(runAction_, &QAction::triggered, this, [this] { sendCommand("run " + runLengthText()); });
    connect(runLength_, &QLineEdit::returnPressed, this, [this] { sendCommand("run " + runLengthText()); });
    connect(runAllAction_, &QAction::triggered, this, [this] { sendCommand("run -all"); });
    connect(restartAction_, &QAction::triggered, this, [this] { restartSimulation(); });
    connect(stopAction_, &QAction::triggered, this, [this] { stopSimulation(); });
    connect(breakAction_, &QAction::triggered, this, [this] { breakRun(); });

    addToolBarBreak();
    waveToolBar_ = new WaveToolBar(wave_, this);
    addToolBar(waveToolBar_);
    {
        // Follow is a setting: restored here, saved at every change (also when a manual pan switches it off).
        auto s = openGuiSettings(settingsFile_);
        wave_->setFollow(s->value(kFollowSettingKey, false).toBool());
        connect(wave_, &WaveWidget::followChanged, this, [this](bool on) {
            auto st = openGuiSettings(settingsFile_);
            st->setValue(kFollowSettingKey, on);
            st->sync();
        });
    }

    // Menus
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Save wave layout..."), this, [this] { saveLayout(); });
    file->addAction(tr("&Load wave layout..."), this, [this] { loadLayout(); });
    file->addAction(tr("Save wave &format (.do)..."), this, [this] { saveWaveFormatDialog(); });
    file->addAction(tr("Load wave f&ormat (.do)..."), this, [this] { loadWaveFormatDialog(); });
    file->addSeparator();
    QAction* openDs = file->addAction(tr("Open &dataset..."), this, [this] { openDatasetDialog(); });
    openDs->setObjectName("openDataset");
    openDs->setToolTip(
        tr("Open a finished FST waveform next to the simulation, to show and compare its signals"));
    QMenu* closeDs = file->addMenu(tr("&Close dataset"));
    closeDs->setObjectName("closeDatasetMenu");
    connect(closeDs, &QMenu::aboutToShow, this, [this, closeDs] {
        closeDs->clear();
        for (const QString& n : wave_->datasetNames())
            closeDs->addAction(n, this, [this, n] { closeDataset(n); });
        if (closeDs->isEmpty())
            closeDs->addAction(tr("(no dataset)"))->setEnabled(false);
    });
    file->addSeparator();
    addCoverageFileActions(file);
    file->addSeparator();
    file->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);
    addProjectMenu();

    // The Wave menu: arranging the rows (the same actions as the wave window's context menu).
    QMenu* waveMenu = menuBar()->addMenu(tr("&Wave"));
    waveMenu->setObjectName("waveMenu");
    connect(waveMenu, &QMenu::aboutToShow, this, [this, waveMenu] {
        qDeleteAll(waveMenu->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly));
        waveMenu->clear();
        wave_->populateArrangeMenu(waveMenu);
        waveMenu->addSeparator();
        wave_->populateDerivedMenu(waveMenu);
    });
    connect(wave_, &WaveWidget::structureChanged, this, [this] { waveLayoutModified_ = true; });
    connect(wave_, &WaveWidget::editRejected, this,
            [this](const QString& why) { statusBar()->showMessage(why, 8000); });
    connect(wave_, &WaveWidget::navigationMessage, this,
            [this](const QString& m) { statusBar()->showMessage(m, 8000); });

    QMenu* simMenu = menuBar()->addMenu(tr("&Simulation"));
    simMenu->addAction(tr("&Start simulation..."), this, [this] { startDialog(); });
    simMenu->addAction(runAction_);
    simMenu->addAction(runAllAction_);
    simMenu->addAction(breakAction_);
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
    simMenu->addSeparator();
    recordArraysAction_ = simMenu->addAction(tr("Record &memories (arrays)"));
    recordArraysAction_->setObjectName("recordMemories");
    recordArraysAction_->setCheckable(true);
    recordArraysAction_->setToolTip(
        tr("Put arrays of words (memories) into the waveform, for the Memory window; applies from the next "
           "start or "
           "restart. Large memories that are written often make the waveform big."));
    connect(recordArraysAction_, &QAction::toggled, this, [this](bool on) { setRecordMemories(on); });
    addCoverageSimulationActions(simMenu);

    QMenu* debugMenu = menuBar()->addMenu(tr("&Debug"));
    debugMenu->setObjectName("debugMenu");
    debugMenu->addAction(continueAction_);
    debugMenu->addAction(stepIntoAction_);
    debugMenu->addAction(stepOverAction_);
    debugMenu->addAction(stepOutAction_);
    debugMenu->addSeparator();
    QAction* toggleBp = debugMenu->addAction(tr("&Toggle breakpoint on the cursor line"), this, [this] {
        if (!source_->filePath().isEmpty())
            toggleLineBreakpoint(source_->filePath(), source_->view()->textCursor().blockNumber() + 1);
    });
    toggleBp->setObjectName("toggleBreakpoint");
    toggleBp->setShortcut(QKeySequence(Qt::Key_F9));
    debugMenu->addAction(tr("&Breakpoints"), this, [breakpointsDock] {
        breakpointsDock->show();
        breakpointsDock->raise();
    });

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("&Compile libraries..."), this, [this] { showCompileLibraries(); });
    tools->addAction(tr("&Library folders..."), this, [this] { showCompileLibraries(); });

    buildTranscriptMenu();

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(libraryDock->toggleViewAction());
    view->addAction(structureDock->toggleViewAction());
    view->addAction(objectsDock->toggleViewAction());
    view->addAction(transcriptDock->toggleViewAction());
    view->addAction(processesDock->toggleViewAction());
    view->addAction(breakpointsDock->toggleViewAction());
    view->addAction(sourceDock->toggleViewAction());
    view->addAction(stackDock->toggleViewAction());
    view->addAction(localsDock->toggleViewAction());
    view->addAction(listDock->toggleViewAction());
    connect(listDock->toggleViewAction(), &QAction::triggered, this, [this](bool on) {
        if (on)
            showListDock();
    });
    view->addAction(memoryDock->toggleViewAction());
    memoryDock->toggleViewAction()->setObjectName("viewMemory");
    connect(memoryDock->toggleViewAction(), &QAction::triggered, this, [this](bool on) {
        if (on)
            showMemoryDock();
    });
    if (auto* covDock = findChild<QDockWidget*>("coverageDock")) {
        view->addAction(covDock->toggleViewAction());
        covDock->toggleViewAction()->setObjectName("viewCoverage");
        connect(covDock->toggleViewAction(), &QAction::triggered, this, [this](bool on) {
            if (on)
                showCoverageDock();
        });
    }
    for (const char* n : {"projectDock", "problemsDock", "regressionDock"}) {
        auto* d = findChild<QDockWidget*>(n);
        if (d == nullptr)
            continue;
        view->addAction(d->toggleViewAction());
        const QString name = QString::fromLatin1(n).chopped(4);
        connect(d->toggleViewAction(), &QAction::triggered, this, [this, name](bool on) {
            if (on && name != QLatin1String("project"))
                showBottomDock(name);
        });
    }
    view->addSeparator();
    QAction* resetLayout = view->addAction(tr("&Reset layout"), this, [this] { applyDefaultLayout(); });
    resetLayout->setObjectName("resetLayout");
    view->addSeparator();
    view->addAction(tr("Zoom &full"), this, [this] { wave_->zoomFull(); });
    {
        QAction* follow = waveToolBar_->followAction();
        follow->setText(tr("F&ollow simulation (stay at end)"));
        view->addAction(follow);
    }
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
    connect(rendererAutoAction_, &QAction::triggered, this,
            [this] { setRenderPolicy(RenderPolicy::Auto, true); });
    connect(rendererCpuAction_, &QAction::triggered, this,
            [this] { setRenderPolicy(RenderPolicy::Cpu, true); });
    connect(rendererGpuAction_, &QAction::triggered, this,
            [this] { setRenderPolicy(RenderPolicy::Gpu, true); });
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
    QStringList planArgs = folderArgs + vsimArgs;
    if (recordArrays_ && !vsimArgs.contains(QStringLiteral("-lvdarrays")))
        planArgs.prepend(QStringLiteral("-lvdarrays"));
    // Simulation > Collect coverage: the same -coverage as on the command line (planVsim adds nvc's options).
    planArgs = coveragePlanArgs(planArgs, ctx);
    const lvd::tools::Plan plan = lvd::tools::planVsim(toStd(planArgs), ctx);
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

    writtenFiles_.clear();
    for (const std::string& a : plan.calls.front().args) {
        // a waveform the simulator records (an .fst argument, alone or as --opt=file)
        const QString q = QString::fromStdString(a);
        if (q.endsWith(QLatin1String(".fst"), Qt::CaseInsensitive))
            writtenFiles_ << q.mid(q.indexOf(QLatin1Char('=')) + 1);
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
    sessionDir_ = QDir::currentPath();
    locator_.reset(); // a new design: every path is asked again
    locatorProbed_ = false;
    dirty_ = true; // the startup script may have added breakpoints or run
    scriptRan_ = true;
    objects_->setStore(nullptr);
    transcript_->completer().setStore(nullptr);
    transcript_->completer().invalidateShellCommands(); // a new shell, maybe with other commands
    list_->setStore(nullptr);
    memory_->setStore(nullptr);
    structure_->setStore(nullptr);

    NvcSession::Config cfg;
    cfg.nvc = QString::fromStdString(env_.nvc);
    {
        cfg.args = toQt(plan.calls.front().args);
        sessionArgs_ = cfg.args;
        // The elaboration writes into the work library: the libraries are stamped at the first prompt.
        libraryStamps_.clear();
        libraryStampPending_ = true;
        restartUnsupported_ = false;
        coverageSessionPlanned(QString::fromStdString(plan.coverageFile));
    }
    if (!env_.nvcDllDir.empty())
        cfg.pathPrepend << QString::fromStdString(env_.nvcDllDir);
    cfg.startupScripts = toQt(plan.startupScripts);
    // The line breakpoints of this design are in force before the startup script runs (it may run).
    loadLineBreakpoints(vsimArgs);
    lineBps_.resetKernelState();
    QVector<QPair<QString, int>> sent;
    for (int i = 0; i < lineBps_.size(); ++i) {
        LineBreakpoint& b = lineBps_[i];
        if (!b.enabled)
            continue;
        cfg.commands << lineBreakpointCommand(kernelFileName(b.file), b.line);
        sent.push_back({b.file, b.line});
        b.state = LineBreakpoint::State::Pending;
    }

    transcript_->setInputEnabled(true);
    if (!session_.start(cfg)) {
        lineBps_.resetKernelState();
        lineBreakpointsChanged();
        return false;
    }
    const QVector<int> indices = session_.startupCommandIndices();
    for (int i = 0; i < indices.size() && i < sent.size(); ++i) {
        const QString file = sent[i].first;
        const int line = sent[i].second;
        pending_.insert(indices[i],
                        [this, file, line](const QString& json) { lineBreakpointAdded(file, line, json); });
        failures_.insert(indices[i], [this, file, line](const QString& error) {
            if (const int k = lineBps_.indexOf(file, line); k >= 0) {
                lineBps_[k].state = LineBreakpoint::State::Rejected;
                lineBps_[k].note = error;
                lineBreakpointsChanged();
            }
        });
    }
    lineBreakpointsChanged();

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
    // Sources of the open project changed: analyse them first, then start a new process (the kernel's restart
    // keeps the design it elaborated).
    if (project_ != nullptr && project_->isOpen() && !project_->compiling()) {
        project_->refreshStatus();
        const bool stale = std::any_of(project_->statuses().begin(), project_->statuses().end(),
                                       [](const lvd::tools::build::FileStatus& st) {
                                           return st.state == lvd::tools::build::FileState::OutOfDate;
                                       });
        if (stale) {
            transcript_->appendNote(
                tr("---- sources changed: analysing them, then starting the simulation again ----"));
            restartAfterCompile_ = true;
            if (project_->compile(lvd::tools::build::Mode::OutOfDate))
                return;
            restartAfterCompile_ = false;
        }
    }
    // In the running process when nothing was analysed since the start: much faster than a new process, and
    // the shell keeps its Tcl state, breakpoints and forces.
    if (session_.running() && !restartUnsupported_ && !librariesChangedSinceStart()) {
        transcript_->appendNote(tr("---- restart ----"));
        dirty_ = true;
        request(
            restartCommand(), [this](const QString& json) { onRunReply(json); },
            [this](const QString& error) {
                // An empty failure is the restart given while stopped at a source line: its reply comes as
                // the reply of the interrupted run or step.
                if (error.contains(QLatin1String("not supported"))) {
                    restartUnsupported_ = true;
                    relaunchSimulation();
                } else if (!error.isEmpty()) {
                    report(tr("restart failed: %1").arg(error));
                }
            });
        return;
    }
    relaunchSimulation();
}

bool SimWindow::librariesChangedSinceStart() const {
    if (libraryStampPending_)
        return true; // the shell is not up yet
    for (auto it = libraryStamps_.constBegin(); it != libraryStamps_.constEnd(); ++it)
        if (QFileInfo(it.key()).lastModified() != it.value())
            return true;
    return false;
}

void SimWindow::stampLibraries(const QStringList& nvcArgs) {
    // nvc rewrites a library's _index whenever it saves a unit into it.
    libraryStamps_.clear();
    for (const QString& a : nvcArgs) {
        if (!a.startsWith(QLatin1String("--work=")) && !a.startsWith(QLatin1String("--map=")))
            continue;
        const QString spec = a.mid(a.indexOf(QLatin1Char('=')) + 1);
        const int colon = static_cast<int>(spec.indexOf(QLatin1Char(':')));
        if (colon <= 0)
            continue;
        const QString index = QDir(spec.mid(colon + 1)).filePath(QStringLiteral("_index"));
        libraryStamps_.insert(index, QFileInfo(index).lastModified());
    }
}

void SimWindow::onRestartReply(const QString& json) {
    const auto r = parseRestartReply(json);
    if (!r.ok()) {
        transcript_->appendNote(tr("** Unreadable reply from the simulator (%1): %2")
                                    .arg(QString::fromStdString(r.error()), json));
        return;
    }
    leaveStop();
    locator_.reset();
    locatorProbed_ = true;
    probeDeclarations();
    source_->setCurrentLine(0);
    source_->setReadyLines({});
    wave_->clearEventMarkers(); // reports of the earlier run
    for (int i = 0; i < lineBps_.size(); ++i)
        lineBps_[i].hits = 0;
    lineBreakpointsChanged();
    lastStatus_ = RunStatus::Paused;
    haveStop_ = true;
    stopText_ = tr("Restarted at 0 fs");
    QString note = tr("# Restarted at time 0");
    if (r.value().breakpoints > 0 || r.value().forces > 0)
        note += tr(" (%n breakpoint(s)", "", r.value().breakpoints) +
                tr(", %n force(s) kept)", "", r.value().forces);
    if (!sessionCoverFile_.isEmpty())
        note += tr("; coverage counts add up over the runs of this simulation");
    transcript_->appendNote(note);
    pollNow();
    refreshProcesses();
    refreshBreakpoints();
    updateStatus();
    emit restarted();
}

void SimWindow::relaunchSimulation() {
    transcript_->appendNote(tr("---- restart (new simulator process) ----"));
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

bool SimWindow::breakRun() {
    if (!session_.running())
        return false;
    if (session_.interruptPending()) {
        // The run did not reach its next time step: a process may be in a loop without a wait.
        const auto waited = static_cast<int>(session_.interruptPendingMs());
        if (session_.askWaitPolicy(NvcSession::Unresponsive::Break, waited) != NvcSession::WaitDecision::Kill)
            return false;
        transcript_->appendNote(tr("** The simulator did not end the run after Break: killed"));
        session_.kill();
        return true;
    }
    if (!session_.canInterrupt()) {
        statusBar()->showMessage(tr("Break: no run is going"), 5000);
        return false;
    }
    const auto r = session_.interrupt();
    if (!r.ok()) {
        report(tr("Break could not reach the simulator (%1). Simulation > Stop ends the simulation.")
                   .arg(QString::fromStdString(r.error())));
        return false;
    }
    statusBar()->showMessage(tr("Break: the run ends before its next time step"), 5000);
    updateStatus();
    return true;
}

NvcSession::WaitDecision SimWindow::askWaitOrKill(NvcSession::Unresponsive what, int waitedMs) {
    using D = NvcSession::WaitDecision;
    const bool exiting = what == NvcSession::Unresponsive::Exit;
    if (!dialogs_)
        return exiting ? D::Kill : D::Wait; // tests and headless runs never block
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("lvdsim"));
    const QString seconds = QString::number(waitedMs / 1000.0, 'f', 1);
    if (exiting) {
        box.setText(tr("The simulator has not exited yet."));
        QString info =
            tr("It was asked to exit %1 s ago. Wait for it a little longer, or kill it.").arg(seconds);
        if (!sessionCoverFile_.isEmpty())
            info += "\n\n" + tr("This simulation collects coverage: killing it loses the coverage counts it "
                                "writes when it exits (%1).")
                                 .arg(QDir::toNativeSeparators(sessionCoverFile_));
        box.setInformativeText(info);
    } else {
        box.setText(tr("The simulator has not ended the run yet."));
        box.setInformativeText(
            tr("Break was sent %1 s ago. A process may be running in a loop without a wait "
               "statement. Wait a little longer, or kill the simulator (the waveform up to "
               "now is kept).")
                .arg(seconds));
    }
    QPushButton* wait = box.addButton(tr("Wait"), QMessageBox::RejectRole);
    QPushButton* kill = box.addButton(tr("Kill"), QMessageBox::DestructiveRole);
    box.setDefaultButton(wait);
    // The question is void once the simulator exits, or ends the run.
    connect(&session_, &NvcSession::finished, &box, [&box] { box.reject(); });
    if (!exiting)
        connect(&session_, &NvcSession::busyChanged, &box, [&box](bool busy) {
            if (!busy)
                box.reject();
        });
    box.exec();
    return box.clickedButton() == kill ? D::Kill : D::Wait;
}

void SimWindow::sendCommand(const QString& command) {
    if (!session_.running()) {
        transcript_->appendNote(tr("** No simulation is running (Simulation > Start)."));
        return;
    }
    if (command.contains(QLatin1Char('\n'))) {
        // A command over several lines (a proc, a loop): one line for the shell. One that may run, stop at a
        // source line or set breakpoints is followed up like a script.
        static const QRegularExpression acts(QStringLiteral("\\b(run|step|breakpoint|do|source|restart)\\b"));
        if (acts.match(command).hasMatch()) {
            dirty_ = true;
            scriptRan_ = true;
        }
        session_.send(oneLineCommand(command));
        return;
    }
    const QStringList words = command.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const QString first = words.value(0);
    if (first == "run" || first == "step") {
        // While stopped at a source line, a run or step resumes the run (if the shell accepts it): the
        // execution marker goes away until the reply says where it stopped next.
        if (stoppedAtSource_) {
            resumeInFlight_ = true;
            showExecutionPoint();
        }
        QStringList rewritten = words;
        if (!words.contains("-json"))
            rewritten.insert(1, "-json");
        request(rewritten.join(' '), [this](const QString& json) { onRunReply(json); });
        updateStatus();
        return;
    }
    if (first == "do" || first == "source" || first == "breakpoint")
        dirty_ = true;
    if (first == "do" || first == "source")
        scriptRan_ = true; // a run inside may stop at a line without a -json reply
    session_.send(command);
}

int SimWindow::request(const QString& command, std::function<void(const QString&)> handler) {
    const int index = session_.send(command);
    if (index >= 0 && handler)
        pending_.insert(index, std::move(handler));
    return index;
}

int SimWindow::request(const QString& command, std::function<void(const QString&)> handler,
                       std::function<void(const QString&)> onFailure) {
    const int index = request(command, std::move(handler));
    if (index >= 0 && onFailure)
        failures_.insert(index, std::move(onFailure));
    return index;
}

void SimWindow::onShellOutput(const QString& text) {
    // Errors of the shell: "** Error: 0ms+0: text" (the time stamp is dropped). While a process is stopped
    // the shell refuses some commands (force, run with a time); say so in the status bar too.
    outputTail_ += text;
    const qsizetype nl = outputTail_.lastIndexOf(QLatin1Char('\n'));
    if (nl < 0)
        return;
    const QStringList lines = outputTail_.left(nl).split(QLatin1Char('\n'));
    outputTail_ = outputTail_.mid(nl + 1);
    static const QRegularExpression stamp(QStringLiteral("^\\S+\\+\\d+:\\s*"));
    for (const QString& raw : lines) {
        QString l = raw.trimmed();
        while (l.startsWith(QLatin1String("% ")))
            l = l.mid(2).trimmed();
        if (!l.startsWith(QLatin1String("** Error:")))
            continue;
        QString msg = l.mid(9).trimmed();
        msg.remove(stamp);
        lastShellError_ = msg;
        if (stoppedAtSource_)
            statusBar()->showMessage(tr("Refused while stopped: %1").arg(msg), 15000);
    }
}

void SimWindow::onReply(int commandIndex, const QString& json) {
    failures_.remove(commandIndex);
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
        waitingNoteTimer_.stop();
        waitingNoted_ = false;
        pending_.clear(); // commands that failed have no reply, and never will
        const auto failed = std::exchange(failures_, {});
        for (const auto& f : failed)
            f(lastShellError_);
        resumeInFlight_ = false; // a resume that the shell refused leaves the process stopped
        if (stoppedAtSource_)
            showExecutionPoint();
        if (dirty_ && session_.running()) {
            dirty_ = false;
            if (!stoppedAtSource_ && std::exchange(scriptRan_, false))
                probeStop();
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
    failures_.clear();
    lastShellError_.clear();
    outputTail_.clear();
    deletedIds_.clear();
    leaveStop();
    locals_->forgetHistory();
}

void SimWindow::leaveStop() {
    stoppedAtSource_ = false;
    resumeInFlight_ = false;
    frames_.clear();
    selectedFrame_ = 0;
    source_->clearExecutionPoint();
    stack_->clear(tr("Not stopped."));
    locals_->clear(tr("Not stopped."));
}

void SimWindow::showExecutionPoint() {
    if (!stoppedAtSource_ || resumeInFlight_ || frames_.isEmpty()) {
        source_->clearExecutionPoint();
        return;
    }
    const SourceFrame& f = frames_.value(selectedFrame_, frames_.first());
    if (f.file.isEmpty() || f.line <= 0) {
        source_->clearExecutionPoint();
        return;
    }
    source_->setExecutionPoint(resolveSourceFile(f.file), f.line,
                               f.level == 0 ? ExecutionMark::Innermost : ExecutionMark::Caller);
}

void SimWindow::onStopReply(const QString& json) {
    const auto r = parseStopReply(json);
    if (!r.ok()) {
        transcript_->appendNote(tr("** Unreadable reply from the simulator (%1): %2")
                                    .arg(QString::fromStdString(r.error()), json));
        return;
    }
    enterStop(r.value(), {});
}

void SimWindow::probeStop() {
    // A run inside a script (-do, do, source) that stops at a line prints the plain stop text, because the
    // command typed at the prompt decides the form of the reply. The shell then waits at its prompt: ask for
    // the stack, quietly (no error when nothing is stopped).
    request(QStringLiteral("if {![catch {stack -json} ::lvd_stack]} {puts $::lvd_stack}"),
            [this](const QString& json) {
                if (stoppedAtSource_)
                    return;
                const auto s = parseStackReply(json);
                if (!s.ok() || s.value().isEmpty())
                    return;
                StopReply stop;
                stop.nowFs = -1; // not part of the stack reply
                stop.frame = s.value().first();
                for (const SourceFrame& f : s.value())
                    if (f.kind == QLatin1String("process"))
                        stop.process = f.name;
                enterStop(stop, s.value());
            });
}

void SimWindow::enterStop(const StopReply& stop, const QVector<SourceFrame>& knownFrames) {
    lastStop_ = stop;
    stoppedAtSource_ = true;
    resumeInFlight_ = false;
    haveStop_ = true;
    ++stopSerial_;
    const bool noFrame =
        lastStop_.frame.name.isEmpty() && lastStop_.frame.file.isEmpty(); // $stop without a frame
    frames_ = !knownFrames.isEmpty() ? knownFrames
              : noFrame              ? QVector<SourceFrame>{}
                                     : QVector<SourceFrame>{lastStop_.frame};
    selectedFrame_ = 0;
    const SourceFrame& f = lastStop_.frame;
    const QString where = f.file.isEmpty()
                              ? tr("an unknown line")
                              : QStringLiteral("%1:%2").arg(QFileInfo(f.file).fileName()).arg(f.line);
    const bool bySystask = lastStop_.reason == StopReason::Stop;
    if (bySystask)
        stopText_ = f.file.isEmpty() ? tr("Stopped by $stop") : tr("Stopped by $stop at %1").arg(where);
    else
        stopText_ = tr("Stopped at %1 in %2").arg(where, lastStop_.process);
    const QString now = formatFemtoseconds(lastStop_.nowFs);
    if (!knownFrames.isEmpty())
        transcript_->appendNote(tr("# %1 stopped at %2 (in a script)").arg(lastStop_.process, where));
    else if (bySystask)
        transcript_->appendNote(
            tr("# $stop: %1 stopped at %2 (now %3, delta %4); Continue resumes the run")
                .arg(lastStop_.process.isEmpty() ? tr("the simulation") : lastStop_.process, where, now)
                .arg(lastStop_.delta));
    else if (lastStop_.atBreakpoint)
        transcript_->appendNote(tr("# Breakpoint %1: %2 stopped at %3 (now %4, delta %5)")
                                    .arg(lastStop_.breakpointId)
                                    .arg(lastStop_.process, where, now)
                                    .arg(lastStop_.delta));
    else
        transcript_->appendNote(tr("# Step: %1 stopped at %2 (now %3, delta %4)")
                                    .arg(lastStop_.process, where, now)
                                    .arg(lastStop_.delta));
    stack_->setFrames(lastStop_.process, frames_, 0);
    if (!f.file.isEmpty() && f.line > 0)
        showSource(resolveSourceFile(f.file), f.line);
    showExecutionPoint();
    if (auto* d = findChild<QDockWidget*>("stackDock"); d && !d->isHidden())
        d->raise();

    const int serial = stopSerial_;
    if (knownFrames.isEmpty())
        request(stackCommand(), [this, serial](const QString& reply) {
            if (serial != stopSerial_ || !stoppedAtSource_)
                return;
            const auto s = parseStackReply(reply);
            if (!s.ok() || s.value().isEmpty()) {
                transcript_->appendNote(
                    tr("** Unreadable call stack (%1): %2")
                        .arg(QString::fromStdString(s.ok() ? "no frames" : s.error()), reply));
                return;
            }
            frames_ = s.value();
            stack_->setFrames(lastStop_.process, frames_, selectedFrame_);
            showExecutionPoint();
        });
    requestLocals();
    refreshProcesses();
    refreshBreakpoints(); // hit counts
    updateStatus();
    emit stoppedAtLine();
}

void SimWindow::requestLocals() {
    const int serial = stopSerial_;
    const int level = selectedFrame_;
    request(localsCommand(level), [this, serial, level](const QString& reply) {
        if (serial != stopSerial_ || level != selectedFrame_ || !stoppedAtSource_)
            return;
        const auto l = parseLocalsReply(reply);
        if (!l.ok()) {
            transcript_->appendNote(
                tr("** Unreadable variable list (%1): %2").arg(QString::fromStdString(l.error()), reply));
            return;
        }
        const SourceFrame f = frames_.value(level);
        QString note;
        if (f.kind == QLatin1String("function") || f.kind == QLatin1String("procedure"))
            note = tr("No variables are shown for functions and for procedures that cannot wait.");
        else if (f.kind == QLatin1String("other"))
            note = tr("No variables are shown for this frame (Verilog variables are signals: use examine).");
        else
            note = tr("This frame has no variables.");
        locals_->setLocals(lastStop_.process + QLatin1Char('|') + f.symbol, l.value(), serial, note);
    });
}

void SimWindow::selectFrame(int level) {
    if (!stoppedAtSource_ || level < 0 || level >= frames_.size())
        return;
    selectedFrame_ = level;
    if (stack_->selectedLevel() != level) {
        const QSignalBlocker block(stack_);
        stack_->selectLevel(level);
    }
    const SourceFrame f = frames_.value(level);
    if (!f.file.isEmpty() && f.line > 0)
        showSource(resolveSourceFile(f.file), f.line);
    showExecutionPoint();
    requestLocals();
}

void SimWindow::continueRun() {
    if (!stoppedAtSource_) {
        transcript_->appendNote(tr("** Continue: no process is stopped at a source line."));
        return;
    }
    sendCommand(resumeCommand());
}

void SimWindow::stepStatement(StepKind kind) {
    if (kind != StepKind::Into && !stoppedAtSource_) {
        transcript_->appendNote(tr("** Step Over and Step Out need a process stopped at a source line."));
        return;
    }
    sendCommand(statementStepCommand(kind));
}

// ---- line breakpoints ------------------------------------------------------------------------------

QString SimWindow::lineBreakpointKey(const QStringList& vsimArgs) {
    // A design is the working directory and the unit it simulates (the last argument).
    const QString id =
        QDir::currentPath() + QLatin1Char('|') + (vsimArgs.isEmpty() ? QString() : vsimArgs.last());
    return QStringLiteral("debug/lineBreakpoints/") +
           QString::fromLatin1(
               QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

void SimWindow::loadLineBreakpoints(const QStringList& vsimArgs) {
    const QString key = lineBreakpointKey(vsimArgs);
    if (key == lineBpKey_)
        return;
    const QString noDesign = lineBreakpointKey({});
    auto s = openGuiSettings(settingsFile_);
    if (lineBpKey_ == noDesign && !lineBps_.isEmpty() && !s->contains(key)) {
        // Set before any design was known: they go with the first design that is started.
        s->remove(noDesign);
        s->sync();
        lineBpKey_ = key;
        savedBps_.clear();
        lineBreakpointsChanged();
        return;
    }
    lineBpKey_ = key;
    lineBps_ = LineBreakpointList::deserialize(s->value(key).toStringList());
    savedBps_ = lineBps_.serialize();
    lineBreakpointsChanged();
}

void SimWindow::lineBreakpointsChanged() {
    source_->setBreakpoints(lineBps_.items());
    breakpoints_->setLineBreakpoints(lineBps_.items());
    if (lineBpKey_.isEmpty())
        lineBpKey_ = lineBreakpointKey({});
    const QStringList now = lineBps_.serialize();
    if (now == savedBps_)
        return; // only kernel state (ids, hits) changed
    savedBps_ = now;
    auto s = openGuiSettings(settingsFile_);
    if (lineBps_.isEmpty())
        s->remove(lineBpKey_);
    else
        s->setValue(lineBpKey_, now);
    s->sync();
}

QString SimWindow::kernelFileName(const QString& absolutePath) const {
    // The kernel matches the path it knows (as given at analysis) or its last components. The file name alone
    // is enough unless processes of another file with the same name exist: then the kernel's own name is
    // used.
    const QString name = QFileInfo(absolutePath).fileName();
    QString own;
    bool otherWithSameName = false;
    for (const ProcessInfo& p : processes_->processes()) {
        if (p.file.isEmpty() || QFileInfo(p.file).fileName().compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if (sameSourceFile(p.file, absolutePath))
            own = p.file;
        else
            otherWithSameName = true;
    }
    if (otherWithSameName)
        return own.isEmpty() ? absolutePath : own;
    return name;
}

QString SimWindow::resolveSourceFile(const QString& kernelFile) const {
    if (kernelFile.isEmpty())
        return {};
    const QFileInfo fi(kernelFile);
    if (fi.isAbsolute() || fi.exists())
        return absoluteSourcePath(kernelFile);
        // A partial name (typed in the shell): a known file that ends in it.
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    const QString tail = QLatin1Char('/') + QDir::fromNativeSeparators(kernelFile);
    for (const LineBreakpoint& b : lineBps_.items())
        if (b.file.endsWith(tail, cs))
            return b.file;
    for (const ProcessInfo& p : processes_->processes()) {
        const QString abs = absoluteSourcePath(p.file);
        if (abs.endsWith(tail, cs))
            return abs;
    }
    if (!source_->filePath().isEmpty() && source_->filePath().endsWith(tail, cs))
        return source_->filePath();
    return absoluteSourcePath(kernelFile);
}

bool SimWindow::toggleLineBreakpoint(const QString& file, int line) {
    if (file.isEmpty() || line <= 0)
        return false;
    if (lineBps_.indexOf(file, line) >= 0) {
        deleteLineBreakpoint(file, line);
        return false;
    }
    lineBps_.add(file, line);
    sendLineBreakpoint(file, line);
    lineBreakpointsChanged();
    return true;
}

void SimWindow::sendLineBreakpoint(const QString& file, int line) {
    const int i = lineBps_.indexOf(file, line);
    if (i < 0)
        return;
    if (!session_.running()) {
        lineBps_[i].state = LineBreakpoint::State::Planned;
        return;
    }
    lineBps_[i].state = LineBreakpoint::State::Pending;
    const QString abs = lineBps_[i].file;
    request(
        lineBreakpointCommand(kernelFileName(abs), line),
        [this, abs, line](const QString& json) { lineBreakpointAdded(abs, line, json); },
        [this, abs, line](const QString& error) {
            if (const int k = lineBps_.indexOf(abs, line); k >= 0) {
                lineBps_[k].state = LineBreakpoint::State::Rejected;
                lineBps_[k].note = error.isEmpty() ? tr("refused by the simulator") : error;
                lineBreakpointsChanged();
            }
        });
}

void SimWindow::lineBreakpointAdded(const QString& file, int line, const QString& json) {
    const auto r = parseLineBreakpointAdded(json);
    if (!r.ok()) {
        transcript_->appendNote(
            tr("** Unreadable reply (%1): %2").arg(QString::fromStdString(r.error()), json));
        return;
    }
    const int k = lineBps_.indexOf(file, line);
    if (k < 0 || !lineBps_[k].enabled) {
        // Removed or disabled while the add was on its way: take it back out of the kernel.
        deleteKernelBreakpoint(r.value().id);
        return;
    }
    LineBreakpoint& b = lineBps_[k];
    if (!b.ids.contains(r.value().id))
        b.ids.push_back(r.value().id);
    b.state = LineBreakpoint::State::Verified;
    b.note.clear();
    lineBreakpointsChanged();
}

void SimWindow::setLineBreakpointEnabled(const QString& file, int line, bool enabled) {
    const int i = lineBps_.indexOf(file, line);
    if (i < 0 || lineBps_[i].enabled == enabled)
        return;
    LineBreakpoint& b = lineBps_[i];
    b.enabled = enabled;
    if (enabled) {
        sendLineBreakpoint(b.file, b.line);
    } else {
        for (int id : std::as_const(b.ids))
            deleteKernelBreakpoint(id);
        b.ids.clear();
        b.hits = 0;
        b.state = LineBreakpoint::State::Planned;
    }
    lineBreakpointsChanged();
}

void SimWindow::deleteKernelBreakpoint(int id) {
    deletedIds_.insert(id);
    request(QStringLiteral("breakpoint delete -json %1").arg(id), [](const QString&) {});
}

void SimWindow::deleteLineBreakpoint(const QString& file, int line) {
    const int i = lineBps_.indexOf(file, line);
    if (i < 0)
        return;
    for (int id : std::as_const(lineBps_[i].ids))
        deleteKernelBreakpoint(id);
    lineBps_.remove(file, line);
    lineBreakpointsChanged();
}

QString SimWindow::stopText() const {
    return stopText_;
}

void SimWindow::onRunReply(const QString& json) {
    // A restart while stopped at a source line answers the interrupted run or step.
    if (isRestartReply(json)) {
        onRestartReply(json);
        return;
    }
    // A stop at a source line has its own status, which parseRunReply does not take.
    if (isStopReply(json)) {
        onStopReply(json);
        return;
    }
    const auto r = parseRunReply(json);
    if (!r.ok()) {
        transcript_->appendNote(tr("** Unreadable reply from the simulator (%1): %2")
                                    .arg(QString::fromStdString(r.error()), json));
        return;
    }
    const RunReply& run = r.value();
    lastStatus_ = run.status;
    haveStop_ = true;
    if (stoppedAtSource_)
        leaveStop(); // the stopped run went on and ended (or paused at a time)
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
    case RunStatus::Interrupted:
        stopText_ = tr("Interrupted at %1").arg(now);
        note = tr("# Interrupted at %1, delta %2 (Break); Run goes on from here").arg(now).arg(run.delta);
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
        const auto lines = parseLineBreakpointList(json);
        if (!r.ok() || !lines.ok()) {
            transcript_->appendNote(
                tr("** Unreadable breakpoint list (%1): %2")
                    .arg(QString::fromStdString(r.ok() ? lines.error() : r.error()), json));
            return;
        }
        setBreakpointList(r.value());
        // Line breakpoints typed in the shell join the list, deleted ones leave it, hit counts are updated.
        // Ids the GUI deleted after this list was asked for are not taken back.
        QVector<LineBreakpointInfo> listed;
        for (const LineBreakpointInfo& b : lines.value())
            if (!deletedIds_.contains(b.id))
                listed.push_back(b);
        lineBps_.mergeKernelList(listed, [this](const QString& f) { return resolveSourceFile(f); });
        lineBreakpointsChanged();
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
    // Line breakpoints are the GUI's: they go at once, also without a simulation.
    for (const LineBreakpoint& b : lineBps_.items())
        for (int id : b.ids)
            deletedIds_.insert(id);
    lineBps_.clear();
    lineBreakpointsChanged();
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
    else if (n == "callstack" || n == "call-stack")
        n = "stack";
    else if (n == "variables")
        n = "locals";
    QDockWidget* dock = findChild<QDockWidget*>(n + "Dock");
    if (!dock)
        return false;
    if (n == "coverage" && visible) {
        showCoverageDock();
        return true;
    }
    if ((n == "problems" || n == "regression") && visible) {
        showBottomDock(n);
        return true;
    }
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

void SimWindow::noteWaitingCommand() {
    if (!session_.running() || !session_.busy() || waitingNoted_ || waitingNoteTimer_.isActive())
        return;
    if (!waitingNoteTimer_.isSingleShot()) { // first use
        waitingNoteTimer_.setSingleShot(true);
        waitingNoteTimer_.setInterval(1500);
        connect(&waitingNoteTimer_, &QTimer::timeout, this, [this] {
            if (!session_.running() || !session_.busy() || waitingNoted_)
                return;
            waitingNoted_ = true;
            transcript_->appendNote(tr("# The simulator is still running: typed commands wait until the run "
                                       "or step ends or stops at a "
                                       "breakpoint. Simulation > Stop ends the simulation."));
        });
    }
    waitingNoteTimer_.start();
}

void SimWindow::onCommandEntered(const QString& command) {
    noteWaitingCommand();
    if (command.contains(QLatin1Char('\n'))) {
        // Typed over several lines or pasted: every complete command on its own; a single line goes the usual
        // way (restart, vcom, run with -json ...).
        for (const QString& part : splitTopLevelCommands(command)) {
            if (part.contains(QLatin1Char('\n')))
                sendCommand(part);
            else
                onCommandEntered(part);
        }
        return;
    }
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        sendCommand(command); // an empty line just brings the prompt back
        return;
    }

    const QStringList words = toQt(parseTclList(trimmed.toStdString()));
    const QString first = words.value(0);
    if (first == "do" && words.size() >= 2)
        addRecentDoFile(words.at(1));
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
    if (words.value(0) == QLatin1String("commands")) { // the answer to shellCommandsQuery()
        transcript_->setShellCommands(words.mid(1));
        return;
    }
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
        if (req.value().expr) {
            const QString err = wave_->checkExpression(QString::fromStdString(*req.value().expr));
            if (!err.isEmpty()) {
                transcript_->appendNote(tr("** add wave -expr: %1").arg(err));
                return;
            }
        }
        if (wave_->addWaveSignals(req.value()) == 0 && !req.value().divider)
            transcript_->appendNote(wave_->lastEditError().isEmpty()
                                        ? tr("** add wave: no signal matches")
                                        : tr("** add wave: %1").arg(wave_->lastEditError()));
    } else if (words.size() >= 2 && words[0] == "add" && words[1] == "list") {
        // Same patterns and options as add wave (-r, -radix, -label); the columns go to the List dock.
        const auto req = parseAddWave(toStd(words.mid(2)));
        if (!req.ok()) {
            transcript_->appendNote("** " + QString::fromStdString(req.error()));
            return;
        }
        if (list_->model()->store() == nullptr) {
            transcript_->appendNote(tr("** add list: the design has no signal definitions yet"));
            return;
        }
        if (list_->addList(req.value()) == 0)
            transcript_->appendNote(tr("** add list: no signal matches"));
    } else if (words.size() >= 2 && words[0] == "add" && (words[1] == "mem" || words[1] == "memory")) {
        const auto req = parseAddMem(toStd(words.mid(2)));
        if (!req.ok()) {
            transcript_->appendNote("** " + QString::fromStdString(req.error()));
            return;
        }
        showMemoryDock();
        QString err;
        if (!req.value().paths.empty() && !memory_->applyRequest(req.value(), &err))
            transcript_->appendNote(tr("** add mem: %1").arg(err));
    } else if (words.size() >= 2 && words[0] == "view") {
        // view memory | list | objects | transcript | source | processes | breakpoints | structure | library
        // | wave
        for (const QString& w : words.mid(1)) {
            const QString n = w.toLower();
            if (n.startsWith(QLatin1Char('-')) || n == "wave" || n == "*")
                continue;
            if (n == "memory" || n == "mem" || n == "memories")
                showMemoryDock();
            else if (n == "list")
                showListDock();
            else if (!setDockVisible(n == "signals" ? QStringLiteral("objects") : n, true))
                transcript_->appendNote(tr("** view: no window named %1").arg(w));
        }
    } else if (words.size() >= 2 && words[0] == "delete" && words[1] == "list") {
        list_->clear();
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
        transcript_->completer().setStore(store_.get());
        list_->setStore(store_);
        memory_->setStore(store_);
        structure_->setStore(store_.get());
        refreshLibraries(); // vsim has elaborated the design by now
        storeAttached_ = true;
    }
    if (storeAttached_ && r.restarted) {
        // Values from time zero again: a new view of the same store (dataChanged alone would keep the old
        // range, cursors and selection).
        wave_->setStore(store_);
        list_->refresh();
        memory_->refresh();
    } else if (storeAttached_ && r.changed()) {
        wave_->dataChanged();
        list_->refresh();
        memory_->refresh();
    }
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
    leaveStop();
    locator_.reset();
    lineBps_.resetKernelState(); // sent again at the next start
    lineBreakpointsChanged();
    pending_.clear();
    failures_.clear();
    pollNow();
    pollTimer_.stop();
    transcript_->appendNote(tr("---- simulation ended (exit code %1) ----").arg(exitCode));
    coverageSessionFinished();
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
    QAction* policyAction = renderPolicy_ == RenderPolicy::Gpu   ? rendererGpuAction_
                            : renderPolicy_ == RenderPolicy::Cpu ? rendererCpuAction_
                                                                 : rendererAutoAction_;
    // Checked: the choice, or QPainter when that is what is really drawing.
    (gpu || renderPolicy_ == RenderPolicy::Cpu ? policyAction : rendererCpuAction_)->setChecked(true);
    const QString tip =
        fellBack ? tr("GPU renderer not available (%1), using QPainter").arg(reason) : QString();
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
    const bool quiet =
        renderPolicy_ == RenderPolicy::Auto && platformHasNoGpu(QGuiApplication::platformName());
    if (fellBack && !fallbackNoted_ && !quiet) {
        fallbackNoted_ = true;
        transcript_->appendNote(tr("GPU renderer not available (%1), using QPainter").arg(reason));
    }
}

void SimWindow::updateStatus() {
    QString state;
    if (session_.running())
        state = session_.busy()    ? tr("Running...")
                : stoppedAtSource_ ? tr("Stopped at a source line")
                                   : tr("Ready");
    else if (finished_)
        state = tr("Simulation ended");
    else
        state = tr("No simulation");
    stateLabel_->setText(state);

    if (stoppedAtSource_ && session_.running() && lastStop_.nowFs >= 0) {
        // The waveform stream is written at the end of a run; while a process is stopped the kernel's time is
        // the one to show.
        timeLabel_->setText(tr("Now: %1").arg(formatFemtoseconds(lastStop_.nowFs)));
    } else if (store_ && storeAttached_) {
        timeLabel_->setText(tr("Now: %1").arg(QString::fromStdString(
            formatTime(static_cast<double>(store_->endTime()), store_->timescaleExponent()))));
    } else {
        timeLabel_->setText({});
    }
    const bool live = session_.running();
    // While a process is stopped at a source line the shell refuses a run with a time and cycle steps;
    // Run -all, Continue and the statement steps resume the run.
    const bool stopped = live && stoppedAtSource_;
    const bool idle = live && !session_.busy();
    runAction_->setEnabled(live && !stopped);
    runAllAction_->setEnabled(live);
    stopAction_->setEnabled(live);
    // Also while a break is pending: pressed again it offers to kill the simulator.
    breakAction_->setEnabled(session_.canInterrupt() || (live && session_.interruptPending()));
    restartAction_->setEnabled(!lastArgs_.isEmpty());
    stepDeltaAction_->setEnabled(live && !stopped);
    stepTimeAction_->setEnabled(live && !stopped);
    continueAction_->setEnabled(stopped && idle);
    stepIntoAction_->setEnabled(idle && (stopped || lastStatus_ != RunStatus::Finished));
    stepOverAction_->setEnabled(stopped && idle);
    stepOutAction_->setEnabled(stopped && idle);
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
    if (!maybeSaveProject()) {
        e->ignore();
        return;
    }
    saveSession();
    pollTimer_.stop();
    session_.stop();
    QMainWindow::closeEvent(e);
}

// ---------------------------------------------------------------------------------------------
// Libraries, session, wave format

void SimWindow::refreshLibraries() {
    libraryInfos_ = lvd::tools::scanLibraries(planContext(), ".");
    library_->setLibraries(libraryInfos_);
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
    QStringList extra = toQt(args);
    if (project_ != nullptr && project_->isOpen()) {
        // The libraries of the open project are visible to every simulation (vsim -L for each).
        for (const auto& [name, path] : project_->libraryMappings()) {
            ctx.libs.set(name, path);
            const QString n = QString::fromStdString(name);
            if (!skip.contains(n, Qt::CaseInsensitive) && !extra.contains(n, Qt::CaseInsensitive))
                extra << QStringLiteral("-L") << n;
        }
    }
    if (extraArgs)
        *extraArgs = extra;
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

void SimWindow::showListDock() {
    auto* listDock = findChild<QDockWidget*>("listDock");
    auto* sourceDock = findChild<QDockWidget*>("sourceDock");
    // A tab next to Source the first time (tabifying a hidden dock at layout time leaves a stray tab bar
    // behind).
    if (sourceDock != nullptr && !sourceDock->isHidden() && !sourceDock->isFloating() &&
        !listDock->isFloating() && !tabifiedDockWidgets(sourceDock).contains(listDock))
        tabifyDockWidget(sourceDock, listDock);
    listDock->show();
    listDock->raise();
}

bool SimWindow::showMemoryDock(const QString& path) {
    auto* memoryDock = findChild<QDockWidget*>("memoryDock");
    auto* sourceDock = findChild<QDockWidget*>("sourceDock");
    if (sourceDock != nullptr && !sourceDock->isHidden() && !sourceDock->isFloating() &&
        !memoryDock->isFloating() && !tabifiedDockWidgets(sourceDock).contains(memoryDock))
        tabifyDockWidget(sourceDock, memoryDock);
    memoryDock->show();
    memoryDock->raise();
    pollNow(); // the definitions may have arrived since the last tick
    return path.isEmpty() || memory_->showMemory(path);
}

void SimWindow::setRecordMemories(bool on) {
    recordArrays_ = on;
    if (recordArraysAction_ != nullptr && recordArraysAction_->isChecked() != on) {
        const QSignalBlocker block(recordArraysAction_);
        recordArraysAction_->setChecked(on);
    }
    auto s = openGuiSettings(settingsFile_);
    s->setValue(kRecordArraysSettingKey, on);
    s->sync();
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
    QDockWidget* listDock = dock("listDock");
    QDockWidget* memoryDock = dock("memoryDock");
    QDockWidget* stackDock = dock("stackDock");
    QDockWidget* localsDock = dock("localsDock");
    QDockWidget* coverageDock = dock("coverageDock");
    QDockWidget* projectDock = dock("projectDock");
    QDockWidget* problemsDock = dock("problemsDock");
    QDockWidget* regressionDock = dock("regressionDock");
    const QList<QDockWidget*> all = {libraryDock,   structureDock,   objectsDock,   transcriptDock,
                                     processesDock, breakpointsDock, sourceDock,    listDock,
                                     memoryDock,    stackDock,       localsDock,    coverageDock,
                                     projectDock,   problemsDock,    regressionDock};
    for (QDockWidget* d : all) {
        d->setFloating(false);
        removeDockWidget(d); // also hides it; makes this usable for a reset
    }

    // Left column: Library | Sim as tabs, Objects below them.
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);
    splitDockWidget(libraryDock, objectsDock,
                    Qt::Vertical); // before tabify: splitting a tab group makes a tab
    tabifyDockWidget(libraryDock, structureDock);
    tabifyDockWidget(libraryDock, projectDock);
    // Bottom: Transcript beside Processes | Breakpoints. Right: Source.
    addDockWidget(Qt::BottomDockWidgetArea, transcriptDock);
    splitDockWidget(transcriptDock, processesDock, Qt::Horizontal);
    splitDockWidget(processesDock, localsDock, Qt::Horizontal); // before tabify, as above
    tabifyDockWidget(processesDock, breakpointsDock);
    tabifyDockWidget(processesDock, stackDock);
    addDockWidget(Qt::RightDockWidgetArea, sourceDock);
    addDockWidget(Qt::RightDockWidgetArea,
                  listDock); // hidden; showListDock() puts it in a tab next to Source
    addDockWidget(Qt::RightDockWidgetArea, memoryDock); // hidden too; showMemoryDock() tabs it next to Source
    addDockWidget(Qt::BottomDockWidgetArea,
                  coverageDock); // hidden; showCoverageDock() tabs it next to Transcript
    // Hidden as well; showBottomDock() tabs them next to the Transcript when they have something to show.
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);
    addDockWidget(Qt::BottomDockWidgetArea, regressionDock);
    for (QDockWidget* d : all)
        if (d != listDock && d != memoryDock && d != coverageDock && d != problemsDock &&
            d != regressionDock) // on demand (add list, add mem, View menu, compile errors, regression)
            d->show();

    // Library is in front until a design is loaded (startSimulation raises Sim).
    libraryDock->raise();
    processesDock->raise();
    sourceDock->raise();

    resizeDocks({libraryDock, objectsDock}, {320, 240}, Qt::Vertical);
    resizeDocks({libraryDock}, {300}, Qt::Horizontal);
    resizeDocks({sourceDock}, {440}, Qt::Horizontal);
    resizeDocks({transcriptDock, processesDock, localsDock}, {560, 420, 420}, Qt::Horizontal);
    resizeDocks({transcriptDock}, {190}, Qt::Vertical);
}

// Bump when docks are added or removed: a saved layout of another version is ignored, not half applied.
constexpr int kLayoutVersion =
    8; // 8: Project, Problems, Regression docks; 7: Coverage dock, 6: Call Stack and Locals, 5: Memory dock,
       // 4: List dock (3: Objects below Library|Sim; 2: Processes, Breakpoints, Source)

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
    recordArrays_ = s->value(kRecordArraysSettingKey, false).toBool();
    coverageEnabled_ = s->value(kCoverageSettingKey, false).toBool();
    if (coverageAction_ != nullptr) {
        const QSignalBlocker block(coverageAction_);
        coverageAction_->setChecked(coverageEnabled_);
    }
    if (recordArraysAction_ != nullptr) {
        const QSignalBlocker block(recordArraysAction_);
        recordArraysAction_->setChecked(recordArrays_);
    }
}

void SimWindow::openDatasetDialog() {
    const QString p = QFileDialog::getOpenFileName(this, tr("Open dataset"), {},
                                                   tr("FST waveforms (*.fst);;All files (*)"));
    if (!p.isEmpty() && !openDataset(p))
        QMessageBox::warning(this, tr("Open dataset"), lastError_);
}

bool SimWindow::openDataset(const QString& path, const QString& name) {
    lastError_.clear();
    auto fail = [&](const QString& why) {
        lastError_ = why;
        transcript_->appendNote("** " + why);
        return false;
    };
    const QFileInfo fi(path);
    if (!fi.isFile())
        return fail(tr("open dataset: %1 is not a file").arg(path));
    // Never read a waveform while the simulator writes it (CLAUDE.md, FST is only readable once closed).
    if (session_.running())
        for (const QString& w : writtenFiles_)
            if (QFileInfo(w).canonicalFilePath() == fi.canonicalFilePath())
                return fail(
                    tr("open dataset: the simulation is still writing %1; open it after the run").arg(path));
    QString dsName = name;
    if (dsName.isEmpty()) {
        for (QChar c : fi.completeBaseName())
            dsName += c.isLetterOrNumber() || c == QLatin1Char('_') ? c : QLatin1Char('_');
        if (dsName.isEmpty() || !dsName[0].isLetter())
            dsName.prepend(QLatin1String("ds_"));
    }
    if (wave_->datasetNames().contains(dsName))
        return fail(tr("open dataset: a dataset named %1 is already open").arg(dsName));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto loaded = lvd::wave::loadFst(path.toStdString());
    QApplication::restoreOverrideCursor();
    if (!loaded.ok())
        return fail(tr("open dataset: %1").arg(QString::fromStdString(loaded.error())));
    auto st = std::make_shared<WaveStore>(std::move(loaded).value());
    if (store_ && st->timescaleExponent() != store_->timescaleExponent()) {
        auto re = lvd::wave::retimeStore(*st, store_->timescaleExponent());
        if (!re.ok())
            return fail(tr("open dataset: %1").arg(QString::fromStdString(re.error())));
        st = std::make_shared<WaveStore>(std::move(re).value());
    }
    const QString err = wave_->addDataset(dsName, st);
    if (!err.isEmpty())
        return fail(tr("open dataset: %1").arg(err));
    transcript_->appendNote(tr("Dataset %1: %2 (%3 signals); add its signals with add wave %1:/path")
                                .arg(dsName, path)
                                .arg(st->signalCount()));
    return true;
}

bool SimWindow::closeDataset(const QString& name) {
    lastError_.clear();
    if (!wave_->removeDataset(name)) {
        lastError_ = tr("no dataset named %1").arg(name);
        return false;
    }
    transcript_->appendNote(tr("Dataset %1 closed").arg(name));
    return true;
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
            return true; // the current selection
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
    auto result = [&](bool ok) {
        return ok ? true
                  : fail(wave_->lastEditError().isEmpty() ? tr("wave op changed nothing")
                                                          : wave_->lastEditError());
    };

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
        return selectNames(1) &&
               result(wave_->groupSelected(a.size() > 2 ? a[2].toStdString() : std::string()));
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
        return result(cmd == "divider-above" ? wave_->insertDividerAbove(label)
                                             : wave_->insertDividerBelow(label));
    }
    if (cmd == "toggle" && a.size() >= 2) {
        const int r = rowByName(a[1]);
        if (r < 0)
            return fail(tr("wave op: no row named %1").arg(a[1]));
        wave_->toggleGroup(r);
        return true;
    }
    if (cmd == "follow" && a.size() >= 2) {
        if (a[1] != "on" && a[1] != "off")
            return fail(tr("wave op: follow takes on or off"));
        wave_->setFollow(a[1] == "on");
        return true;
    }
    if (cmd == "edge" && a.size() >= 2) { // edge next|prev [any|rising|falling]: step the active cursor
        const EdgeKind kind = a.value(2) == "rising"    ? EdgeKind::Rising
                              : a.value(2) == "falling" ? EdgeKind::Falling
                                                        : EdgeKind::Any;
        return wave_->jumpToEdge(a[1] == "prev" ? -1 : +1, kind) ? true : fail(tr("wave op: no such edge"));
    }
    if (cmd == "find" && a.size() >= 3) { // find next|prev VALUE [equals|not-equals|rising|falling|either]
        FindValueDialog* d = wave_->showFindDialog();
        static const QStringList kConditions = {"equals", "not-equals", "rising", "falling", "either"};
        d->conditionBox()->setCurrentIndex(std::max(0, static_cast<int>(kConditions.indexOf(a.value(3)))));
        d->valueEdit()->setText(a[2]);
        return d->find(a[1] == "prev" ? -1 : +1).found
                   ? true
                   : fail(tr("wave op: %1").arg(d->statusLabel()->text()));
    }
    if (cmd == "zoom" &&
        a.size() >= 2) { // zoom in by a factor (> 1) or out (< 1) around the middle of the view
        const double f = a[1].toDouble();
        if (!(f > 0.0))
            return fail(tr("wave op: zoom takes a positive factor"));
        wave_->zoomBy(f);
        return true;
    }
    if (cmd == "scroll-time" &&
        a.size() >= 2) { // pan so that the left edge is at a fraction (0..1) of the run
        const double f = std::clamp(a[1].toDouble(), 0.0, 1.0);
        QScrollBar* bar = wave_->horizontalScrollBar();
        bar->setValue(static_cast<int>(f * bar->maximum()));
        return true;
    }
    if (cmd == "analog-range" && a.size() >= 3) { // analog-range NAME[,NAME] auto | MIN MAX
        if (!selectNames(1))
            return false;
        if (a[2] == "auto")
            return result(wave_->setAnalogRangeForSelection(true));
        if (a.size() < 4)
            return fail(tr("wave op: analog-range needs auto or MIN MAX"));
        return result(wave_->setAnalogRangeForSelection(false, a[2].toDouble(), a[3].toDouble()));
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
    if (cmd == "expr" && a.size() >= 3)
        return result(wave_->addExpression(a[1], a.mid(2).join(QLatin1Char(' '))));
    if (cmd == "compare-with" && a.size() >= 3) {
        if (!selectNames(2))
            return false;
        lvd::wave::Time tol = 0;
        if (a.size() > 3) {
            const auto t =
                lvd::wave::parseDuration(a[3].toStdString(), store_ ? store_->timescaleExponent() : -15);
            if (!t)
                return fail(tr("wave op: bad tolerance %1").arg(a[3]));
            tol = *t;
        }
        return result(wave_->compareSelectionWith(a[1], tol) > 0);
    }
    if (cmd == "compare-datasets" && a.size() >= 3) {
        lvd::wave::Time tol = 0;
        if (a.size() > 4) {
            const auto t =
                lvd::wave::parseDuration(a[4].toStdString(), store_ ? store_->timescaleExponent() : -15);
            if (!t)
                return fail(tr("wave op: bad tolerance %1").arg(a[4]));
            tol = *t;
        }
        auto ds = [](const QString& n) { return n == "-" ? QString() : n; };
        const auto d =
            wave_->compareDatasets(ds(a[1]), ds(a[2]), tol, a.size() > 3 && a[3] != "-" ? a[3] : QString());
        return d.differing.empty() ? fail(wave_->lastEditError().isEmpty() ? tr("wave op: no differences")
                                                                           : wave_->lastEditError())
                                   : true;
    }
    if (cmd == "difference" && a.size() >= 2)
        return wave_->jumpToDifference(a[1] == "prev" ? -1 : +1) ? true : fail(tr("wave op: no difference"));
    return fail(tr("unknown wave op: %1").arg(op));
}

} // namespace lvd::gui

namespace lvd::gui {

void SimWindow::setDialogsEnabled(bool enabled) {
    dialogs_ = enabled;
    transcript_->setDialogsEnabled(enabled);
}

QString SimWindow::shellCommandsQuery() {
    return QStringLiteral("puts \"@@lvd [list commands {*}[lsort [info commands]]]\"");
}

void SimWindow::buildTranscriptMenu() {
    QMenu* menu = menuBar()->addMenu(tr("T&ranscript"));
    menu->setObjectName("transcriptMenu");
    QAction* runDo = menu->addAction(tr("&Run do file..."), this, [this] { runDoFileDialog(); });
    runDo->setObjectName("runDoFile");
    runDo->setToolTip(
        tr("Run a script in the simulator with do (a .do file can also be dropped on the console)"));
    QMenu* recent = menu->addMenu(tr("Recent &do files"));
    recent->setObjectName("recentDoMenu");
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        const QStringList files = recentDoFiles();
        for (const QString& f : files)
            recent->addAction(QDir::toNativeSeparators(f), this, [this, f] { runDoFile(f); });
        if (files.isEmpty()) {
            recent->addAction(tr("(none)"))->setEnabled(false);
        } else {
            recent->addSeparator();
            recent->addAction(tr("Clear list"), this, [this] {
                auto s = openGuiSettings(settingsFile_);
                s->remove(kRecentDoFilesKey);
            });
        }
    });
    menu->addSeparator();
    menu->addAction(transcript_->findAction());
    menu->addAction(transcript_->copyWithoutPromptsAction());
    menu->addAction(transcript_->saveAction());
    menu->addAction(transcript_->clearAction());
    menu->addSeparator();
    menu->addAction(transcript_->historyAction());
}

void SimWindow::runDoFile(const QString& path) {
    if (path.isEmpty())
        return;
    transcript_->submitCommand(QStringLiteral("do ") + tclFileWord(QFileInfo(path).absoluteFilePath()));
}

void SimWindow::runDoFileDialog() {
    if (!dialogs_)
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Run do file"), QString(),
                                                      tr("Scripts (*.do *.tcl *.udo);;All files (*)"));
    if (!path.isEmpty())
        runDoFile(path);
}

QStringList SimWindow::recentDoFiles() const {
    auto s = openGuiSettings(settingsFile_);
    return s->value(kRecentDoFilesKey).toStringList();
}

void SimWindow::addRecentDoFile(const QString& file) {
    QString f = file;
    if (f.size() >= 2 && f.startsWith(QLatin1Char('{')) && f.endsWith(QLatin1Char('}')))
        f = f.mid(1, f.size() - 2);
    const QFileInfo fi(f);
    if (!fi.isFile())
        return; // a typo, or a name only the shell can resolve: not worth remembering
    const QString path = QDir::cleanPath(fi.absoluteFilePath());
    auto s = openGuiSettings(settingsFile_);
    QStringList l = s->value(kRecentDoFilesKey).toStringList();
#ifdef Q_OS_WIN
    l.removeIf([&](const QString& e) { return e.compare(path, Qt::CaseInsensitive) == 0; });
#else
    l.removeAll(path);
#endif
    l.prepend(path);
    while (l.size() > kRecentDoFilesLimit)
        l.removeLast();
    s->setValue(kRecentDoFilesKey, l);
}

} // namespace lvd::gui
