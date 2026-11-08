// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/output_filter.hpp"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

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

    /// Send one command line. Several may be sent in a row; the shell executes them in order. Returns the
    /// number of the command (0 for the first one sent, startup commands included), which identifies its
    /// JSON reply in `reply`; -1 if no shell is running.
    int send(const QString& command);

    /// Ask the shell to exit, and kill it if it has not within the timeout.
    void stop(int timeoutMs = 3000);

signals:
    void output(const QString& text); ///< transcript text, may end in the prompt
    void
    notice(const QStringList& words); ///< a GUI notice from the command layer, e.g. {"add","wave","-r","/*"}
    /// One line of JSON printed by a `-json` command (not part of `output`). `commandIndex` is the value
    /// send() returned for the command that printed it. Emitted in order with `output`.
    void reply(int commandIndex, const QString& json);
    void promptShown();               ///< the shell is ready for input
    void busyChanged(bool busy);
    void finished(int exitCode);
    void failed(const QString& message);

private:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void updateBusy();

    QProcess process_;
    OutputFilter filter_;
    int sent_ = 0;
    int prompts_ = 0;
    bool lastBusy_ = false;
};

} // namespace lvd::gui
