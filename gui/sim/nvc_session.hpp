// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/output_filter.hpp"

#include <lvdwave/result.hpp>

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace lvd::gui {

/// An interactive `nvc -i` shell running as a child process. Commands go to its standard input, its
/// output is cleaned up by OutputFilter and reported through signals. The simulator never runs inside
/// the GUI process: if it crashes or hangs the GUI stays usable.
class NvcSession : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString nvc;             ///< nvc executable
        QStringList args;        ///< arguments, normally ending in -i --format=live --wave=...
        QStringList pathPrepend; ///< directories put in front of PATH (runtime DLLs)
        QString workingDir;
        QStringList startupScripts; ///< sourced once the shell is up, after the GUI hooks are enabled
        /// Commands sent before the startup scripts (for example the breakpoints the GUI keeps), so that they
        /// are in force when a script runs. Their numbers are in startupCommandIndices().
        QStringList commands;
    };

    explicit NvcSession(QObject* parent = nullptr);
    ~NvcSession() override;

    /// Starts the process. Returns false (and emits failed) if it cannot be started.
    bool start(const Config& config);
    bool running() const noexcept { return process_.state() != QProcess::NotRunning; }

    /// True from sending a command until the shell shows its prompt again (for example during `run`).
    bool busy() const noexcept { return sent_ + 1 > prompts_; }
    /// Commands sent so far, including the startup ones.
    int commandsSent() const noexcept { return sent_; }
    /// The command numbers of Config::commands of the last start, in order.
    QVector<int> startupCommandIndices() const { return startupIndices_; }

    /// Send one command line. Several may be sent in a row; the shell executes them in order. Returns the
    /// number of the command (0 for the first one sent, startup commands included), which identifies its
    /// JSON reply in `reply`; -1 if no shell is running.
    int send(const QString& command);

    /// What the simulator did not do in time: exit after `exit` (stop()), or end its run after interrupt().
    enum class Unresponsive { Exit, Break };
    enum class WaitDecision { Wait, Kill };
    /// Asked by stop() each time its timeout passes (`waitedMs` since `exit` was sent; Wait waits one more
    /// timeout), and by the GUI when Break is pressed again while the first one is pending. Without a policy
    /// the answer is Kill for Exit and Wait for Break, so nothing blocks in tests and headless runs.
    using WaitPolicy = std::function<WaitDecision(Unresponsive what, int waitedMs)>;
    void setWaitPolicy(WaitPolicy policy) { waitPolicy_ = std::move(policy); }
    WaitDecision askWaitPolicy(Unresponsive what, int waitedMs) const;

    /// Ask the shell to exit; a run in progress is interrupted first. When it has not exited within the
    /// timeout the wait policy decides: wait another period, or kill it.
    void stop(int timeoutMs = 3000);
    /// Ends the process at once (coverage counts and other data written at exit are lost).
    void kill();

    /// Asks the simulator to end the run in progress before its next time step: the run (or step) then
    /// replies with status "interrupted" and the commands sent meanwhile run. Delivered as SIGINT, on Windows
    /// as CTRL_BREAK_EVENT to the console of the child (which runs in its own process group). The shell
    /// ignores it at the prompt and while a process is stopped at a source line. Fails, with the reason, when
    /// it cannot be delivered or the shell is not up yet (before the first prompt nvc would end).
    lvd::Result<bool> interrupt();
    /// A run is going and the shell has shown its prompt at least once: interrupt() can be used.
    bool canInterrupt() const noexcept { return running() && prompts_ > 0 && busy(); }
    /// interrupt() was delivered and the shell has not shown its prompt since; elapsed time in ms.
    bool interruptPending() const noexcept { return interruptPending_; }
    qint64 interruptPendingMs() const { return interruptPending_ ? interruptClock_.elapsed() : 0; }

signals:
    void output(const QString& text); ///< transcript text, may end in the prompt
    void
    notice(const QStringList& words); ///< a GUI notice from the command layer, e.g. {"add","wave","-r","/*"}
    /// One line of JSON printed by a `-json` command (not part of `output`). `commandIndex` is the value
    /// send() returned for the command that printed it. Emitted in order with `output`.
    void reply(int commandIndex, const QString& json);
    void promptShown(); ///< the shell is ready for input
    void busyChanged(bool busy);
    void finished(int exitCode);
    void failed(const QString& message);

private:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void updateBusy();
    void trace(const char* direction, const QByteArray& data);

    QProcess process_;
    OutputFilter filter_;
    int sent_ = 0;
    int prompts_ = 0;
    bool lastBusy_ = false;
    QVector<int> startupIndices_;
    std::unique_ptr<QFile> trace_; ///< raw stdin/stdout log when LVDSIM_SHELL_TRACE names a file
    QElapsedTimer traceClock_;
    WaitPolicy waitPolicy_;
    bool interruptPending_ = false;
    QElapsedTimer interruptClock_;
    bool sharesConsole_ = false; ///< Windows: the child is attached to this process's console
};

} // namespace lvd::gui
