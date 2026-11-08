// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QToolBar>

class QLineEdit;

namespace lvd::gui {

class WaveWidget;

/// Toolbar for a WaveWidget: go to the beginning or end of the simulation, previous and next
/// transition, go to an entered time ("1500 ps", "12.5 ns"), zoom, cursors and dividers.
///
/// Actions have object names (goStart, prevEdge, nextEdge, goEnd, goTo, zoomIn, zoomOut, zoomFit,
/// addCursor, removeCursor, addDivider, follow) so tests and shortcuts can find them.
class WaveToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit WaveToolBar(WaveWidget* wave, QWidget* parent = nullptr);

    /// Parse the text as a time and move there. A number without unit is in ns. Returns false (and marks
    /// the entry red) if the text is not a time.
    bool goToText(const QString& text);
    QLineEdit* timeEdit() const noexcept { return edit_; }
    /// The checkable "Follow simulation" action (object name follow). It mirrors WaveWidget::follow() in both
    /// directions; add it to a menu to get the same toggle there.
    QAction* followAction() const noexcept { return follow_; }

private:
    QAction* add(const QString& objectName, const QString& text, const QString& tip, const QIcon& icon = {});

    WaveWidget* wave_;
    QLineEdit* edit_ = nullptr;
    QAction* follow_ = nullptr;
};

}  // namespace lvd::gui
