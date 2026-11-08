// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/nvc_session.hpp"

#include <QDir>
#include <QProcessEnvironment>

namespace lvd::gui {

namespace {

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
    stop(1000);
}

bool NvcSession::start(const Config& config) {
    stop(1000);
    filter_ = OutputFilter();
    sent_ = 0;
    prompts_ = 0;
    lastBusy_ = true; // busy until the first prompt

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!config.pathPrepend.isEmpty()) {
        const QString path = env.value("PATH");
        env.insert("PATH", config.pathPrepend.join(QDir::listSeparator()) + QDir::listSeparator() + path);
    }
    process_.setProcessEnvironment(env);
    if (!config.workingDir.isEmpty())
        process_.setWorkingDirectory(config.workingDir);

    process_.start(config.nvc, config.args);
    if (!process_.waitForStarted(5000)) {
        emit failed(tr("Cannot start %1: %2").arg(config.nvc, process_.errorString()));
        return false;
    }

    // The shell queues these until it is ready. The first tells the command layer that a GUI is attached.
    if (!config.startupScripts.isEmpty()) {
        send("namespace eval ::lvd { variable gui 1 }");
        for (const QString& s : config.startupScripts)
            send("source " + tclPath(s));
    }
    emit busyChanged(true);
    return true;
}

int NvcSession::send(const QString& command) {
    if (!running())
        return -1;
    process_.write(command.toUtf8() + "\n");
    const int index = sent_++;
    updateBusy();
    return index;
}

void NvcSession::stop(int timeoutMs) {
    if (!running())
        return;
    process_.write("exit\n");
    if (!process_.waitForFinished(timeoutMs)) {
        process_.kill();
        process_.waitForFinished(2000);
    }
}

void NvcSession::onReadyRead() {
    const FilteredOutput out = filter_.feed(process_.readAll());
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
    emit finished(exitCode);
    if (lastBusy_) {
        lastBusy_ = false;
        emit busyChanged(false);
    }
}

void NvcSession::updateBusy() {
    const bool b = running() && busy();
    if (b != lastBusy_) {
        lastBusy_ = b;
        emit busyChanged(b);
    }
}

} // namespace lvd::gui
