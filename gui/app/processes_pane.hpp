// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/parsers/json_replies.hpp"

#include <QVector>
#include <QWidget>

class QTableWidget;

namespace lvd::gui {

/// Processes dock: one row per process of the design with name, path, state (ready, delayed, waiting) and
/// the source location. Double-click (or Return) on a row asks for the file to be shown at that line;
/// selecting a row only reports the location, so that the source pane can mark it.
class ProcessesPane : public QWidget {
    Q_OBJECT
public:
    explicit ProcessesPane(QWidget* parent = nullptr);

    /// Replaces the rows. The selected process (by path) stays selected if it still exists.
    void setProcesses(const QVector<ProcessInfo>& processes);
    int processCount() const noexcept { return static_cast<int>(processes_.size()); }
    QVector<ProcessInfo> processes() const { return processes_; }
    /// The row's process, for tests and for the source pane; an empty ProcessInfo if the row is invalid.
    ProcessInfo processAt(int row) const;
    ProcessInfo selectedProcess() const;
    /// Lines of the given file where a process is ready to run.
    QSet<int> readyLines(const QString& file) const;

    QTableWidget* table() const noexcept { return table_; }

signals:
    void refreshRequested();
    /// Double-click or Return: show `file` at `line`. Emitted even when the location is unknown (empty file).
    void openRequested(const QString& file, int line);
    /// Selection changed (file empty if unknown, or nothing selected).
    void processSelected(const QString& file, int line);

private:
    void emitSelection();

    QTableWidget* table_ = nullptr;
    QVector<ProcessInfo> processes_;
};

} // namespace lvd::gui
