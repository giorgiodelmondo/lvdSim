// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/nvc_session.hpp"
#include "sim/parsers/json_replies.hpp"
#include "wave/render_policy.hpp"

#include <lvdwave/live_reader.hpp>
#include <lvdwave/store.hpp>

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <environment.hpp>
#include <functional>
#include <memory>

class QAction;
class QLabel;
class QLineEdit;
class QProcess;
class QMenu;

namespace lvd::gui {

class BreakpointPane;
class CompileLibsDialog;
class LibraryPane;
class ObjectsPane;
class ProcessesPane;
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
    /// above Objects on the left, Transcript and Processes | Breakpoints at the bottom, Source on the right.
    void applyDefaultLayout();

    /// Start a simulation. `vsimArgs` are vsim options without -c, for example
    /// {"-do", "add wave -r /*; run 100 ns", "work.top"}. Returns false if they are invalid (the reason
    /// is shown in the transcript). A running simulation is stopped first; the wave rows are kept.
    bool startSimulation(const QStringList& vsimArgs);
    void restartSimulation();
    void stopSimulation();
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
    /// Show (and raise) or hide a dock by name: library, structure (or sim), objects, transcript, processes,
    /// breakpoints, source. Used by the screenshot options of lvdsim-gui. Returns false for an unknown name.
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
    TranscriptWidget* transcript() const noexcept { return transcript_; }
    NvcSession& session() noexcept { return session_; }
    const lvd::wave::WaveStore* store() const noexcept { return store_.get(); }
    QString runLengthText() const;
    void setRunLength(const QString& text);

    /// Dialogs make no sense in automated runs; tests and --screenshot switch them off.
    void setDialogsEnabled(bool enabled) noexcept { dialogs_ = enabled; }
    QString lastError() const { return lastError_; }

    LibraryPane* library() const noexcept { return library_; }
    StructurePane* structure() const noexcept { return structure_; }

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

    /// True once wave rows were moved, grouped, renamed, added or removed since the last save of a wave layout
    /// or wave format (or since the window was created).
    bool waveLayoutModified() const noexcept { return waveLayoutModified_; }
    /// Scripted wave edits for headless runs and tests (`lvdsim-gui --wave-op "..."`). Rows are named as the
    /// wave window shows them; several names are separated by commas:
    ///   select N,N  |  move-up [N,N] [steps]  |  move-down [N,N] [steps]  |  move-top [N,N]  |  move-bottom [N,N]
    ///   group N,N [label]  |  ungroup N  |  rename N new  |  move-to-group N,N GROUP|-  |  delete N,N
    ///   divider-above N [label]  |  divider-below N [label]  |  toggle N (expand or collapse a group)
    ///   undo  |  redo  |  edit N (open the rename editor)  |  drag N,N TARGET FRACTION (hold a drag over row TARGET,
    ///   FRACTION 0..1 from its top to its bottom, without dropping)
    /// Returns false (and sets lastError()) if a name is unknown or the edit was refused.
    bool applyWaveOp(const QString& op);

signals:
    void simulationStarted();
    /// A run or step reply was read (a breakpoint hit is `reply.breakpoint`).
    void stopped(const lvd::gui::RunReply& reply);
    void simulationFinished(int exitCode);

protected:
    void closeEvent(QCloseEvent*) override;

private:
    void applyRenderDecision(const RenderDecision& d);
    void syncRenderer();
    void buildUi();
    void onNotice(const QStringList& words);
    void onReply(int commandIndex, const QString& json);
    void onRunReply(const QString& json);
    void onBusyChanged(bool busy);
    void onProcessSelected(const QString& file, int line);
    void setBreakpointList(QVector<BreakpointInfo> list);
    void resetDebugState();
    void onFinished(int exitCode);
    void onCommandEntered(const QString& command);
    void runExternalTool(const QString& tool, const QStringList& args);
    void updateStatus();
    void report(const QString& error);
    void saveLayout();
    void saveWaveFormatDialog();
    void loadWaveFormatDialog();
    void loadLayout();
    void startDialog();
    void cleanupTemp();

    lvd::tools::Environment env_;
    NvcSession session_;
    QTimer pollTimer_;
    std::shared_ptr<lvd::wave::WaveStore> store_;
    std::unique_ptr<lvd::wave::LiveReader> reader_;
    bool storeAttached_ = false;
    QStringList lastArgs_;
    QString tempDir_;
    QString liveFile_;
    QStringList tempFiles_;
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
    TranscriptWidget* transcript_ = nullptr;
    QLineEdit* runLength_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QAction* runAction_ = nullptr;
    QAction* runAllAction_ = nullptr;
    QAction* restartAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* stepDeltaAction_ = nullptr;
    QAction* stepTimeAction_ = nullptr;

    ProcessesPane* processes_ = nullptr;
    SourcePane* source_ = nullptr;
    BreakpointPane* breakpoints_ = nullptr;
    QLabel* stopLabel_ = nullptr;
    QLabel* rendererLabel_ = nullptr;
    QAction* rendererAutoAction_ = nullptr;
    QAction* rendererCpuAction_ = nullptr;
    QAction* rendererGpuAction_ = nullptr;
    RenderPolicy renderPolicy_ = RenderPolicy::Auto;
    QString startupReason_;  ///< why the GPU was not even requested (headless platform)
    bool fallbackNoted_ = false;
    QHash<int, std::function<void(const QString&)>> pending_;
    QVector<BreakpointInfo> bpList_;
    RunStatus lastStatus_ = RunStatus::Paused;
    bool haveStop_ = false;
    QString stopText_;
    bool dirty_ = false; ///< something may have changed that no -json reply told us about
};

} // namespace lvd::gui
