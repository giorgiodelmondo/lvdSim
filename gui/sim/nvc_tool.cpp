// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/nvc_tool.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcessEnvironment>
#include <QThread>

namespace lvd::gui {

NvcTool::NvcTool(QObject* parent)
    : QObject(parent) {}

NvcTool::~NvcTool() {
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(2000);
        delete process_;
    }
}

bool NvcTool::start(const QStringList& args, Done done) {
    if (process_)
        return false;
    auto* p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!setup_.pathPrepend.isEmpty())
        env.insert(QStringLiteral("PATH"), setup_.pathPrepend.join(QDir::listSeparator()) +
                                               QDir::listSeparator() + env.value(QStringLiteral("PATH")));
    p->setProcessEnvironment(env);
    const QStringList full = setup_.globalArgs + args;
    lastCommand_ = (QStringList{setup_.nvc} + full).join(QLatin1Char(' '));
    process_ = p;
    auto finish = [this, p, done](int code) {
        if (process_ != p)
            return;
        const QString out = QString::fromLocal8Bit(p->readAll());
        process_ = nullptr;
        p->deleteLater();
        emit busyChanged(false);
        if (done)
            done(code, out);
    };
    connect(p, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit ? code : (code != 0 ? code : 1));
    });
    connect(p, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) // no finished() follows
            finish(-1);
    });
    emit busyChanged(true);
    p->start(setup_.nvc, full);
    return true;
}

bool NvcTool::waitForIdle(int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (process_ && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (process_)
            QThread::msleep(5);
    }
    return !process_;
}

} // namespace lvd::gui
