// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/project_session.hpp"

#include <QWidget>

class QLabel;
class QTreeWidget;

namespace lvd::gui {

/// The Problems dock: errors and warnings of the last analysis, one row each (severity, message, file, line).
/// Activating a row asks for its file and line in the Source pane.
class ProblemsPane : public QWidget {
    Q_OBJECT
public:
    explicit ProblemsPane(QWidget* parent = nullptr);

    void setProblems(const QVector<Problem>& problems);
    int count() const;
    int errorCount() const noexcept { return errors_; }
    QTreeWidget* tree() const noexcept { return tree_; }
    /// What activating row `row` does.
    void activateRow(int row);

signals:
    void sourceRequested(const QString& file, int line);

private:
    QTreeWidget* tree_ = nullptr;
    QLabel* summary_ = nullptr;
    QVector<Problem> problems_;
    int errors_ = 0;
};

} // namespace lvd::gui
