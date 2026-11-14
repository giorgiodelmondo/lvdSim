// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>

namespace lvd::gui {

/// Runs one-off nvc commands (coverage export, merge, report) as child processes, one at a time, without
/// blocking the GUI. Like NvcSession it never links the simulator: everything goes through the nvc binary.
class NvcTool : public QObject {
    Q_OBJECT
public:
    struct Setup {
        QString nvc;             ///< nvc executable
        QStringList globalArgs;  ///< put before the command's own arguments (for example -L <dir>)
        QStringList pathPrepend; ///< directories put in front of PATH (runtime DLLs)
    };
    /// exitCode is -1 when the program could not be started; output holds stdout and stderr.
    using Done = std::function<void(int exitCode, const QString& output)>;

    explicit NvcTool(QObject* parent = nullptr);
    ~NvcTool() override;

    void setSetup(const Setup& s) { setup_ = s; }
    const Setup& setup() const noexcept { return setup_; }

    /// Starts nvc with the arguments; `done` is called from the event loop when it ends. Returns false (and
    /// calls nothing) if another command is still running.
    bool start(const QStringList& args, Done done);
    bool busy() const noexcept { return process_ != nullptr; }
    /// Spins the event loop until the running command ended (for tests and headless runs). False on timeout.
    bool waitForIdle(int timeoutMs = 60000);
    /// The full command line of the last start, for the transcript.
    QString lastCommandLine() const { return lastCommand_; }

signals:
    void busyChanged(bool busy);

private:
    Setup setup_;
    QPointer<QProcess> process_;
    QString lastCommand_;
};

} // namespace lvd::gui
