// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/parsers/json_replies.hpp"

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace lvd::gui {

/// Breakpoint list of the simulation. Breakpoints are one-shot, absolute times: the kernel deletes one when
/// it is reached. The pane only shows what it is given and asks for changes through signals; SimWindow sends
/// the commands and feeds the answers back.
class BreakpointPane : public QWidget {
    Q_OBJECT
public:
    explicit BreakpointPane(QWidget* parent = nullptr);

    void setBreakpoints(const QVector<BreakpointInfo>& breakpoints);
    QVector<BreakpointInfo> breakpoints() const { return breakpoints_; }
    int breakpointCount() const noexcept { return static_cast<int>(breakpoints_.size()); }
    /// The id of the selected row, 0 if none.
    int selectedId() const;

    /// Text of the time box and the unit selector, for tests and for loading a script.
    void setTimeText(const QString& text, const QString& unit);
    /// Same as pressing Add. Returns false (and says why in the status label) if the time is not a
    /// positive decimal number.
    bool submitAdd();
    void requestDeleteSelected();

    QTableWidget* table() const noexcept { return table_; }
    QLineEdit* timeEdit() const noexcept { return time_; }
    QComboBox* unitBox() const noexcept { return unit_; }

signals:
    void addRequested(const QString& time, const QString& unit);
    void deleteRequested(int id);
    void deleteAllRequested();

private:
    QTableWidget* table_ = nullptr;
    QLineEdit* time_ = nullptr;
    QComboBox* unit_ = nullptr;
    QPushButton* delete_ = nullptr;
    QPushButton* deleteAll_ = nullptr;
    QLabel* hint_ = nullptr;
    QVector<BreakpointInfo> breakpoints_;
};

} // namespace lvd::gui
