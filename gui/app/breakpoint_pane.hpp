// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/line_breakpoints.hpp"
#include "sim/parsers/json_replies.hpp"

#include <QVector>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace lvd::gui {

/// Breakpoint list of the simulation: time breakpoints (one-shot, absolute times; the kernel deletes one when
/// it is reached) followed by source line breakpoints (with a check box to enable or disable them, the number
/// of stops and their state). The pane only shows what it is given and asks for changes through signals;
/// SimWindow sends the commands and feeds the answers back.
class BreakpointPane : public QWidget {
    Q_OBJECT
public:
    explicit BreakpointPane(QWidget* parent = nullptr);

    void setBreakpoints(const QVector<BreakpointInfo>& breakpoints);
    QVector<BreakpointInfo> breakpoints() const { return breakpoints_; }
    /// Time breakpoints only.
    int breakpointCount() const noexcept { return static_cast<int>(breakpoints_.size()); }
    void setLineBreakpoints(const QVector<LineBreakpoint>& breakpoints);
    QVector<LineBreakpoint> lineBreakpoints() const { return lines_; }
    int lineBreakpointCount() const noexcept { return static_cast<int>(lines_.size()); }
    /// The table row of a line breakpoint, -1 if it is not listed.
    int rowOfLine(const QString& file, int line) const;

    /// The id of the selected time breakpoint, 0 if none (or a line breakpoint is selected).
    int selectedId() const;

    /// Text of the time box and the unit selector, for tests and for loading a script.
    void setTimeText(const QString& text, const QString& unit);
    /// Same as pressing Add. Returns false (and says why in the status label) if the time is not a
    /// positive decimal number.
    bool submitAdd();
    /// Delete the selected row (a time or a line breakpoint).
    void requestDeleteSelected();

    QTableWidget* table() const noexcept { return table_; }
    QLineEdit* timeEdit() const noexcept { return time_; }
    QComboBox* unitBox() const noexcept { return unit_; }

signals:
    void addRequested(const QString& time, const QString& unit);
    void deleteRequested(int id);
    void deleteAllRequested();
    void lineEnableRequested(const QString& file, int line, bool enabled);
    void lineDeleteRequested(const QString& file, int line);
    /// A line breakpoint was double clicked: show it in the Source pane.
    void lineActivated(const QString& file, int line);

private:
    void rebuild();
    void updateButtons();
    int selectedRow() const;

    QTableWidget* table_ = nullptr;
    QLineEdit* time_ = nullptr;
    QComboBox* unit_ = nullptr;
    QPushButton* delete_ = nullptr;
    QPushButton* deleteAll_ = nullptr;
    QLabel* hint_ = nullptr;
    QVector<BreakpointInfo> breakpoints_;
    QVector<LineBreakpoint> lines_;
    bool rebuilding_ = false;
};

} // namespace lvd::gui
