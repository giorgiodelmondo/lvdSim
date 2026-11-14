// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/declaration_locator.hpp"
#include "app/line_breakpoints.hpp"
#include "sim/debug_commands.hpp"
#include "sim/nvc_session.hpp"
#include "sim/nvc_tool.hpp"
#include "sim/parsers/json_replies.hpp"
#include "wave/render_policy.hpp"

#include <lvdwave/live_reader.hpp>
#include <lvdwave/store.hpp>

#include <QDateTime>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <environment.hpp>
#include <functional>
#include <libraries.hpp>
#include <memory>

class QAction;
class QLabel;
class QLineEdit;
class QProcess;
class QMenu;

namespace lvd::gui {

class BreakpointPane;
class CallStackPane;
class CompileLibsDialog;
class CoveragePane;
class LibraryPane;
class ListWindow;
class LocalsPane;
class MemoryWindow;
class ObjectsPane;
class ProblemsPane;
class ProcessesPane;
class ProjectPane;
class ProjectSession;
class RegressionPane;
class SourcePane;
class StructurePane;
class TranscriptWidget;
class WaveToolBar;
class WaveWidget;

/// The simulation window: wave window, objects browser, transcript and the controls to run, restart and
/// stop. It starts the simulator exactly as `vsim` would (same option translation), keeps it as a child
/// process and shows what it writes to its live waveform stream while it runs.
class SimWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit SimWindow(lvd::tools::Environment environment, QString settingsFile = {},
                       QWidget* parent = nullptr);
    ~SimWindow() override;

    /// Puts every dock back in the default arrangement (also used for the first start): Library | Sim tabs
    /// above Objects on the left, Transcript, Processes | Breakpoints | Call Stack and Locals at the bottom,
    /// Source on the right.
    void applyDefaultLayout();

    /// Start a simulation. `vsimArgs` are vsim options without -c, for example
    /// {"-do", "add wave -r /*; run 100 ns", "work.top"}. Returns false if they are invalid (the reason
    /// is shown in the transcript). A running simulation is stopped first; the wave rows are kept.
    bool startSimulation(const QStringList& vsimArgs);
    /// Simulation > Restart: back to time zero, the wave rows are kept. In the running process (`restart`)
    /// when no library was analysed since the start; else (and when the kernel refuses) in a new process,
    /// after analysing the out-of-date files of an open project.
    void restartSimulation();
    /// True when a library the simulation uses was analysed again since it started.
    bool librariesChangedSinceStart() const;
    /// Simulation > Stop: ends the simulator (a run in progress is interrupted first). When it has not exited
    /// within the timeout, the session's wait policy decides (a Wait / Kill dialog unless dialogs are off).
    void stopSimulation();
    /// Simulation > Break (Ctrl+Break): asks the simulator to end the run in progress before its next time
    /// step (NvcSession::interrupt). Pressed again while that is pending (a process that never waits, for
    /// example), the wait policy decides whether to kill the simulator. Returns false if nothing was sent or
    /// killed.
    bool breakRun();
    bool simulationRunning() const;

    /// Send a command to the simulator as if typed in the transcript. `run` and `step` get the -json option
    /// added, so that their reply (time, stop reason, breakpoint hit) can be read; the transcript shows the
    /// command as sent and a one line summary of the reply.
    void sendCommand(const QString& command);
    /// Send a `-json` command and call `handler` with its reply line when it arrives. A command that fails
    /// prints an error and has no reply: the handler is dropped when the shell is idle again. Returns the
    /// command number, or -1 without a simulation.
    int request(const QString& command, std::function<void(const QString&)> handler);
    void stepDelta() { sendCommand("step -delta"); }
    void stepTime() { sendCommand("step -time"); }
    /// Reads the process list / the breakpoint list from the simulator (also done after every stop).
    void refreshProcesses();
    void refreshBreakpoints();
    void addBreakpoint(const QString& time, const QString& unit);
    void deleteBreakpoint(int id);
    void deleteAllBreakpoints();
    /// Shows a source file in the Source dock and raises the dock; false (with the reason in the
    /// transcript) if it cannot be read.
    bool showSource(const QString& file, int line);

    // ---- source navigation (View Declaration / Definition / Instantiation / Source) -------------------
    /// Shows where an object of the running simulation is declared: `describe -json -location` for its path
    /// (asked once per path and session), the file looked up as declarationSourceFile() does, then the Source
    /// pane at the line with the identifier selected (a jump Back can return from). Problems (no simulation,
    /// an old kernel, an unknown name, a file that is not found) are reported in the transcript.
    void viewDeclaration(const QString& shellPath);
    /// The same for a region: its design unit (or block / generate statement), or with `instantiation` the
    /// statement in the parent that instantiates it.
    void viewRegion(const QString& shellPath, bool instantiation);
    /// The source of a library unit ("library.unit", the architecture if not empty): the open project's file,
    /// else the file nvc names (`--print-deps`); never starts a simulation.
    void viewLibraryUnit(const QString& unit, const QString& architecture);
    /// Why the source of a library unit cannot be shown (empty: it can, or it is worth asking nvc).
    QString librarySourceBlocker(const QString& unit, const QString& architecture) const;
    /// Opens the location in the Source pane as a jump (history); false with a transcript note on failure.
    bool jumpToSource(const SourceLocation& location);
    /// A source file named by the kernel (as given at analysis): absolute names as they are, relative ones
    /// in the folder the simulation started in, the project folder, the library folders (and their parents)
    /// and the coverage folders. Empty if none has it; `searched` gets the folders looked in.
    QString declarationSourceFile(const QString& name, QStringList* searched = nullptr) const;
    const DeclarationLocator& declarationLocator() const noexcept { return locator_; }
    /// Why View Declaration cannot be used now (empty when it can).
    QString declarationBlocker() const;
    /// Waits until the library source lookup in progress (nvc --print-deps) ended (tests). False on timeout.
    bool waitForLibraryTool(int timeoutMs = 30000);

    // ---- source debugger (docs/protocol.md, "Source debugger") ----------------------------------------
    /// Debug > Continue: resume the run stopped at a source line.
    void continueRun();
    /// Debug > Step Into / Over / Out. Into also works when nothing is stopped (runs to the first statement).
    void stepStatement(StepKind kind);
    /// Gutter click: sets a line breakpoint, or removes the one on that line. Kept in the settings for the
    /// design and sent to the simulator when it runs (else at the next start). Returns true when it was set.
    bool toggleLineBreakpoint(const QString& file, int line);
    void setLineBreakpointEnabled(const QString& file, int line, bool enabled);
    void deleteLineBreakpoint(const QString& file, int line);
    const LineBreakpointList& lineBreakpoints() const noexcept { return lineBps_; }
    /// True while a process is stopped at a source line (the shell then reads commands, the run is
    /// suspended).
    bool stoppedAtSource() const noexcept { return stoppedAtSource_; }
    /// The last stop at a source line (valid when stoppedAtSource()).
    const StopReply& lastStop() const noexcept { return lastStop_; }
    /// Frames of the stopped process, innermost first, as the Call Stack shows them.
    QVector<SourceFrame> stackFrames() const { return frames_; }
    /// Shows a frame in the Source pane and its variables in Locals (what selecting it in the Call Stack
    /// does).
    void selectFrame(int level);
    int selectedFrame() const noexcept { return selectedFrame_; }
    CallStackPane* callStack() const noexcept { return stack_; }
    LocalsPane* locals() const noexcept { return locals_; }
    /// The last error the shell printed ("** Error: ...", without the prefix and the time stamp).
    QString lastShellError() const { return lastShellError_; }
    /// Settings key of the line breakpoints of the design started with these arguments.
    static QString lineBreakpointKey(const QStringList& vsimArgs);
    /// Show (and raise) or hide a dock by name: library, structure (or sim), objects, transcript, processes,
    /// breakpoints, source, list, memory, stack (or callstack), locals. Used by the screenshot options of
    /// lvdsim-gui. Returns false for an unknown name.
    bool setDockVisible(const QString& name, bool visible);

    ProcessesPane* processes() const noexcept { return processes_; }
    SourcePane* source() const noexcept { return source_; }
    BreakpointPane* breakpoints() const noexcept { return breakpoints_; }
    /// What the status bar says about the last stop: "Paused at 20 ns", "Breakpoint 1 at 50 ns", ...
    QString stopText() const;
    RunStatus lastStatus() const noexcept { return lastStatus_; }

    /// Read the live stream now instead of waiting for the next timer tick.
    void pollNow();
    /// Spin the event loop until the simulator is idle (prompt shown, nothing pending) and the waveform
    /// stream has been read up to there. Returns false on timeout or if there is no simulation.
    bool waitUntilIdle(int timeoutMs = 20000);
    // Process pending events including deferred deletes, so Qt removes the dock tab bars it replaced
    // (a headless run never enters the event loop that would do it).
    void settleLayout();

    WaveWidget* wave() const noexcept { return wave_; }
    WaveToolBar* waveToolBar() const noexcept { return waveToolBar_; }
    /// Settings key of the Follow state (View > Follow simulation).
    static constexpr const char* kFollowSettingKey = "wave/follow";
    ObjectsPane* objects() const noexcept { return objects_; }
    /// The List dock (`add list`, View > List).
    ListWindow* list() const noexcept { return list_; }
    /// Shows the List dock as a tab next to Source (where it is placed the first time) and raises it.
    void showListDock();
    /// The Memory dock (`add mem`, View > Memory, Objects > Open in Memory window).
    MemoryWindow* memory() const noexcept { return memory_; }
    /// Shows the Memory dock as a tab next to Source and raises it; with a path that memory is selected
    /// (false if there is no such memory).
    bool showMemoryDock(const QString& path = {});
    /// Simulation > Record memories: start simulations with arrays in the waveform (vsim -lvdarrays). Kept in
    /// the settings (kRecordArraysSettingKey); applies from the next start or restart.
    void setRecordMemories(bool on);
    bool recordMemories() const noexcept { return recordArrays_; }
    static constexpr const char* kRecordArraysSettingKey = "simulation/recordArrays";
    TranscriptWidget* transcript() const noexcept { return transcript_; }

    // ---- code coverage (Coverage dock; docs/tutorial.md, "Code coverage") ----------------------------
    CoveragePane* coverage() const noexcept { return coverage_; }
    /// Simulation > Collect coverage: simulations are elaborated with coverage of every kind (nvc --cover)
    /// into <top>.ncdb in the current directory (the data folder when that is not writable). Kept in the
    /// settings (kCoverageSettingKey); applies from the next start or restart. When the simulation reaches
    /// its end, and when the session ends, the counts are loaded into the Coverage dock.
    void setCoverageEnabled(bool on);
    bool coverageEnabled() const noexcept { return coverageEnabled_; }
    static constexpr const char* kCoverageSettingKey = "simulation/coverage";
    /// The database the running (or last) simulation writes; empty when it collects no coverage.
    QString sessionCoverageFile() const { return sessionCoverFile_; }
    /// The nvc arguments the running (or last) simulation was started with.
    QStringList sessionArguments() const { return sessionArgs_; }
    /// Loads coverage databases (.ncdb; several are merged) with `nvc --cover-export` and shows them in the
    /// Coverage dock. Runs in the background: coverageLoaded() follows, or an error in the transcript.
    /// Returns false if it could not start (missing file, another coverage command still running).
    bool openCoverage(const QStringList& databases);
    QStringList coverageDatabases() const { return coverageDbs_; }
    /// Simulation > Refresh coverage: `coverage save -json` in the running simulation, then the saved counts
    /// are loaded into the Coverage dock (coverageLoaded() follows). Counts add up over restarts. False
    /// without a simulation that collects coverage.
    bool refreshCoverage();
    /// Merges the loaded databases into one file (`nvc --cover-merge`).
    bool saveMergedCoverage(const QString& outFile);
    /// Writes nvc's HTML report of the loaded databases into `dir` and opens its index.html in the browser
    /// (not with dialogs switched off). coverageReportWritten() follows.
    bool exportCoverageReport(const QString& dir);
    /// Waits for the coverage command in progress (tests, headless runs). False on timeout.
    bool waitForCoverageTool(int timeoutMs = 60000);
    /// Shows the Coverage dock as a tab next to the Transcript and raises it.
    void showCoverageDock();
    NvcSession& session() noexcept { return session_; }
    const lvd::wave::WaveStore* store() const noexcept { return store_.get(); }
    QString runLengthText() const;
    void setRunLength(const QString& text);

    /// Dialogs make no sense in automated runs; tests and --screenshot switch them off.
    void setDialogsEnabled(bool enabled);
    QString lastError() const { return lastError_; }

    // ---- projects (Project menu, Project / Problems / Regression docks; docs/projects.md) ------------
    ProjectSession* project() const noexcept { return project_; }
    ProjectPane* projectPane() const noexcept { return projectPane_; }
    ProblemsPane* problems() const noexcept { return problems_; }
    RegressionPane* regressionPane() const noexcept { return regressionPane_; }
    /// Opens a project file: the working directory becomes the project directory (relative paths of the
    /// project, its libraries and the simulator's file names are relative to it) and the Project dock shows
    /// it. False (reason in lastError()) on failure.
    bool openProject(const QString& path);
    /// Builds a project from a compile script (vlib/vmap/vcom/vlog/vsim lines and lvdsim.ini) and opens it.
    bool importProject(const QString& script, const QString& projectFile);
    /// Analyses what is out of date, then starts the test in the simulator (as Simulate in the Project dock).
    bool simulateTest(const QString& test);
    /// Shows the Problems or Regression dock as a tab next to the Transcript and raises it.
    void showBottomDock(const QString& name);
    QStringList recentProjects() const;
    static constexpr const char* kRecentProjectsKey = "project/recent";
    static constexpr int kRecentProjectsLimit = 8;

    LibraryPane* library() const noexcept { return library_; }
    StructurePane* structure() const noexcept { return structure_; }

    // ---- console (Transcript menu)
    // -------------------------------------------------------------------------
    /// Runs a script with `do` as if typed in the console (it goes into the command history and the recent do
    /// files).
    void runDoFile(const QString& path);
    /// Transcript > Recent do files: the scripts run with `do` (typed, dropped or chosen), newest first.
    QStringList recentDoFiles() const;
    static constexpr int kRecentDoFilesLimit = 10;
    static constexpr const char* kRecentDoFilesKey = "console/recentDoFiles";
    /// Settings: how many commands the history keeps and how many lines the transcript keeps.
    static constexpr const char* kHistoryLimitKey = "console/historyLimit";
    static constexpr const char* kTranscriptLinesKey = "console/maxLines";
    /// The command that asks the shell for its command list (answered with an "@@lvd commands ..." notice).
    static QString shellCommandsQuery();

    /// Window layout, run length and the last simulation arguments are kept in an ini file between runs
    /// (the settings file given to the constructor, or the user's default location).
    void saveSession();
    void restoreSession();
    QString settingsFile() const { return settingsFile_; }
    /// The waveform renderer choice in force (menu View > Waveform renderer). With persist the choice is
    /// stored in the settings (render/backend) for the next start; it beats LVDSIM_RENDERER until then.
    void setRenderPolicy(RenderPolicy policy, bool persist);
    RenderPolicy renderPolicy() const noexcept { return renderPolicy_; }
    QStringList previousArguments() const {
        return previousArgs_;
    } ///< arguments of the last session, for the start dialog

    /// Rescans the libraries (lvdsim.ini mappings, library directories below the current directory and the
    /// libraries in the library folders).
    void refreshLibraries();

    /// Library folders (for example the output of Tools > Compile libraries): every library found directly in
    /// one of them is given to each simulation as `-L name` and shown in the Library dock. Kept in the
    /// settings.
    QStringList libraryFolders() const { return libraryFolders_; }
    void setLibraryFolders(const QStringList& folders);
    void addLibraryFolder(const QString& folder);
    void removeLibraryFolder(const QString& folder);
    /// Tools > Compile libraries (modeless; the same dialog is raised if already open).
    CompileLibsDialog* showCompileLibraries();
    /// The context used to plan a simulation: the environment's plus the mappings of the library folders.
    /// `skip` names libraries that must not get a mapping or an -L (the user's own -L words).
    lvd::tools::Context planContext(QStringList* extraArgs = nullptr, const QStringList& userArgs = {}) const;

    /// The wave rows as a .do script of `add wave` commands, and the way back. Loading needs a running
    /// or finished simulation (the signals must be known). Errors are shown in the transcript.
    bool saveWaveFormat(const QString& path);
    bool loadWaveFormat(const QString& path);

    /// Opens a finished FST file as a dataset next to the current simulation (File > Open dataset...). Its
    /// signals are named `name:/path` (name: the file name without extension, made a valid name, unless
    /// given). A file the running simulation is writing is refused, as is a name already in use. Times are
    /// converted to the unit of the simulation when it differs. Returns false and sets lastError() on
    /// failure.
    bool openDataset(const QString& path, const QString& name = {});
    bool closeDataset(const QString& name);

    /// True once wave rows were moved, grouped, renamed, added or removed since the last save of a wave
    /// layout or wave format (or since the window was created).
    bool waveLayoutModified() const noexcept { return waveLayoutModified_; }
    /// Scripted wave edits for headless runs and tests (`lvdsim-gui --wave-op "..."`). Rows are named as the
    /// wave window shows them; several names are separated by commas:
    ///   select N,N  |  move-up [N,N] [steps]  |  move-down [N,N] [steps]  |  move-top [N,N]  |  move-bottom
    ///   [N,N] group N,N [label]  |  ungroup N  |  rename N new  |  move-to-group N,N GROUP|-  |  delete N,N
    ///   divider-above N [label]  |  divider-below N [label]  |  toggle N (expand or collapse a group)
    ///   edge next|prev [any|rising|falling]  |  find next|prev VALUE
    ///   [equals|not-equals|rising|falling|either] (in the Find Value dialog, on the selected rows) undo  |
    ///   redo  |  edit N (open the rename editor)  |  drag N,N TARGET FRACTION (hold a drag over row TARGET,
    ///   FRACTION 0..1 from its top to its bottom, without dropping)
    ///   expr NAME TEXT (add an expression row)  |  compare-with DATASET N,N [TOLERANCE]  |
    ///   compare-datasets REF TEST [SCOPE] [TOLERANCE] (REF, TEST: dataset names, "-" for the simulation)  |
    ///   difference next|prev
    /// Returns false (and sets lastError()) if a name is unknown or the edit was refused.
    bool applyWaveOp(const QString& op);

signals:
    void simulationStarted();
    /// A run or step reply was read (a breakpoint hit is `reply.breakpoint`).
    void stopped(const lvd::gui::RunReply& reply);
    void simulationFinished(int exitCode);
    /// The simulator restarted at time zero in the same process (the restart reply was read).
    void restarted();
    /// A process stopped at a source line (breakpoint or statement step); see lastStop().
    void stoppedAtLine();
    /// openCoverage() finished and the Coverage dock shows the result.
    void coverageLoaded();
    /// exportCoverageReport() finished; `index` is the report's start page (empty on failure).
    void coverageReportWritten(const QString& index);

protected:
    void closeEvent(QCloseEvent*) override;

private:
    /// The wait policy of the session: a Wait / Kill dialog, or the automatic answer when dialogs are off.
    NvcSession::WaitDecision askWaitOrKill(NvcSession::Unresponsive what, int waitedMs);
    void applyRenderDecision(const RenderDecision& d);
    void syncRenderer();
    void buildUi();
    void onNotice(const QStringList& words);
    void onReply(int commandIndex, const QString& json);
    void onRunReply(const QString& json);
    void onStopReply(const QString& json);
    void onRestartReply(const QString& json);
    void relaunchSimulation();
    void stampLibraries(const QStringList& nvcArgs);
    /// The stop state, the Source marker, the Call Stack and Locals; `knownFrames` empty asks for the stack.
    void enterStop(const StopReply& stop, const QVector<SourceFrame>& knownFrames);
    void probeStop();
    void leaveStop(); ///< the run went on (or ended): no stop marker, stack or locals
    void requestLocals();
    void showExecutionPoint();
    void onShellOutput(const QString& text);
    /// request() with a handler for a command that fails (no reply): called when the shell is idle again,
    /// with the error text it printed.
    int request(const QString& command, std::function<void(const QString&)> handler,
                std::function<void(const QString&)> onFailure);
    void sendLineBreakpoint(const QString& file, int line);
    void deleteKernelBreakpoint(int id);
    void lineBreakpointAdded(const QString& file, int line, const QString& json);
    void lineBreakpointsChanged(); ///< show and store the list
    void loadLineBreakpoints(const QStringList& vsimArgs);
    QString kernelFileName(const QString& absolutePath) const;
    QString resolveSourceFile(const QString& kernelFile) const;
    void onBusyChanged(bool busy);
    void onProcessSelected(const QString& file, int line);
    void setBreakpointList(QVector<BreakpointInfo> list);
    void resetDebugState();
    void onFinished(int exitCode);
    void onCommandEntered(const QString& command);
    /// A command typed while a run or step is still going waits in the shell's input: say so once, if the
    /// wait is noticeable (it looked like a hang).
    void noteWaitingCommand();
    void runExternalTool(const QString& tool, const QStringList& args);
    void addRecentDoFile(const QString& file);
    void runDoFileDialog();
    void buildTranscriptMenu();
    void updateStatus();
    void report(const QString& error);
    void saveLayout();
    void saveWaveFormatDialog();
    void loadWaveFormatDialog();
    void loadLayout();
    void startDialog();
    void cleanupTemp();
    void openDatasetDialog();
    // coverage (sim_window_coverage.cpp)
    void createCoverageDock();
    void addCoverageFileActions(QMenu* file);
    void addCoverageSimulationActions(QMenu* sim);
    void setupCoverageTool();
    /// The database planVsim chose for this session (relative to the working directory), empty without
    /// coverage.
    void coverageSessionPlanned(const QString& databaseFile);
    /// The vsim arguments with the coverage options of this window: -coverage for Simulation > Collect
    /// coverage, and an absolute -lvdcoverfile (in a writable folder) for every simulation that collects it.
    QStringList coveragePlanArgs(QStringList args, const lvd::tools::Context& ctx);
    /// A source file named in a coverage database: relative names are relative to where nvc analysed them,
    /// usually the folder of the database.
    QString coverageSourceFile(const QString& name) const;
    /// Waits for the coverage command in progress, if any (an export must read a database before it is
    /// written again).
    void finishCoverageCommand();
    void coverageSessionStarted();
    void coverageSessionFinished();
    void applyCoverageToSource();
    void openCoverageDialog();
    void saveMergedCoverageDialog();
    void exportCoverageReportDialog();
    // projects (sim_window_project.cpp)
    void createProjectDocks();
    void addProjectMenu();
    void addRecentProject(const QString& path);
    bool maybeSaveProject();
    void newProjectDialog();
    void openProjectDialog();
    void importProjectDialog();
    void updateProjectTitle();
    // source navigation (sim_window_source.cpp)
    void connectSourceNavigation();
    void probeDeclarations();
    void showDeclaration(const QString& shellPath, const DescribedObject& o, bool region, bool instantiation);

    lvd::tools::Environment env_;
    NvcSession session_;
    ProjectSession* project_ = nullptr;
    ProjectPane* projectPane_ = nullptr;
    ProblemsPane* problems_ = nullptr;
    RegressionPane* regressionPane_ = nullptr;
    QMenu* recentProjectsMenu_ = nullptr;
    QString pendingTest_; ///< started once the analysis before it succeeded
    bool restartAfterCompile_ = false;
    bool libraryStampPending_ = false;
    bool restartUnsupported_ = false;         ///< the kernel answered "restart command is not supported"
    QHash<QString, QDateTime> libraryStamps_; ///< _index files of the libraries at the start
    QTimer pollTimer_;
    std::shared_ptr<lvd::wave::WaveStore> store_;
    std::unique_ptr<lvd::wave::LiveReader> reader_;
    bool storeAttached_ = false;
    QStringList lastArgs_;
    QString tempDir_;
    QString liveFile_;
    QStringList tempFiles_;
    QStringList writtenFiles_; ///< waveform files the running simulation writes (never opened as datasets)
    bool dialogs_ = true;
    LibraryPane* library_ = nullptr;
    StructurePane* structure_ = nullptr;
    QString settingsFile_;
    WaveToolBar* waveToolBar_ = nullptr;
    QStringList libraryFolders_;
    QPointer<CompileLibsDialog> compileDialog_;
    QStringList previousArgs_;
    bool finished_ = false;
    QString lastError_;
    bool waveLayoutModified_ = false;

    WaveWidget* wave_ = nullptr;
    ObjectsPane* objects_ = nullptr;
    ListWindow* list_ = nullptr;
    MemoryWindow* memory_ = nullptr;
    QAction* recordArraysAction_ = nullptr;
    bool recordArrays_ = false;
    CoveragePane* coverage_ = nullptr;
    NvcTool* coverageTool_ = nullptr;
    QAction* coverageAction_ = nullptr;
    bool coverageEnabled_ = false;
    bool coverageJson_ = true; ///< read databases through the JSON export (false after "unknown format")
    QStringList coverageDbs_;  ///< databases shown in the Coverage dock
    QStringList coverageDirs_; ///< their folders (source names in them are relative to these)
    /// Copies of databases shown in the dock that a new simulation writes again (removed with the window).
    std::unique_ptr<QTemporaryDir> coverageSnapshots_;
    QString sessionCoverFile_; ///< database of the running simulation, empty without coverage
    QDateTime
        sessionCoverTime_; ///< its time stamp when the shell first showed its prompt (after elaboration)
    bool sessionCoverStamped_ = false;
    QStringList sessionArgs_;
    QString sessionDir_; ///< working directory when the simulation started
    DeclarationLocator locator_;
    bool locatorProbed_ = false;     ///< the session's first prompt asked whether locations are reported
    NvcTool* libraryTool_ = nullptr; ///< nvc --print-deps for the Library pane
    std::vector<lvd::tools::LibraryInfo> libraryInfos_; ///< what the Library pane shows
    TranscriptWidget* transcript_ = nullptr;
    QLineEdit* runLength_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QAction* runAction_ = nullptr;
    QAction* runAllAction_ = nullptr;
    QAction* restartAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* breakAction_ = nullptr;
    QAction* stepDeltaAction_ = nullptr;
    QAction* stepTimeAction_ = nullptr;
    QAction* continueAction_ = nullptr;
    QAction* stepIntoAction_ = nullptr;
    QAction* stepOverAction_ = nullptr;
    QAction* stepOutAction_ = nullptr;

    CallStackPane* stack_ = nullptr;
    LocalsPane* locals_ = nullptr;
    LineBreakpointList lineBps_;
    QString lineBpKey_;    ///< settings key the list belongs to
    QStringList savedBps_; ///< what the settings hold for lineBpKey_
    QSet<int> deletedIds_; ///< kernel ids the GUI deleted in this simulation
    bool stoppedAtSource_ = false;
    bool resumeInFlight_ = false; ///< a run or step was sent while stopped; its reply has not come
    StopReply lastStop_;
    int stopSerial_ = 0;
    QVector<SourceFrame> frames_;
    int selectedFrame_ = 0;
    QString lastShellError_;
    QString outputTail_; ///< an unfinished output line
    QHash<int, std::function<void(const QString&)>> failures_;

    ProcessesPane* processes_ = nullptr;
    SourcePane* source_ = nullptr;
    BreakpointPane* breakpoints_ = nullptr;
    QLabel* stopLabel_ = nullptr;
    QLabel* rendererLabel_ = nullptr;
    QAction* rendererAutoAction_ = nullptr;
    QAction* rendererCpuAction_ = nullptr;
    QAction* rendererGpuAction_ = nullptr;
    RenderPolicy renderPolicy_ = RenderPolicy::Auto;
    QString startupReason_; ///< why the GPU was not even requested (headless platform)
    bool fallbackNoted_ = false;
    QHash<int, std::function<void(const QString&)>> pending_;
    QVector<BreakpointInfo> bpList_;
    RunStatus lastStatus_ = RunStatus::Paused;
    bool haveStop_ = false;
    QString stopText_;
    bool dirty_ = false;        ///< something may have changed that no -json reply told us about
    bool scriptRan_ = false;    ///< a script ran since the last idle: it may have stopped at a source line
    QTimer waitingNoteTimer_;   ///< started by a command typed while busy
    bool waitingNoted_ = false; ///< the note was shown in this busy period
};

} // namespace lvd::gui
