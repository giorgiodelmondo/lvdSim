// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/breakpoint_pane.hpp"

#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lvd::gui {

namespace {

constexpr int kLineRole = Qt::UserRole + 1; // the line of a line breakpoint row (0 for a time row)
constexpr int kFileRole = Qt::UserRole + 2;

QString idsText(const QVector<int>& ids) {
    QStringList l;
    for (int id : ids)
        l << QString::number(id);
    return l.isEmpty() ? QStringLiteral("-") : l.join(QLatin1Char(','));
}

} // namespace

BreakpointPane::BreakpointPane(QWidget* parent)
    : QWidget(parent) {
    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({tr("Id"), tr("Time / Line"), tr("Hits"), tr("Status")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(qMax(18, fontMetrics().height() + 4));
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->setShowGrid(false);
    table_->setToolTip(tr("Time breakpoints stop once at their time. Line breakpoints (set in the Source "
                          "gutter) stop every process that reaches the line; the check box enables them."));

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
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (rebuilding_ || it->column() != 0 || it->data(kLineRole).toInt() <= 0)
            return;
        emit lineEnableRequested(it->data(kFileRole).toString(), it->data(kLineRole).toInt(),
                                 it->checkState() == Qt::Checked);
    });
    connect(table_, &QTableWidget::cellActivated, this, [this](int r, int) {
        const QTableWidgetItem* it = table_->item(r, 0);
        if (it != nullptr && it->data(kLineRole).toInt() > 0)
            emit lineActivated(it->data(kFileRole).toString(), it->data(kLineRole).toInt());
    });
    rebuild();
}

void BreakpointPane::setBreakpoints(const QVector<BreakpointInfo>& breakpoints) {
    breakpoints_ = breakpoints;
    rebuild();
}

void BreakpointPane::setLineBreakpoints(const QVector<LineBreakpoint>& breakpoints) {
    lines_ = breakpoints;
    rebuild();
}

void BreakpointPane::rebuild() {
    // Keep the selection on the same breakpoint across a refresh.
    QString selFile;
    int selLine = 0, selId = 0;
    if (const int r = selectedRow(); r >= 0) {
        selFile = table_->item(r, 0)->data(kFileRole).toString();
        selLine = table_->item(r, 0)->data(kLineRole).toInt();
        selId = table_->item(r, 0)->data(Qt::UserRole).toInt();
    }
    rebuilding_ = true;
    table_->clearSelection();
    const int timeRows = static_cast<int>(breakpoints_.size());
    table_->setRowCount(timeRows + static_cast<int>(lines_.size()));
    int select = -1;
    for (int i = 0; i < timeRows; ++i) {
        const BreakpointInfo& b = breakpoints_[i];
        auto* id = new QTableWidgetItem(QString::number(b.id));
        id->setData(Qt::UserRole, b.id);
        id->setData(kLineRole, 0);
        table_->setItem(i, 0, id);
        table_->setItem(i, 1, new QTableWidgetItem(formatFemtoseconds(b.timeFs)));
        table_->setItem(i, 2, new QTableWidgetItem());
        table_->setItem(i, 3, new QTableWidgetItem(tr("time, once")));
        if (selLine == 0 && selId != 0 && b.id == selId)
            select = i;
    }
    for (int j = 0; j < lines_.size(); ++j) {
        const LineBreakpoint& b = lines_[j];
        const int r = timeRows + j;
        auto* id = new QTableWidgetItem(idsText(b.ids));
        id->setFlags(id->flags() | Qt::ItemIsUserCheckable);
        id->setCheckState(b.enabled ? Qt::Checked : Qt::Unchecked);
        id->setData(Qt::UserRole, 0);
        id->setData(kLineRole, b.line);
        id->setData(kFileRole, b.file);
        id->setToolTip(tr("Enable or disable this line breakpoint"));
        table_->setItem(r, 0, id);
        auto* where =
            new QTableWidgetItem(QStringLiteral("%1:%2").arg(QFileInfo(b.file).fileName()).arg(b.line));
        where->setToolTip(b.file);
        table_->setItem(r, 1, where);
        table_->setItem(r, 2, new QTableWidgetItem(b.hits > 0 ? QString::number(b.hits) : QString()));
        QString status;
        if (!b.enabled)
            status = tr("disabled");
        else
            switch (b.state) {
            case LineBreakpoint::State::Planned:
                status = tr("set when a simulation starts");
                break;
            case LineBreakpoint::State::Pending:
                status = tr("sending...");
                break;
            case LineBreakpoint::State::Verified:
                status = tr("line");
                break;
            case LineBreakpoint::State::Rejected:
                status = tr("not set: %1").arg(b.note);
                break;
            }
        auto* st = new QTableWidgetItem(status);
        if (b.state == LineBreakpoint::State::Rejected && b.enabled) {
            st->setForeground(QColor(0xb0, 0x30, 0x30));
            st->setToolTip(b.note);
        }
        table_->setItem(r, 3, st);
        if (selLine > 0 && b.line == selLine && sameSourceFile(b.file, selFile))
            select = r;
    }
    if (select >= 0)
        table_->selectRow(select);
    rebuilding_ = false;
    updateButtons();
}

int BreakpointPane::rowOfLine(const QString& file, int line) const {
    for (int r = 0; r < table_->rowCount(); ++r) {
        const QTableWidgetItem* it = table_->item(r, 0);
        if (it != nullptr && it->data(kLineRole).toInt() == line &&
            sameSourceFile(it->data(kFileRole).toString(), file))
            return r;
    }
    return -1;
}

int BreakpointPane::selectedRow() const {
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty() || table_->item(rows.first().row(), 0) == nullptr)
        return -1;
    return rows.first().row();
}

void BreakpointPane::updateButtons() {
    deleteAll_->setEnabled(!breakpoints_.isEmpty() || !lines_.isEmpty());
    delete_->setEnabled(selectedRow() >= 0);
}

int BreakpointPane::selectedId() const {
    const int r = selectedRow();
    if (r < 0)
        return 0;
    return table_->item(r, 0)->data(Qt::UserRole).toInt();
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
    const int r = selectedRow();
    if (r < 0)
        return;
    const QTableWidgetItem* it = table_->item(r, 0);
    if (const int line = it->data(kLineRole).toInt(); line > 0)
        emit lineDeleteRequested(it->data(kFileRole).toString(), line);
    else if (const int id = it->data(Qt::UserRole).toInt(); id != 0)
        emit deleteRequested(id);
}

} // namespace lvd::gui
