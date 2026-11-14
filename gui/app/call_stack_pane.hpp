// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/parsers/json_replies.hpp"

#include <QVector>
#include <QWidget>

class QLabel;
class QTreeWidget;

namespace lvd::gui {

/// Call Stack dock: the frames of the process the debugger stopped in (`stack -json`), innermost first.
/// Selecting a frame asks for its source line and its variables (frameSelected).
class CallStackPane : public QWidget {
    Q_OBJECT
public:
    explicit CallStackPane(QWidget* parent = nullptr);

    /// Shows the frames and selects `selected` without emitting frameSelected.
    void setFrames(const QString& process, const QVector<SourceFrame>& frames, int selected = 0);
    /// Empty list with a note ("Not stopped", ...).
    void clear(const QString& note = {});
    QVector<SourceFrame> frames() const { return frames_; }
    int frameCount() const noexcept { return static_cast<int>(frames_.size()); }
    /// Level of the selected frame, -1 if none.
    int selectedLevel() const;
    /// Selects a frame as a click does (emits frameSelected when it changes).
    void selectLevel(int level);

    QTreeWidget* tree() const noexcept { return tree_; }

signals:
    void frameSelected(int level);

private:
    QTreeWidget* tree_ = nullptr;
    QLabel* note_ = nullptr;
    QVector<SourceFrame> frames_;
    bool filling_ = false;
};

} // namespace lvd::gui
