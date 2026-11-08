// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/breakpoint_pane.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lvd::gui {

BreakpointPane::BreakpointPane(QWidget* parent)
    : QWidget(parent) {
    table_ = new QTableWidget(0, 2, this);
    table_->setHorizontalHeaderLabels({tr("Id"), tr("Time")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(qMax(18, fontMetrics().height() + 4));
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setShowGrid(false);

    time_ = new QLineEdit(this);
    time_->setPlaceholderText(tr("time"));
    time_->setFixedWidth(90);
    time_->setToolTip(tr("Absolute simulation time, for example 250 or 1.5"));
    unit_ = new QComboBox(this);
    unit_->addItems({"fs", "ps", "ns", "us", "ms"});
    unit_->setCurrentText("ns");
    auto* add = new QPushButton(tr("Add"), this);
    add->setToolTip(tr("Stop the simulation when this time is reached (once)"));
    delete_ = new QPushButton(tr("Delete"), this);
    deleteAll_ = new QPushButton(tr("Delete all"), this);
    hint_ = new QLabel(this);

    auto* row = new QHBoxLayout;
    row->addWidget(time_);
    row->addWidget(unit_);
    row->addWidget(add);
    row->addStretch(1);
    row->addWidget(delete_);
    row->addWidget(deleteAll_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(table_, 1);
    layout->addLayout(row);
    layout->addWidget(hint_);

    connect(add, &QPushButton::clicked, this, [this] { submitAdd(); });
    connect(time_, &QLineEdit::returnPressed, this, [this] { submitAdd(); });
    connect(delete_, &QPushButton::clicked, this, [this] { requestDeleteSelected(); });
    connect(deleteAll_, &QPushButton::clicked, this, &BreakpointPane::deleteAllRequested);
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            [this] { delete_->setEnabled(selectedId() != 0); });
    setBreakpoints({});
}

void BreakpointPane::setBreakpoints(const QVector<BreakpointInfo>& breakpoints) {
    breakpoints_ = breakpoints;
    table_->setRowCount(static_cast<int>(breakpoints.size()));
    for (int i = 0; i < breakpoints.size(); ++i) {
        auto* id = new QTableWidgetItem(QString::number(breakpoints[i].id));
        id->setData(Qt::UserRole, breakpoints[i].id);
        table_->setItem(i, 0, id);
        table_->setItem(i, 1, new QTableWidgetItem(formatFemtoseconds(breakpoints[i].timeFs)));
    }
    deleteAll_->setEnabled(!breakpoints.isEmpty());
    delete_->setEnabled(selectedId() != 0);
}

int BreakpointPane::selectedId() const {
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return 0;
    const QTableWidgetItem* it = table_->item(rows.first().row(), 0);
    return it ? it->data(Qt::UserRole).toInt() : 0;
}

void BreakpointPane::setTimeText(const QString& text, const QString& unit) {
    time_->setText(text);
    unit_->setCurrentText(unit);
}

bool BreakpointPane::submitAdd() {
    const QString t = time_->text().trimmed();
    static const QRegularExpression number(QStringLiteral("^([0-9]+\\.?[0-9]*|\\.[0-9]+)$"));
    if (!number.match(t).hasMatch() || t.toDouble() <= 0.0) {
        hint_->setText(tr("Enter a positive time, for example 250"));
        return false;
    }
    hint_->clear();
    emit addRequested(t, unit_->currentText());
    return true;
}

void BreakpointPane::requestDeleteSelected() {
    const int id = selectedId();
    if (id != 0)
        emit deleteRequested(id);
}

} // namespace lvd::gui
