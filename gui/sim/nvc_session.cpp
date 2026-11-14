// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/nvc_session.hpp"

#include <QDir>
#include <QProcessEnvironment>

#include <cerrno>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#endif

namespace lvd::gui {

namespace {

#ifdef Q_OS_WIN
// True when this process is attached to a console, also one without a window.
bool attachedToConsole() {
    DWORD ids[1];
    return GetConsoleProcessList(ids, 1) != 0;
}

std::string windowsError(const char* what) {
    return std::string(what) + " (Windows error " + std::to_string(GetLastError()) + ")";
}
#endif

// A path as a Tcl word: forward slashes, in braces so that spaces are harmless.
QString tclPath(const QString& path) {
    return "{" + QDir::fromNativeSeparators(path) + "}";
}

} // namespace

NvcSession::NvcSession(QObject* parent)
    : QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyRead, this, &NvcSession::onReadyRead);
    connect(&process_, &QProcess::finished, this, &NvcSession::onFinished);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit failed(tr("Cannot start %1: %2").arg(process_.program(), process_.errorString()));
    });
}

NvcSession::~NvcSession() {
    waitPolicy_ = {}; // it may belong to an object that is being destroyed
    stop(1000);
}

bool NvcSession::start(const Config& config) {
    stop(1000);
    filter_ = OutputFilter();
    sent_ = 0;
    prompts_ = 0;
    lastBusy_ = true; // busy until the first prompt
    interruptPending_ = false;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!config.pathPrepend.isEmpty()) {
        const QString path = env.value("PATH");
        env.insert("PATH", config.pathPrepend.join(QDir::listSeparator()) + QDir::listSeparator() + path);
    }
    process_.setProcessEnvironment(env);
    // LVDSIM_SHELL_TRACE=<file>: every byte written to and read from the shell, with times (for protocol
    // bugs).
    trace_.reset();
    if (const QString traceFile = qEnvironmentVariable("LVDSIM_SHELL_TRACE"); !traceFile.isEmpty()) {
        trace_ = std::make_unique<QFile>(traceFile);
        if (!trace_->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered))
            trace_.reset();
        traceClock_.start();
    }
    if (!config.workingDir.isEmpty())
        process_.setWorkingDirectory(config.workingDir);

#ifdef Q_OS_WIN
    // The child runs in its own process group, so that interrupt() can send a console break to it alone. It
    // shares the console of this process when there is one (a GUI started from a terminal, tests), else it
    // gets a new console without a window; interrupt() attaches to that one for a moment.
    sharesConsole_ = attachedToConsole();
    process_.setCreateProcessArgumentsModifier([share = sharesConsole_](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NEW_PROCESS_GROUP;
        if (share)
            a->flags &= ~static_cast<DWORD>(CREATE_NO_WINDOW);
        else
            a->flags |= CREATE_NO_WINDOW;
    });
#endif
    process_.start(config.nvc, config.args);
    if (!process_.waitForStarted(5000)) {
        emit failed(tr("Cannot start %1: %2").arg(config.nvc, process_.errorString()));
        return false;
    }

    // The shell queues these until it is ready. The first tells the command layer that a GUI is attached, and
    // stops Tcl from running an unknown command word as a program (`pause`, `more`, `cmd`): such a program
    // would read the shell's standard input, which is this session's command stream, and the shell would
    // never answer again.
    startupIndices_.clear();
    if (!config.startupScripts.isEmpty())
        send("namespace eval ::lvd { variable gui 1 }; namespace eval :: { variable auto_noexec 1 }");
    for (const QString& c : config.commands)
        startupIndices_.push_back(send(c));
    for (const QString& s : config.startupScripts)
        send("source " + tclPath(s));
    emit busyChanged(true);
    return true;
}

int NvcSession::send(const QString& command) {
    if (!running())
        return -1;
    const QByteArray line = command.toUtf8() + "\n";
    trace(">>", line);
    process_.write(line);
    const int index = sent_++;
    updateBusy();
    return index;
}

NvcSession::WaitDecision NvcSession::askWaitPolicy(Unresponsive what, int waitedMs) const {
    if (waitPolicy_)
        return waitPolicy_(what, waitedMs);
    return what == Unresponsive::Exit ? WaitDecision::Kill : WaitDecision::Wait;
}

void NvcSession::stop(int timeoutMs) {
    if (!running())
        return;
    // A run in progress reads `exit` only when it ends: end it first. Only once: a second break ends nvc.
    if (canInterrupt() && !interruptPending_)
        interrupt();
    trace(">>", "exit\n");
    process_.write("exit\n");
    QElapsedTimer waited;
    waited.start();
    while (!process_.waitForFinished(timeoutMs) && running()) {
        if (askWaitPolicy(Unresponsive::Exit, static_cast<int>(waited.elapsed())) == WaitDecision::Kill) {
            kill();
            break;
        }
    }
}

void NvcSession::kill() {
    if (!running())
        return;
    trace(">>", "<kill>\n");
    process_.kill();
    process_.waitForFinished(2000);
}

lvd::Result<bool> NvcSession::interrupt() {
    using R = lvd::Result<bool>;
    if (!running())
        return R::failure("no simulator is running");
    if (prompts_ == 0)
        return R::failure("the simulator is still starting");
    const qint64 pid = process_.processId();
#ifdef Q_OS_WIN
    const auto group = static_cast<DWORD>(pid); // the child leads its own process group
    if (sharesConsole_) {
        if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, group))
            return R::failure(windowsError("cannot send a console break"));
    } else {
        if (!AttachConsole(group))
            return R::failure(windowsError("cannot attach to the console of the simulator"));
        const bool sent = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, group) != 0;
        const std::string error = sent ? std::string() : windowsError("cannot send a console break");
        FreeConsole();
        if (!sent)
            return R::failure(error);
    }
#else
    if (::kill(static_cast<pid_t>(pid), SIGINT) != 0)
        return R::failure(std::string("cannot send SIGINT: ") + std::strerror(errno));
#endif
#ifdef Q_OS_WIN
    trace(">>", sharesConsole_ ? "<interrupt: CTRL_BREAK_EVENT on the shared console>"
                               : "<interrupt: CTRL_BREAK_EVENT, attached to the console of the child>");
#else
    trace(">>", "<interrupt: SIGINT>");
#endif
    interruptPending_ = busy(); // at the prompt the shell ignores it: nothing is pending
    interruptClock_.start();
    return true;
}

void NvcSession::trace(const char* direction, const QByteArray& data) {
    if (!trace_ || data.isEmpty())
        return;
    // One line per chunk: control bytes and line ends escaped, so that split prompts stay visible.
    QByteArray text;
    for (const char c : data) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '\\')
            text += "\\\\";
        else if (u < 0x20 || u == 0x7f)
            text += "\\x" + QByteArray::number(u, 16).rightJustified(2, '0');
        else
            text += c;
    }
    trace_->write(QByteArray::number(traceClock_.nsecsElapsed() / 1000) + " us " + direction + " " + text +
                  "\n");
}

void NvcSession::onReadyRead() {
    const QByteArray chunk = process_.readAll();
    trace("<<", chunk);
    const FilteredOutput out = filter_.feed(chunk);
    qsizetype done = 0;
    for (const JsonLine& r : out.replies) {
        if (r.textOffset > done)
            emit output(out.text.mid(done, r.textOffset - done));
        done = r.textOffset;
        emit reply(r.prompt - 1, r.text);
    }
    if (out.text.size() > done)
        emit output(out.text.mid(done));
    for (const QStringList& n : out.notices)
        emit notice(n);
    if (out.prompts > 0) {
        prompts_ += out.prompts;
        emit promptShown();
    }
    updateBusy();
}

void NvcSession::onFinished(int exitCode, QProcess::ExitStatus) {
    onReadyRead();
    interruptPending_ = false;
    emit finished(exitCode);
    if (lastBusy_) {
        lastBusy_ = false;
        emit busyChanged(false);
    }
}

void NvcSession::updateBusy() {
    const bool b = running() && busy();
    if (!b)
        interruptPending_ = false; // the run ended (interrupted or not) and the prompt is back
    if (b != lastBusy_) {
        lastBusy_ = b;
        emit busyChanged(b);
    }
}

} // namespace lvd::gui
