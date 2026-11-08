// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/processes_pane.hpp"

#include <QFileInfo>
#include <QHeaderView>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lvd::gui {

namespace {

enum Column { kName, kPath, kState, kLocation, kColumnCount };

QColor stateColor(const QString& state) {
    if (state == QLatin1String("ready"))
        return QColor(0x2e, 0xa0, 0x43);
    if (state == QLatin1String("delayed"))
        return QColor(0xc0, 0x80, 0x00);
    return QColor();
}

} // namespace

ProcessesPane::ProcessesPane(QWidget* parent)
    : QWidget(parent) {
    table_ = new QTableWidget(0, kColumnCount, this);
    table_->setHorizontalHeaderLabels({tr("Name"), tr("Path"), tr("State"), tr("Source")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(qMax(18, fontMetrics().height() + 4));
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(kPath, QHeaderView::Stretch);
    table_->setShowGrid(false);

    auto* refresh = new QPushButton(tr("Refresh"), this);
    refresh->setToolTip(tr("Read the process list from the simulator again"));
    connect(refresh, &QPushButton::clicked, this, &ProcessesPane::refreshRequested);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(table_, 1);
    layout->addWidget(refresh, 0, Qt::AlignRight);

    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { emitSelection(); });
    connect(table_, &QTableWidget::cellActivated, this, [this](int row, int) {
        const ProcessInfo p = processAt(row);
        emit openRequested(p.file, p.line);
    });
}

ProcessInfo ProcessesPane::processAt(int row) const {
    // the table may be sorted by the user, so the process comes from the item, not from the row number
    const QTableWidgetItem* it = table_->item(row, kName);
    if (it == nullptr)
        return {};
    return processes_.value(it->data(Qt::UserRole).toInt());
}

ProcessInfo ProcessesPane::selectedProcess() const {
    const auto rows = table_->selectionModel()->selectedRows();
    return rows.isEmpty() ? ProcessInfo{} : processAt(rows.first().row());
}

void ProcessesPane::emitSelection() {
    const ProcessInfo p = selectedProcess();
    emit processSelected(p.file, p.line);
}

QSet<int> ProcessesPane::readyLines(const QString& file) const {
    QSet<int> lines;
    const QString want = QFileInfo(file).absoluteFilePath();
    for (const ProcessInfo& p : processes_)
        if (p.state == QLatin1String("ready") && p.line > 0 && !p.file.isEmpty() &&
            QFileInfo(p.file).absoluteFilePath() == want)
            lines.insert(p.line);
    return lines;
}

void ProcessesPane::setProcesses(const QVector<ProcessInfo>& processes) {
    const QString keep = selectedProcess().path;
    const bool sorting = table_->isSortingEnabled();
    table_->setSortingEnabled(false);
    table_->blockSignals(true);
    processes_ = processes;
    table_->clearContents();
    table_->setRowCount(static_cast<int>(processes.size()));
    int select = -1;
    for (int i = 0; i < processes.size(); ++i) {
        const ProcessInfo& p = processes[i];
        const QString where = p.file.isEmpty()
                                  ? QString()
                                  : QStringLiteral("%1:%2").arg(QFileInfo(p.file).fileName()).arg(p.line);
        auto* name = new QTableWidgetItem(p.name);
        name->setData(Qt::UserRole, i);
        auto* state = new QTableWidgetItem(p.state);
        if (const QColor c = stateColor(p.state); c.isValid())
            state->setForeground(c);
        auto* loc = new QTableWidgetItem(where);
        loc->setToolTip(p.file);
        table_->setItem(i, kName, name);
        table_->setItem(i, kPath, new QTableWidgetItem(p.path));
        table_->setItem(i, kState, state);
        table_->setItem(i, kLocation, loc);
        if (!keep.isEmpty() && p.path == keep)
            select = i;
    }
    table_->blockSignals(false);
    table_->setSortingEnabled(sorting);
    if (select >= 0)
        table_->selectRow(select);
    else
        emitSelection();
}

} // namespace lvd::gui
