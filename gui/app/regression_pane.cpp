// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/regression_pane.hpp"

#include "app/project_pane.hpp"
#include "app/project_session.hpp"

#include <QAction>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>

namespace lvd::gui {

namespace regress = lvd::tools::regress;

RegressionPane::RegressionPane(ProjectSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session) {
    toolBar_ = new QToolBar(this);
    toolBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto add = [this](const char* name, const QString& text, const QString& tip, auto fn) {
        QAction* a = toolBar_->addAction(text);
        a->setObjectName(name);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    add("regressAll", tr("Run all"), tr("Run every test of the project (each seed once)"),
        [this] { session_->runRegression(); });
    add("regressSelected", tr("Run selected"), tr("Run the selected runs again"), [this] {
        QStringList ids;
        const auto* r = session_->regression();
        for (const QModelIndex& i : table_->selectionModel()->selectedRows())
            if (r && i.row() < static_cast<int>(r->jobs().size()))
                ids << QString::fromStdString(r->jobs()[static_cast<std::size_t>(i.row())].id);
        if (!ids.isEmpty())
            session_->runRegression(ids);
    });
    add("regressFailed", tr("Rerun failed"), tr("Run the runs that did not pass again"),
        [this] { session_->rerunFailed(); });
    add("regressStop", tr("Stop"), tr("End the running runs; the others are not started"),
        [this] { session_->cancelRegression(); });
    toolBar_->addSeparator();
    add("regressLog", tr("Log"), tr("Show the log of the selected run"), [this] {
        const int j = selectedJob();
        if (j >= 0)
            emit logRequested(
                QString::fromStdString(session_->regression()->jobs()[static_cast<std::size_t>(j)].logFile));
    });
    add("regressWave", tr("Wave"), tr("Open the waveform of the selected run as a dataset"), [this] {
        const int j = selectedJob();
        if (j < 0)
            return;
        const auto& job = session_->regression()->jobs()[static_cast<std::size_t>(j)];
        emit waveRequested(QString::fromStdString(job.waveFile), QString::fromStdString(job.id));
    });
    add("regressCoverage", tr("Coverage"), tr("Show the coverage merged over the runs in the Coverage dock"),
        [this] { emit coverageRequested(session_->mergedCoverage()); });
    toolBar_->addSeparator();
    toolBar_->addWidget(new QLabel(tr(" Jobs "), this));
    jobs_ = new QSpinBox(this);
    jobs_->setObjectName("jobs");
    jobs_->setRange(0, 256);
    jobs_->setSpecialValueText(tr("auto"));
    jobs_->setToolTip(
        tr("Runs at the same time; auto: the project's jobs setting or the number of processors (%1)")
            .arg(QThread::idealThreadCount()));
    jobs_->setValue(session_->jobs());
    connect(jobs_, &QSpinBox::valueChanged, this, [this](int v) { session_->setJobs(v); });
    toolBar_->addWidget(jobs_);

    summary_ = new QLabel(this);
    summary_->setObjectName("regressionSummary");
    summary_->setContentsMargins(4, 2, 4, 2);

    table_ = new QTableWidget(0, ColumnCount, this);
    table_->setHorizontalHeaderLabels(
        {tr("Test"), tr("Seed"), tr("Status"), tr("Time"), tr("Errors"), tr("Message")});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    for (int c = 0; c < ColMessage; ++c)
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (session_->regression() && row < static_cast<int>(session_->regression()->jobs().size()))
            emit logRequested(QString::fromStdString(
                session_->regression()->jobs()[static_cast<std::size_t>(row)].logFile));
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateActions(); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolBar_);
    layout->addWidget(summary_);
    layout->addWidget(table_);

    connect(session_, &ProjectSession::regressionStarted, this, [this](int) { rebuild(); });
    connect(session_, &ProjectSession::jobChanged, this, [this](int j) {
        updateRow(j);
        updateActions();
    });
    connect(session_, &ProjectSession::regressionFinished, this, [this](int, int) { updateActions(); });
    connect(session_, &ProjectSession::coverageMerged, this, [this](const QString&) { updateActions(); });
    connect(session_, &ProjectSession::projectChanged, this, [this] { updateActions(); });
    rebuild();
}

QAction* RegressionPane::action(const QString& objectName) const {
    for (QAction* a : toolBar_->actions())
        if (a->objectName() == objectName)
            return a;
    return nullptr;
}

int RegressionPane::selectedJob() const {
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty() || !session_->regression())
        return -1;
    const int r = rows.front().row();
    return r < static_cast<int>(session_->regression()->jobs().size()) ? r : -1;
}

void RegressionPane::rebuild() {
    table_->setRowCount(0);
    const regress::Runner* r = session_->regression();
    if (r) {
        table_->setRowCount(static_cast<int>(r->jobs().size()));
        for (int i = 0; i < table_->rowCount(); ++i) {
            for (int c = 0; c < ColumnCount; ++c)
                table_->setItem(i, c, new QTableWidgetItem);
            updateRow(i);
        }
    }
    updateActions();
}

void RegressionPane::updateRow(int i) {
    const regress::Runner* r = session_->regression();
    if (!r || i < 0 || i >= table_->rowCount() || i >= static_cast<int>(r->jobs().size()))
        return;
    const regress::Job& j = r->jobs()[static_cast<std::size_t>(i)];
    const regress::Outcome& o = r->outcomes()[static_cast<std::size_t>(i)];
    table_->item(i, ColTest)->setText(QString::fromStdString(j.test));
    table_->item(i, ColTest)->setToolTip(QString::fromStdString(j.top));
    table_->item(i, ColSeed)->setText(j.seed ? QString::number(*j.seed) : QString());
    StatusIcon icon = StatusIcon::Pending;
    switch (o.status) {
    case regress::Status::Passed:
        icon = StatusIcon::Ok;
        break;
    case regress::Status::Failed:
    case regress::Status::NotStarted:
        icon = StatusIcon::Failed;
        break;
    case regress::Status::TimedOut:
        icon = StatusIcon::TimedOut;
        break;
    case regress::Status::Running:
        icon = StatusIcon::Busy;
        break;
    default:
        break;
    }
    table_->item(i, ColStatus)->setIcon(statusIcon(icon));
    table_->item(i, ColStatus)->setText(QString::fromLatin1(regress::statusName(o.status)));
    const bool done = regress::isDone(o.status);
    table_->item(i, ColTime)
        ->setText(done && o.status != regress::Status::Cancelled ? QString::number(o.seconds, 'f', 2) + " s"
                                                                 : QString());
    table_->item(i, ColErrors)->setText(done ? QString::number(o.counts.problems()) : QString());
    QString msg = done ? QString::fromStdString(o.message) : QString();
    if (!o.firstProblem.empty())
        msg += " - " + QString::fromStdString(o.firstProblem);
    table_->item(i, ColMessage)->setText(msg);
    table_->item(i, ColMessage)->setToolTip(QString::fromStdString(j.logFile));
}

QString RegressionPane::summaryText() const {
    return summary_->text();
}

void RegressionPane::updateActions() {
    const regress::Runner* r = session_->regression();
    const bool running = session_->regressionRunning();
    int passed = 0, failed = 0, done = 0;
    if (r)
        for (const regress::Outcome& o : r->outcomes()) {
            done += regress::isDone(o.status) ? 1 : 0;
            passed += o.status == regress::Status::Passed ? 1 : 0;
            failed += regress::isDone(o.status) && o.status != regress::Status::Passed ? 1 : 0;
        }
    const int total = r ? static_cast<int>(r->jobs().size()) : 0;
    if (!session_->isOpen())
        summary_->setText(tr("Open a project (Project > Open) to run its tests."));
    else if (!r)
        summary_->setText(tr("%n test(s) in the project. Run all to start a regression.", "",
                             static_cast<int>(session_->project().tests.size())));
    else if (running)
        summary_->setText(
            tr("Running: %1 of %2 done, %3 passed, %4 failed").arg(done).arg(total).arg(passed).arg(failed));
    else
        summary_->setText(tr("%1 of %2 passed, %3 failed").arg(passed).arg(total).arg(failed) +
                          (session_->mergedCoverage().isEmpty() ? QString() : tr("; coverage merged")));
    const int sel = selectedJob();
    auto set = [this](const char* n, bool on) {
        if (QAction* a = action(n))
            a->setEnabled(on);
    };
    set("regressAll", session_->isOpen() && !running && !session_->project().tests.empty());
    set("regressSelected", !running && sel >= 0);
    set("regressFailed", !running && failed > 0);
    set("regressStop", running);
    set("regressLog", sel >= 0);
    set("regressWave",
        sel >= 0 && r && !r->jobs()[static_cast<std::size_t>(sel)].waveFile.empty() &&
            QFileInfo::exists(QString::fromStdString(r->jobs()[static_cast<std::size_t>(sel)].waveFile)));
    set("regressCoverage", !session_->mergedCoverage().isEmpty());
}

} // namespace lvd::gui
