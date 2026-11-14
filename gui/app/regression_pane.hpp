// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QAction;
class QLabel;
class QSpinBox;
class QTableWidget;
class QToolBar;

namespace lvd::gui {

class ProjectSession;

/// The Regression dock: one row per test run (test, seed, status, time, messages, result), a tool bar to run
/// all tests, the selected ones or the failed ones again, to stop, and to open the log or the waveform of a
/// run and the merged coverage. The number of parallel runs is set in the tool bar.
class RegressionPane : public QWidget {
    Q_OBJECT
public:
    enum Column { ColTest, ColSeed, ColStatus, ColTime, ColErrors, ColMessage, ColumnCount };

    explicit RegressionPane(ProjectSession* session, QWidget* parent = nullptr);

    QTableWidget* table() const noexcept { return table_; }
    QSpinBox* jobsBox() const noexcept { return jobs_; }
    /// Tool bar actions by object name: regressAll, regressSelected, regressFailed, regressStop, regressLog,
    /// regressWave, regressCoverage.
    QAction* action(const QString& objectName) const;
    /// Job index of the selected row, -1 if none.
    int selectedJob() const;
    QString summaryText() const;

signals:
    void logRequested(const QString& file);
    void waveRequested(const QString& file, const QString& name);
    void coverageRequested(const QString& database);

private:
    void rebuild();
    void updateRow(int job);
    void updateActions();

    ProjectSession* session_ = nullptr;
    QToolBar* toolBar_ = nullptr;
    QTableWidget* table_ = nullptr;
    QSpinBox* jobs_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace lvd::gui
